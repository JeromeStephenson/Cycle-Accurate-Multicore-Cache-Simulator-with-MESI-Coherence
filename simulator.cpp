#include <iostream>
#include <vector>
#include <cmath>
#include <cstdint>
#include <iomanip>

const uint32_t BLOCK_SIZE = 64;

// MESI Protocol States
enum class MESIState {
    MODIFIED,   // Exclusive, dirty (must write back on eviction)
    EXCLUSIVE,  // Exclusive, clean
    SHARED,     // Potentially shared, clean
    INVALID     // Line contains no valid data
};

struct AdvancedLine {
    uint64_t tag = 0;
    MESIState state = MESIState::INVALID;
    uint32_t lru_counter = 0; // Pseudo-LRU age tracker
};

class L1Cache {
private:
    uint32_t size;
    uint32_t associativity;
    uint32_t num_sets;
    uint32_t index_bits;
    uint32_t offset_bits;

public:
    std::vector<std::vector<AdvancedLine>> sets;
    uint64_t hits = 0;
    uint64_t misses = 0;

    L1Cache(uint32_t size_bytes, uint32_t assoc) : size(size_bytes), associativity(assoc) {
        num_sets = size / (BLOCK_SIZE * associativity);
        offset_bits = std::log2(BLOCK_SIZE);
        index_bits = std::log2(num_sets);
        sets.resize(num_sets, std::vector<AdvancedLine>(associativity));
    }

    void decode(uint64_t address, uint64_t& tag, uint64_t& index) {
        index = (address >> offset_bits) & (num_sets - 1);
        tag = address >> (offset_bits + index_bits);
    }

    // Updates LRU age counters within a targeted set
    void update_lru(uint64_t index, uint32_t hit_way) {
        uint32_t max_age = sets[index][hit_way].lru_counter;
        for (uint32_t i = 0; i < associativity; ++i) {
            if (sets[index][i].lru_counter < max_age && sets[index][i].state != MESIState::INVALID) {
                sets[index][i].lru_counter++;
            }
        }
        sets[index][hit_way].lru_counter = 0; // 0 represents Most Recently Used (MRU)
    }

    int find_way(uint64_t index, uint64_t tag) {
        for (uint32_t i = 0; i < associativity; ++i) {
            if (sets[index][i].state != MESIState::INVALID && sets[index][i].tag == tag) {
                return i;
            }
        }
        return -1;
    }

    int get_eviction_way(uint64_t index) {
        uint32_t max_age = 0;
        int victim = 0;
        for (uint32_t i = 0; i < associativity; ++i) {
            if (sets[index][i].state == MESIState::INVALID) return i; // Always take an invalid line first
            if (sets[index][i].lru_counter >= max_age) {
                max_age = sets[index][i].lru_counter;
                victim = i;
            }
        }
        return victim;
    }
};

class AdvancedMulticoreSystem {
private:
    std::vector<L1Cache> l1_cores;
    uint32_t num_cores;
    uint64_t total_cycles = 0;

    // Fixed Cycle Latencies
    const uint32_t L1_HIT_LATENCY = 4;
    const uint32_t L2_HIT_LATENCY = 15;
    const uint32_t BUS_SNOOP_LATENCY = 10;
    const uint32_t MEMORY_LATENCY = 150;

public:
    AdvancedMulticoreSystem(uint32_t cores, uint32_t l1_size, uint32_t l1_assoc)
        : num_cores(cores), l1_cores(cores, L1Cache(l1_size, l1_assoc)) {}

