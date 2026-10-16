#include "include/orderBook.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

int main() {
  constexpr int orderCount = 500'000;
  constexpr int repetitions = 5;

  std::vector<int> ids(orderCount);
  std::iota(ids.begin(), ids.end(), 1);

  std::mt19937 rng(12345);
  std::shuffle(ids.begin(), ids.end(), rng);

  std::vector<double> results;

  for (int run = 0; run < repetitions; ++run) {
    OrderBook book;

    // Set up the book outside the timed region.
    for (int id = 1; id <= orderCount; ++id) {
      book.submitOrder({orderType::BUY, id, 1, static_cast<std::int64_t>(id)});
    }

    auto start = std::chrono::steady_clock::now();

    for (int id : ids) {
      book.cancelOrder(id);
    }

    auto end = std::chrono::steady_clock::now();

    if (book.getBestBid().has_value()) {
      throw std::runtime_error("Book should be empty");
    }

    double elapsedNs =
        std::chrono::duration<double, std::nano>(end - start).count();

    double nsPerCancel = elapsedNs / orderCount;

    results.push_back(nsPerCancel);

    std::cout << "Run " << run + 1 << ": " << nsPerCancel
              << " ns/cancellation\n";
  }

  std::sort(results.begin(), results.end());

  std::cout << "\nMedian: " << results[results.size() / 2]
            << " ns/cancellation\n";
}