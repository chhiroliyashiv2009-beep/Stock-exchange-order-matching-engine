# High-Performance C++20 Limit Order Book & Matching Engine

A low-latency, deterministic, zero-allocation C++20 Limit Order Book (LOB) and matching engine designed for high-frequency trading (HFT) environments. Features an intrusive lock-free Single Producer Single Consumer (SPSC) ring buffer, custom intrusive memory pools, PMR monotonic memory indexing, and strict price-time priority execution.

---

## Key Features

- **C++20 Zero-Allocation Hot Path**:
  - Pre-allocated intrusive [`MemoryPool<T, N>`](src/core/memory_pool.h) ensuring zero dynamic heap allocations (`malloc`/`new`) during live matching.
  - Polymorphic Memory Resources (`std::pmr`) with monotonic buffer resources backed by `null_memory_resource` for deterministic memory bounding.
- **Lock-Free Concurrency**:
  - Cache-line padded (128-byte aligned) [`SPSCQueue<T, N>`](src/core/scsc_queue.h) ring buffer preventing false sharing between producer and consumer threads.
  - Acquire-release memory ordering semantics with power-of-two bitmask indexing.
- **Strict Price-Time Priority (FIFO)**:
  - Bids ordered descending (`std::greater`), asks ordered ascending (`std::less`).
  - Doubly-linked list price levels preserving arrival order for orders with identical prices.
- **Order Types Supported**:
  - `LIMIT` (standard passive/aggressive resting limit orders)
  - `MARKET` (immediate liquidity taker orders)
  - `IOC` (Immediate-Or-Cancel with residual volume cancellation)
  - `FOK` (Fill-Or-Kill all-or-none execution)
  - `POST_ONLY` (maker liquidity only; rejected if crossing spread)
  - `ICEBERG` (hidden reserve replenishment with queue penalty)
  - `STOP` & `STOP_LIMIT` (off-book parked orders activated on trade price triggers)
- **Comprehensive Unit Testing**:
  - 33 Google Test (GTest) unit tests covering concurrency, price-time priority, partial fills, crossing spreads, and pool exhaustion / leak prevention.

---

## Architecture Overview

```
                      +-----------------------------+
                      |   Inbound Order Stream      |
                      +--------------+--------------+
                                     |
                                     v
                      +-----------------------------+
                      |  Lock-Free SPSC Ring Buffer |  <-- 128-byte cursor alignment
                      +--------------+--------------+
                                     |
                                     v
                       +---------------------------+
                       |      Matching Engine      |
                       +-------------+-------------+
                                     |
           +-------------------------+-------------------------+
           |                                                   |
           v                                                   v
+----------------------+                            +----------------------+
|  Bids Map (greater)  |                            |   Asks Map (less)    |
|  [PMR Monotonic]     |                            |   [PMR Monotonic]    |
+----------+-----------+                            +----------+-----------+
           |                                                   |
           v                                                   v
+----------------------+                            +----------------------+
| Level Doubly-Linked  |                            | Level Doubly-Linked  |
| List (FIFO Queue)    |                            | List (FIFO Queue)    |
+----------+-----------+                            +----------+-----------+
           |                                                   |
           +-------------------------+-------------------------+
                                     |
                                     v
                       +---------------------------+
                       |    Intrusive MemoryPool   |  <-- 64-byte aligned slots,
                       |    (Orders & Levels)      |      O(1) allocation/deallocation
                       +---------------------------+
```

---

## Project Structure

```text
STOCK_EXCHANGE/
├── CMakeLists.txt              # CMake build configuration with GTest & Ninja support
├── README.md                   # Project documentation
├── src/
│   ├── core/
│   │   ├── clock.h             # Steady timestamp and logical clock utilities
│   │   ├── memory_pool.h       # Intrusive fixed-capacity zero-allocation memory pool
│   │   ├── scsc_queue.h        # Lock-free SPSC circular ring buffer
│   │   └── types.h             # Fundamental types, price scaling, fixed-point representation
│   ├── matching/
│   │   ├── events.h            # Trivially-copyable execution reports and trade events
│   │   ├── level.h             # Cache-aligned price level FIFO queue
│   │   ├── matching_engine.h   # Multi-symbol engine dispatcher
│   │   ├── order.h             # 64-byte aligned compact order structure
│   │   └── order_book.h        # Core matching logic, book maps, and lifecycle management
│   └── exchange_main.cpp       # Main exchange gateway entry point
├── tests/
│   ├── test_crossing_orders.cpp              # Spread clearance, post-only, IOC/FOK tests
│   ├── test_helpers.h                        # Order factories and event assertion utilities
│   ├── test_matching_engine_pool_limits.cpp  # Engine pool exhaustion and leak tests
│   ├── test_memory_pool.cpp                  # Memory pool boundaries and recycling tests
│   ├── test_partial_fills_and_queue.cpp      # Queue retention and quantity modification tests
│   ├── test_price_time_priority.cpp          # Strict price and FIFO time priority tests
│   └── test_spsc_queue.cpp                   # Multi-threaded concurrent stress tests
└── tools/
    ├── liquidity_bot.cpp       # Automated market-making liquidity simulator
    └── trader_terminal.cpp     # Interactive trader client terminal
```

