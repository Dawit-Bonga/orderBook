#include "orderBook.cpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
  }
}

Order makeOrder(orderType side, int id, int amount, double price) {
  return Order{side, id, amount, price};
}

void expectTrade(const Trade& trade, int buyer, int seller, int amount, double price,
                 const std::string& message) {
  expect(trade.BuyerID == buyer, message + " buyer");
  expect(trade.SellerID == seller, message + " seller");
  expect(trade.amount == amount, message + " amount");
  expect(trade.price == price, message + " price");
}

void restingOrderDoesNotTrade() {
  OrderBook book;
  SubmitResult sell = book.submitOrder(makeOrder(orderType::SELL, 1, 5, 10));
  expect(sell.filled == 0, "resting sell filled");
  expect(sell.remaining == 5, "resting sell remaining");
  expect(sell.trades.empty(), "resting sell trades");

  SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 2, 5, 9));
  expect(buy.filled == 0, "non-crossing buy filled");
  expect(buy.remaining == 5, "non-crossing buy remaining");
  expect(buy.trades.empty(), "non-crossing buy trades");
}

void buyFullyFillsOneSellAtRestingPrice() {
  OrderBook book;
  book.submitOrder(makeOrder(orderType::SELL, 1, 4, 10));

  SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 2, 4, 12));
  expect(buy.filled == 4, "full cross filled");
  expect(buy.remaining == 0, "full cross remaining");
  expect(buy.trades.size() == 1, "full cross trade count");
  if (buy.trades.size() == 1) {
    expectTrade(buy.trades[0], 2, 1, 4, 10, "full cross");
  }
}

void buyWalksCheapestSellsFirst() {
  OrderBook book;
  book.submitOrder(makeOrder(orderType::SELL, 1, 4, 9));
  book.submitOrder(makeOrder(orderType::SELL, 2, 4, 10));

  SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 3, 6, 10));
  expect(buy.filled == 6, "multi-level buy filled");
  expect(buy.remaining == 0, "multi-level buy remaining");
  expect(buy.trades.size() == 2, "multi-level buy trade count");
  if (buy.trades.size() == 2) {
    expectTrade(buy.trades[0], 3, 1, 4, 9, "cheapest sell");
    expectTrade(buy.trades[1], 3, 2, 2, 10, "next sell");
  }
}

void sellWalksHighestBidsFirst() {
  OrderBook book;
  book.submitOrder(makeOrder(orderType::BUY, 1, 3, 12));
  book.submitOrder(makeOrder(orderType::BUY, 2, 3, 11));

  SubmitResult sell = book.submitOrder(makeOrder(orderType::SELL, 3, 5, 11));
  expect(sell.filled == 5, "multi-level sell filled");
  expect(sell.remaining == 0, "multi-level sell remaining");
  expect(sell.trades.size() == 2, "multi-level sell trade count");
  if (sell.trades.size() == 2) {
    expectTrade(sell.trades[0], 1, 3, 3, 12, "best bid");
    expectTrade(sell.trades[1], 2, 3, 2, 11, "next bid");
  }
}

void samePriceFillsOldestFirst() {
  OrderBook book;
  book.submitOrder(makeOrder(orderType::SELL, 1, 2, 10));
  book.submitOrder(makeOrder(orderType::SELL, 2, 2, 10));

  SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 3, 3, 10));
  expect(buy.filled == 3, "fifo filled");
  expect(buy.remaining == 0, "fifo remaining");
  expect(buy.trades.size() == 2, "fifo trade count");
  if (buy.trades.size() == 2) {
    expectTrade(buy.trades[0], 3, 1, 2, 10, "older sell");
    expectTrade(buy.trades[1], 3, 2, 1, 10, "newer sell");
  }
}