    // Executes a cycle-accurate memory transaction utilizing MESI Snooping on a shared bus
    void execute_transaction(uint32_t core_id, uint64_t address, bool is_write) {
        uint64_t tag, index;
        l1_cores[core_id].decode(address, tag, index);
        
        int hit_way = l1_cores[core_id].find_way(index, tag);
        bool is_hit = (hit_way != -1);

        if (is_hit) {
            l1_cores[core_id].hits++;
            MESIState current_state = l1_cores[core_id].sets[index][hit_way].state;

            if (!is_write) {
                // Read Hit: No state transitions needed
                total_cycles += L1_HIT_LATENCY;
                l1_cores[core_id].update_lru(index, hit_way);
            } else {
                // Write Hit
                if (current_state == MESIState::MODIFIED) {
                    total_cycles += L1_HIT_LATENCY; // Fast local write execution
                } else if (current_state == MESIState::EXCLUSIVE) {
                    l1_cores[core_id].sets[index][hit_way].state = MESIState::MODIFIED;
                    total_cycles += L1_HIT_LATENCY;
                } else if (current_state == MESIState::SHARED) {
                    // Upgrade Miss / Invalidation broadcast across the interconnect bus
                    total_cycles += L1_HIT_LATENCY + BUS_SNOOP_LATENCY;
                    snoop_bus(core_id, address, true); // Invalidate others
                    l1_cores[core_id].sets[index][hit_way].state = MESIState::MODIFIED;
                }
                l1_cores[core_id].update_lru(index, hit_way);
            }
        } else {
            // Cache Miss Handling
            l1_cores[core_id].misses++;
            int victim_way = l1_cores[core_id].get_eviction_way(index);
            AdvancedLine& victim = l1_cores[core_id].sets[index][victim_way];

            // If replacing a modified block, simulate write-back latency overhead
            if (victim.state == MESIState::MODIFIED) {
                total_cycles += L2_HIT_LATENCY; 
            }

            // Snoop other cores to see who has the line
            bool found_elsewhere = snoop_bus(core_id, address, is_write);

            if (!is_write) {
                // Read Miss
                if (found_elsewhere) {
                    victim.state = MESIState::SHARED;
                    total_cycles += L1_HIT_LATENCY + L2_HIT_LATENCY + BUS_SNOOP_LATENCY;
                } else {
                    victim.state = MESIState::EXCLUSIVE;
                    total_cycles += L1_HIT_LATENCY + MEMORY_LATENCY; // Fetch directly from DRAM
                }
            } else {
                // Write Miss (Read-With-Intent-To-Modify)
                victim.state = MESIState::MODIFIED;
                if (found_elsewhere) {
                    total_cycles += L1_HIT_LATENCY + L2_HIT_LATENCY + BUS_SNOOP_LATENCY;
                } else {
                    total_cycles += L1_HIT_LATENCY + MEMORY_LATENCY;
                }
            }
            victim.tag = tag;
            l1_cores[core_id].update_lru(index, victim_way);
        }
    }

    // Bus snooping logic implementation
    bool snoop_bus(uint32_t requesting_core, uint64_t address, bool is_write_intent) {
        bool cached_elsewhere = false;
        for (uint32_t i = 0; i < num_cores; ++i) {
            if (i == requesting_core) continue;

            uint64_t snoop_tag, snoop_index;
            l1_cores[i].decode(address, snoop_tag, snoop_index);
            int remote_way = l1_cores[i].find_way(snoop_index, snoop_tag);

            if (remote_way != -1) {
                cached_elsewhere = true;
                MESIState& remote_state = l1_cores[i].sets[snoop_index][remote_way].state;

                if (is_write_intent) {
                    // All other copies must transition to Invalid
                    remote_state = MESIState::INVALID;
                } else {
                    // Read request downgrades Modified or Exclusive copies to Shared
                    if (remote_state == MESIState::MODIFIED || remote_state == MESIState::EXCLUSIVE) {
                        remote_state = MESIState::SHARED;
                    }
                }
            }
        }
        return cached_elsewhere;
    }

    void print_advanced_analysis() {
        std::cout << "\n=======================================================\n";
        std::cout << "          ADVANCED MULTICORE PERFORMANCE ANALYSIS       \n";
        std::cout << "=======================================================\n";
        uint64_t global_requests = 0;
        
        for (uint32_t i = 0; i < num_cores; ++i) {
            uint64_t core_reqs = l1_cores[i].hits + l1_cores[i].misses;
            global_requests += core_reqs;
            double miss_rate = core_reqs ? ((double)l1_cores[i].misses / core_reqs) * 100 : 0.0;
            std::cout << " Core " << i << " -> L1 Hits: " << std::setw(4) << l1_cores[i].hits
                      << " | L1 Misses: " << std::setw(4) << l1_cores[i].misses
                      << " | Miss Rate: " << std::fixed << std::setprecision(2) << miss_rate << "%\n";
        }
        
        double overall_cpi = (double)total_cycles / (global_requests ? global_requests : 1);
        std::cout << "-------------------------------------------------------\n";
        std::cout << " Total Simulated Execution Cost: " << total_cycles << " Clock Cycles\n";
        std::cout << " Simulated Average Cycles Per Memory Access (AMAT): " << overall_cpi << "\n";
        std::cout << "=======================================================\n";
    }
};

int main() {
    // 4 Cores, 32KB L1 Caches (4-way associative)
    AdvancedMulticoreSystem processor(4, 32768, 4);

    // Scenario: Heavy Inter-Core Sharing & Contention (True Sharing Stress Test)
    uint64_t target_addr = 0x7FFF0000;

    for (int step = 0; step < 200; ++step) {
        processor.execute_transaction(0, target_addr, false); // Core 0 reads -> Fetches from Main Memory (Exclusive)
        processor.execute_transaction(1, target_addr, false); // Core 1 reads -> Snoops Core 0 (Transitions to Shared)
        processor.execute_transaction(2, target_addr, true);  // Core 2 writes -> Invalidation Storm triggered! (Modified)
        processor.execute_transaction(0, target_addr, false); // Core 0 reads -> Cache Miss again due to invalidation.
    }

    processor.print_advanced_analysis();
    return 0;
}