---

## Prerequisites

- **C++ Compiler**: GCC $\ge$ 13 or Clang $\ge$ 16 with C++20 support (e.g. MSYS2 UCRT64 on Windows).
- **Build System**: [CMake](https://cmake.org/) $\ge$ 3.24 and [Ninja](https://ninja-build.org/).
- **Testing Framework**: [Google Test](https://github.com/google/googletest) (`mingw-w64-ucrt-x86_64-gtest` or system package).

---

## Build & Installation

### 1. Configure Build Directory
```bash
cmake -B build -G Ninja
```

### 2. Build Targets
Build the exchange, tool binaries, and test suite:
```bash
ninja -C build
```

The resulting binaries will be placed in:
- `build/src/exchange.exe` — Exchange engine executable
- `build/tools/trader_terminal.exe` — Trader terminal client
- `build/tools/liquidity_bot.exe` — Liquidity generator bot
- `build/tests/matching_engine_tests.exe` — Unit test executable

---

## Running the Unit Test Suite

### Run All Tests Directly
```bash
./build/tests/matching_engine_tests.exe
```

### Run via CTest
```bash
ctest --test-dir build --output-on-failure
```

### Filter Specific Test Suites
You can use `--gtest_filter` to run individual test suites:

- **Price-Time Priority Tests**:
  ```bash
  ./build/tests/matching_engine_tests.exe --gtest_filter="PriceTimePriorityTest.*"
  ```
- **SPSC Queue Concurrency & Thread-Safety Tests**:
  ```bash
  ./build/tests/matching_engine_tests.exe --gtest_filter="SPSCQueueTest.*"
  ```
- **Memory Pool & Allocation Limits Tests**:
  ```bash
  ./build/tests/matching_engine_tests.exe --gtest_filter="MemoryPoolTest.*:EnginePoolLimitsTest.*"
  ```
- **Crossing Orders & Spread Invariant Tests**:
  ```bash
  ./build/tests/matching_engine_tests.exe --gtest_filter="CrossingOrdersTest.*"
  ```
- **Partial Fills & Queue Placement Tests**:
  ```bash
  ./build/tests/matching_engine_tests.exe --gtest_filter="PartialFillsAndQueueTest.*"
  ```

---

## Test Suite Coverage Summary

| Test Suite | Tests | Description |
| :--- | :---: | :--- |
| **`PriceTimePriorityTest`** | 6 | Best-price matching (bids/asks), FIFO arrival queue execution, passive price execution guarantee, multi-level sweeps. |
| **`PartialFillsAndQueueTest`**| 5 | Passive order queue head retention, aggressor unexecuted volume resting, quantity reduction priority retention, quantity increase priority forfeiture. |
| **`CrossingOrdersTest`** | 6 | Overlapping spread crossing, post-trade spread invariant ($\text{best\_bid} < \text{best\_ask}$), Post-Only rejection, IOC residual cancellation, FOK all-or-none. |
| **`SPSCQueueTest`** | 5 | Lock-free thread safety, 500,000-message concurrent producer-consumer stress test, ring buffer wrap-around, buffer saturation, 128-byte cache-line padding. |
| **`MemoryPoolTest`** | 6 | Fixed-capacity limit exhaustion (`nullptr` on $(N+1)$), intrusive free-list recycling, reverse & alternating deallocations, 1,000-cycle zero-leak churn. |
| **`EnginePoolLimitsTest`** | 4 | Constrained book exhaustion (`ReasonCode::ORDER_POOL_EXHAUSTED`), slot recovery on order cancel and fill, duplicate cancel safety. |
| **`HighVolumeLeakTest`** | 1 | 10,000 continuous order add and cancel cycles verifying full pool recovery and zero memory leakage. |
| **Total** | **33** | **100% Pass Rate** |

---

## Performance Considerations

- **Cache Locality**: [`Order`](src/matching/order.h) and [`Level`](src/matching/level.h) structs are aligned to 64 bytes (`alignas(64)`), matching x86-64 cache lines to minimize cache line splitting and L1 data cache misses.
- **False Sharing Prevention**: Producer and consumer cursors in [`SPSCQueue`](src/core/scsc_queue.h) are separated by 128 bytes (`alignas(128)`), eliminating cross-core cache invalidation ping-pong.
- **Fixed-Point Arithmetic**: Prices are scaled using fixed-point representation (`PRICE_SCALE = 10,000`, 4 decimal places) using 64-bit signed integers (`std::int64_t`), avoiding floating-point rounding errors and non-deterministic FPU latency.
- **Branch Prediction**: Critical cold-path error branches (pool exhaustion, full queues) utilize C++20 `[[unlikely]]` attributes to prioritize instructions in CPU instruction caches.