### C++ OrderBook implementaion

This is my own implementation of an orderBook that also has a matching engine within to learn CPP and better understand how orderbooks work.
I had previously built a mini orderBook in python but I wanted to try something new and c++ was a lot harder and more challenging

## Features

- Buy and sell limit orders
- Price-time priority
- Full and partial fills
- Matching across multiple price levels
- Order cancellation
- Quantity modification
- Decreasing quantity preserves priority
- Increasing quantity loses priority
- Duplicate order ID rejection
- Integer-based prices using `std::int64_t`
- Best bid and best ask queries



## Matching Rules

- The highest bid and lowest ask recieve price priority
- Orders at the same price execute in a first in first out way. A sort of time priority I added.
- Trades execute at the resting order's price. EX: if a sell goes in for 1000 and the best bid is at 1200 then the it's sold at 1200.
- A cancelled order ID cannod be reused and all order ID's are unique.
- Prices and quantities must be greater than 0



## Project Structure

- `include/orderBook.h` — public types and `OrderBook` declaration
- `orderBook.cpp` — matching-engine implementation
- `orderBook_test.cpp` — deterministic and randomized tests
- `orderBook_benchmark.cpp` — cancellation benchmark



## Order storage

Orders are currently stored within a map in c++. The map is sorted ascending for sells and desciding for buys based on their price and maps to a std::list of orders at that price level. 

## Build Requirements

- A compiler supporting C++20
- Clang or GCC



## Running the Tests

```bash
clang++ -std=c++20 -Wall -Wextra \
  orderBook_test.cpp -o orderBook_test
./orderBook_test
```



## Performance Benchmarks



### Cancellation Baseline

This baseline was measured before further cancellation optimizations.

- Orders: 500,000
- Repetitions: 5
- Cancellation order: shuffled using a fixed seed
- Build mode: `-O3 -DNDEBUG`
- Metric: nanoseconds per cancellation


| Run        | Time           |
| ---------- | -------------- |
| 1          | 498.752 ns     |
| 2          | 549.798 ns     |
| 3          | 493.928 ns     |
| 4          | 486.700 ns     |
| 5          | 485.525 ns     |
| **Median** | **493.928 ns** |


Results were measured on macOS and may differ across machines.

### Running the Benchmark

```bash
clang++ -std=c++20 -O3 -DNDEBUG \
  orderBook_benchmark.cpp orderBook.cpp \
  -o orderBook_benchmark

./orderBook_benchmark
```

