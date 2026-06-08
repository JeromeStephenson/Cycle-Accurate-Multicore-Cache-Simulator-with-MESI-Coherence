# Cycle-Accurate Multicore Cache Simulator with MESI Coherence

A lightweight, object-oriented, cycle-accurate C++ simulation model of a multi-core processor memory hierarchy. The engine simulates private L1 caches, an interconnect snooping bus, and evaluates average memory access time ($AMAT$) metrics alongside hardware coherence states using the **MESI (Modified, Exclusive, Shared, Invalid)** protocol.

##  Features
* **Parameterized Cache Architecture:** Configurable cache line size, total capacity, and set-associativity with a Pseudo-LRU replacement algorithm.
* **MESI Cache Coherence Protocol:** Full implementation of bus-snooping state logic handling read misses, write hits, and upgrade invalidations.
* **Cycle-Accurate Latency Model:** Accounts for microarchitectural execution stalls including L1/L2 hits, bus snooping penalties, and main memory (DRAM) access cycles.
* **Hardware Bottleneck Analysis:** Built-in telemetry to capture and identify real-world memory phenomenon like cache thrashing, true sharing, and invalidation storms.

---

##  Microarchitectural Specification

The execution engine is modeled around a highly tight, multi-core memory infrastructure:

| Hardware Component | Default Value / Latency |
| :--- | :--- |
| **Cache Line Size** | 64 Bytes |
| **L1 Cache Geometry** | 32 KB, 4-Way Set Associative |
| **L1 Hit Latency** | 4 Clock Cycles |
| **L2 Hit Latency** | 15 Clock Cycles |
| **Bus Snooping Overhead** | 10 Clock Cycles |
| **Main Memory Latency** | 150 Clock Cycles |

---

##  Sample Performance Analysis Output

Running a dense, interleaved data sharing stress test across 4 simulated cores demonstrates the devastating performance impact of **Cache Thrashing / True Sharing**:

```text
=======================================================
          ADVANCED MULTICORE PERFORMANCE ANALYSIS       
=======================================================
 Core 0 -> L1 Hits:  199 | L1 Misses:  201 | Miss Rate: 50.25%
 Core 1 -> L1 Hits:    0 | L1 Misses:  200 | Miss Rate: 100.00%
 Core 2 -> L1 Hits:  199 | L1 Misses:    1 | Miss Rate: 0.50%
 Core 3 -> L1 Hits:    0 | L1 Misses:    0 | Miss Rate: 0.00%
-------------------------------------------------------
 Total Simulated Execution Cost: 15365 Clock Cycles
 Simulated Average Cycles Per Memory Access (AMAT): 19.21
=======================================================