void partialFillLeavesRestingSize() {
  OrderBook book;
  book.submitOrder(makeOrder(orderType::SELL, 1, 10, 10));

  SubmitResult first = book.submitOrder(makeOrder(orderType::BUY, 2, 4, 10));
  expect(first.filled == 4, "partial aggressor filled");
  expect(first.remaining == 0, "partial aggressor remaining");
  expect(first.trades.size() == 1, "partial aggressor trade count");
  if (!first.trades.empty()) {
    expectTrade(first.trades[0], 2, 1, 4, 10, "partial aggressor");
  }

  SubmitResult second = book.submitOrder(makeOrder(orderType::BUY, 3, 4, 10));
  expect(second.filled == 4, "second bite filled");
  expect(second.remaining == 0, "second bite remaining");
  if (!second.trades.empty()) {
    expectTrade(second.trades[0], 3, 1, 4, 10, "second bite");
  }

  SubmitResult third = book.submitOrder(makeOrder(orderType::BUY, 4, 4, 10));
  expect(third.filled == 2, "leftover sell filled");
  expect(third.remaining == 2, "leftover buy rests");
  if (!third.trades.empty()) {
    expectTrade(third.trades[0], 4, 1, 2, 10, "leftover sell");
  }
}

void duplicateIdIsRejected() {
  OrderBook book;
  book.submitOrder(makeOrder(orderType::SELL, 1, 5, 10));

  SubmitResult duplicate = book.submitOrder(makeOrder(orderType::BUY, 1, 5, 10));
  expect(duplicate.filled == 0, "duplicate filled");
  expect(duplicate.remaining == 5, "duplicate remaining");
  expect(duplicate.trades.empty(), "duplicate trades");

  SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 2, 5, 10));
  expect(buy.filled == 5, "original sell still available");
  expect(buy.trades.size() == 1, "original sell trade count");
}

void cancelRemovesRestingOrder() {
  OrderBook book;
  book.submitOrder(makeOrder(orderType::SELL, 1, 5, 10));
  book.submitOrder(makeOrder(orderType::SELL, 2, 3, 10));
  book.cancelOrder(1);

  SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 3, 5, 10));
  expect(buy.filled == 3, "cancel filled");
  expect(buy.remaining == 2, "cancel remaining");
  expect(buy.trades.size() == 1, "cancel trade count");
  if (!buy.trades.empty()) {
    expectTrade(buy.trades[0], 3, 2, 3, 10, "uncancelled sell");
  }
}

void cancelUnknownIdDoesNothing() {
  OrderBook book;
  book.submitOrder(makeOrder(orderType::BUY, 1, 2, 8));
  book.cancelOrder(99);

  SubmitResult sell = book.submitOrder(makeOrder(orderType::SELL, 2, 2, 8));
  expect(sell.filled == 2, "unknown cancel filled");
  expect(sell.remaining == 0, "unknown cancel remaining");
  if (!sell.trades.empty()) {
    expectTrade(sell.trades[0], 1, 2, 2, 8, "unknown cancel");
  }
}

void cancelledIdCannotBeReused() {
  OrderBook book;
  book.submitOrder(makeOrder(orderType::SELL, 1, 4, 10));
  book.cancelOrder(1);

  SubmitResult reused = book.submitOrder(makeOrder(orderType::SELL, 1, 4, 10));
  expect(reused.filled == 0, "reused id filled");
  expect(reused.remaining == 4, "reused id remaining");
  expect(reused.trades.empty(), "reused id trades");
}

}  // namespace

int main() {
  restingOrderDoesNotTrade();
  buyFullyFillsOneSellAtRestingPrice();
  buyWalksCheapestSellsFirst();
  sellWalksHighestBidsFirst();
  samePriceFillsOldestFirst();
  partialFillLeavesRestingSize();
  duplicateIdIsRejected();
  cancelRemovesRestingOrder();
  cancelUnknownIdDoesNothing();
  cancelledIdCannotBeReused();

  if (g_failures != 0) {
    std::cerr << g_failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "all order book tests passed\n";
  return EXIT_SUCCESS;
}
