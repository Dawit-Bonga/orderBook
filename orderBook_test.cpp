#include "orderBook.cpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
  }
}

Order makeOrder(orderType side, int id, int amount, std::int64_t price) {
  return Order{side, id, amount, price};
}

void expectTrade(const Trade& trade, int buyer, int seller, int amount, std::int64_t price,
                 const std::string& message) {
  expect(trade.BuyerID == buyer, message + " buyer");
  expect(trade.SellerID == seller, message + " seller");
  expect(trade.amount == amount, message + " amount");
  expect(trade.price == price, message + " price");
}

void expectTrades(const std::vector<Trade>& trades, const std::vector<Trade>& expected,
                  const std::string& message) {
  expect(trades.size() == expected.size(), message + " trade count");
  if (trades.size() != expected.size()) {
    return;
  }
  for (std::size_t i = 0; i < trades.size(); ++i) {
    const Trade& want = expected[i];
    expectTrade(trades[i], want.BuyerID, want.SellerID, want.amount, want.price,
                message + " #" + std::to_string(i));
  }
}

void expectTop(const std::optional<Order>& top, orderType side, int id, int amount,
               std::int64_t price, const std::string& message) {
  expect(top.has_value(), message + " exists");
  if (!top) {
    return;
  }
  expect(top->side == side, message + " side");
  expect(top->orderID == id, message + " id");
  expect(top->amount == amount, message + " amount");
  expect(top->price == price, message + " price");
}

void expectOk(const SubmitResult& result, int filled, int remaining, const std::string& message) {
  expect(result.status == SubmitStatus::Ok, message + " status");
  expect(result.filled == filled, message + " filled");
  expect(result.remaining == remaining, message + " remaining");
}

void expectRejected(const SubmitResult& result, SubmitStatus status, int amount,
                    const std::string& message) {
  expect(result.status == status, message + " status");
  expect(result.filled == 0, message + " filled");
  expect(result.remaining == amount, message + " remaining");
  expect(result.trades.empty(), message + " trades");
}

// Unmatched buy rests, and the best bid is the highest price, not the first order.
void basicOrderInsertion() {
  OrderBook book;
  expect(!book.getBestBid().has_value(), "empty book bid");
  expect(!book.getBestAsk().has_value(), "empty book ask");

  SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 1, 25, 100));
  expectOk(buy, 0, 25, "resting buy");
  expect(buy.trades.empty(), "resting buy trades");
  expectTop(book.getBestBid(), orderType::BUY, 1, 25, 100, "inserted bid");
  expect(!book.getBestAsk().has_value(), "insert leaves ask empty");

  book.submitOrder(makeOrder(orderType::BUY, 2, 10, 90));
  expectTop(book.getBestBid(), orderType::BUY, 1, 25, 100, "lower bid stays behind");

  book.submitOrder(makeOrder(orderType::BUY, 3, 4, 110));
  expectTop(book.getBestBid(), orderType::BUY, 3, 4, 110, "higher bid becomes best");
}

// A cross trades at the resting order's price, on either side of the book.
void simpleMatching() {
  {
    OrderBook book;
    book.submitOrder(makeOrder(orderType::BUY, 1, 10, 100));

    SubmitResult sell = book.submitOrder(makeOrder(orderType::SELL, 2, 4, 99));
    expectOk(sell, 4, 0, "sell into bid");
    expectTrades(sell.trades, {Trade{1, 2, 4, 100}}, "sell into bid");
    expectTop(book.getBestBid(), orderType::BUY, 1, 6, 100, "resting bid after cross");
    expect(!book.getBestAsk().has_value(), "aggressor sell does not rest");
  }
  {
    OrderBook book;
    book.submitOrder(makeOrder(orderType::SELL, 1, 10, 99));

    SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 2, 4, 100));
    expectOk(buy, 4, 0, "buy into ask");
    expectTrades(buy.trades, {Trade{2, 1, 4, 99}}, "buy into ask");
    expectTop(book.getBestAsk(), orderType::SELL, 1, 6, 99, "resting ask after cross");
    expect(!book.getBestBid().has_value(), "aggressor buy does not rest");
  }
  {
    OrderBook book;
    book.submitOrder(makeOrder(orderType::BUY, 1, 5, 100));
    SubmitResult sell = book.submitOrder(makeOrder(orderType::SELL, 2, 5, 101));
    expectOk(sell, 0, 5, "non-crossing sell");
    expect(sell.trades.empty(), "non-crossing sell trades");
    expectTop(book.getBestBid(), orderType::BUY, 1, 5, 100, "untouched bid");
    expectTop(book.getBestAsk(), orderType::SELL, 2, 5, 101, "resting ask");
  }
}

// A 40-share buy against a 100-share sell leaves 60 shares at that price.
void partialFill() {
  OrderBook book;
  book.submitOrder(makeOrder(orderType::SELL, 1, 100, 100));

  SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 2, 40, 100));
  expectOk(buy, 40, 0, "partial buy");
  expectTrades(buy.trades, {Trade{2, 1, 40, 100}}, "partial buy");
  expectTop(book.getBestAsk(), orderType::SELL, 1, 60, 100, "60 shares remain");
  expect(!book.getBestBid().has_value(), "partial buy does not rest");

  SubmitResult sweep = book.submitOrder(makeOrder(orderType::BUY, 3, 80, 100));
  expectOk(sweep, 60, 20, "sweep remainder");
  expectTrades(sweep.trades, {Trade{3, 1, 60, 100}}, "sweep remainder");
  expect(!book.getBestAsk().has_value(), "ask level cleared");
  expectTop(book.getBestBid(), orderType::BUY, 3, 20, 100, "leftover buy rests");
}

// Price-time: a buy walks asks from cheapest upward, a sell walks bids from richest downward.
void multiplePriceLevels() {
  {
    OrderBook book;
    book.submitOrder(makeOrder(orderType::SELL, 1, 9, 102));
    book.submitOrder(makeOrder(orderType::SELL, 2, 5, 100));
    book.submitOrder(makeOrder(orderType::SELL, 3, 7, 101));

    SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 4, 20, 102));
    expectOk(buy, 20, 0, "buy walks asks");
    expectTrades(buy.trades,
                 {Trade{4, 2, 5, 100}, Trade{4, 3, 7, 101}, Trade{4, 1, 8, 102}},
                 "buy walks asks");
    expectTop(book.getBestAsk(), orderType::SELL, 1, 1, 102, "last ask remainder");
    expect(!book.getBestBid().has_value(), "walking buy fully fills");
  }
  {
    OrderBook book;
    book.submitOrder(makeOrder(orderType::BUY, 1, 9, 100));
    book.submitOrder(makeOrder(orderType::BUY, 2, 5, 102));
    book.submitOrder(makeOrder(orderType::BUY, 3, 7, 101));

    SubmitResult sell = book.submitOrder(makeOrder(orderType::SELL, 4, 20, 100));
    expectOk(sell, 20, 0, "sell walks bids");
    expectTrades(sell.trades,
                 {Trade{2, 4, 5, 102}, Trade{3, 4, 7, 101}, Trade{1, 4, 8, 100}},
                 "sell walks bids");
    expectTop(book.getBestBid(), orderType::BUY, 1, 1, 100, "last bid remainder");
    expect(!book.getBestAsk().has_value(), "walking sell fully fills");
  }
}

// At one price, the earlier order fills before the later one.
void fifoPriority() {
  {
    OrderBook book;
    book.submitOrder(makeOrder(orderType::SELL, 1, 10, 100));
    book.submitOrder(makeOrder(orderType::SELL, 2, 10, 100));

    SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 3, 12, 100));
    expectOk(buy, 12, 0, "fifo sell");
    expectTrades(buy.trades, {Trade{3, 1, 10, 100}, Trade{3, 2, 2, 100}}, "fifo sell");
    expectTop(book.getBestAsk(), orderType::SELL, 2, 8, 100, "newer sell remains");
  }
  {
    OrderBook book;
    book.submitOrder(makeOrder(orderType::BUY, 1, 10, 100));
    book.submitOrder(makeOrder(orderType::BUY, 2, 10, 100));

    SubmitResult sell = book.submitOrder(makeOrder(orderType::SELL, 3, 12, 100));
    expectOk(sell, 12, 0, "fifo buy");
    expectTrades(sell.trades, {Trade{1, 3, 10, 100}, Trade{2, 3, 2, 100}}, "fifo buy");
    expectTop(book.getBestBid(), orderType::BUY, 2, 8, 100, "newer buy remains");
  }
}

// A cancelled order is gone from the book and cannot be matched or reused.
void cancellation() {
  OrderBook book;
  book.submitOrder(makeOrder(orderType::SELL, 1, 5, 100));
  book.submitOrder(makeOrder(orderType::SELL, 2, 3, 100));
  book.submitOrder(makeOrder(orderType::SELL, 3, 8, 110));
  book.cancelOrder(1);
  book.cancelOrder(99);

  expectTop(book.getBestAsk(), orderType::SELL, 2, 3, 100, "next order after cancel");

  SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 4, 5, 100));
  expectOk(buy, 3, 2, "cancel cannot match");
  expectTrades(buy.trades, {Trade{4, 2, 3, 100}}, "cancel cannot match");
  expectTop(book.getBestBid(), orderType::BUY, 4, 2, 100, "uncancelled level skipped");
  expectTop(book.getBestAsk(), orderType::SELL, 3, 8, 110, "higher ask untouched");

  book.cancelOrder(3);
  expect(!book.getBestAsk().has_value(), "last ask cancelled");

  SubmitResult reused = book.submitOrder(makeOrder(orderType::SELL, 1, 4, 100));
  expectRejected(reused, SubmitStatus::DuplicateId, 4, "cancelled id");
  expectTop(book.getBestBid(), orderType::BUY, 4, 2, 100, "rejected reuse leaves bid");
}

// Decreasing size keeps time priority. Increasing size moves the order behind the level.
void quantityModification() {
  {
    OrderBook book;
    book.submitOrder(makeOrder(orderType::SELL, 1, 10, 100));
    book.submitOrder(makeOrder(orderType::SELL, 2, 10, 100));
    book.changeOrder(1, 4);

    SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 3, 6, 100));
    expectOk(buy, 6, 0, "decrease keeps priority");
    expectTrades(buy.trades, {Trade{3, 1, 4, 100}, Trade{3, 2, 2, 100}}, "decrease keeps priority");
    expectTop(book.getBestAsk(), orderType::SELL, 2, 8, 100, "decreased order filled first");
  }
  {
    OrderBook book;
    book.submitOrder(makeOrder(orderType::SELL, 1, 10, 100));
    book.submitOrder(makeOrder(orderType::SELL, 2, 10, 100));
    book.changeOrder(1, 15);

    SubmitResult buy = book.submitOrder(makeOrder(orderType::BUY, 3, 10, 100));
    expectOk(buy, 10, 0, "increase loses priority");
    expectTrades(buy.trades, {Trade{3, 2, 10, 100}}, "increase loses priority");
    expectTop(book.getBestAsk(), orderType::SELL, 1, 15, 100, "increased order is now last");
  }
  {
    OrderBook book;
    book.submitOrder(makeOrder(orderType::BUY, 1, 10, 100));
    book.submitOrder(makeOrder(orderType::BUY, 2, 10, 100));
    book.changeOrder(1, 6);

    SubmitResult sell = book.submitOrder(makeOrder(orderType::SELL, 3, 6, 100));
    expectOk(sell, 6, 0, "bid decrease keeps priority");
    expectTrades(sell.trades, {Trade{1, 3, 6, 100}}, "bid decrease keeps priority");
    expectTop(book.getBestBid(), orderType::BUY, 2, 10, 100, "older bid filled");
  }
  {
    OrderBook book;
    book.submitOrder(makeOrder(orderType::BUY, 1, 10, 100));
    book.submitOrder(makeOrder(orderType::BUY, 2, 10, 100));
    book.changeOrder(1, 15);

    SubmitResult grown = book.submitOrder(makeOrder(orderType::SELL, 3, 10, 100));
    expectOk(grown, 10, 0, "bid increase loses priority");
    expectTrades(grown.trades, {Trade{2, 3, 10, 100}}, "bid increase loses priority");
    expectTop(book.getBestBid(), orderType::BUY, 1, 15, 100, "increased bid is now last");
  }
  {
    OrderBook book;
    book.submitOrder(makeOrder(orderType::BUY, 1, 10, 100));
    book.submitOrder(makeOrder(orderType::BUY, 2, 10, 100));
    book.changeOrder(1, 0);

    SubmitResult sell = book.submitOrder(makeOrder(orderType::SELL, 3, 10, 100));
    expectOk(sell, 10, 0, "zero change cancels");
    expectTrades(sell.trades, {Trade{2, 3, 10, 100}}, "zero change cancels");
    expect(!book.getBestBid().has_value(), "zeroed bid is gone");
  }
}

// Rejected submits do not rest, match, or consume a fresh id. A used id stays used.
void invalidInputs() {
  OrderBook book;
  book.submitOrder(makeOrder(orderType::SELL, 7, 5, 100));

  SubmitResult zero = book.submitOrder(makeOrder(orderType::BUY, 1, 0, 100));
  expectRejected(zero, SubmitStatus::InvalidOrder, 0, "zero amount");

  SubmitResult negative = book.submitOrder(makeOrder(orderType::BUY, 1, -5, 100));
  expectRejected(negative, SubmitStatus::InvalidOrder, -5, "negative amount");

  SubmitResult badPrice = book.submitOrder(makeOrder(orderType::BUY, 2, 5, -1));
  expectRejected(badPrice, SubmitStatus::InvalidOrder, 5, "negative price");

  SubmitResult duplicate = book.submitOrder(makeOrder(orderType::BUY, 7, 5, 100));
  expectRejected(duplicate, SubmitStatus::DuplicateId, 5, "duplicate id");

  SubmitResult duplicateInvalid = book.submitOrder(makeOrder(orderType::BUY, 7, 0, 100));
  expectRejected(duplicateInvalid, SubmitStatus::DuplicateId, 0, "duplicate beats invalid");

  expectTop(book.getBestAsk(), orderType::SELL, 7, 5, 100, "rejects leave the book");
  expect(!book.getBestBid().has_value(), "rejects add no bid");

  SubmitResult reusedFreshId = book.submitOrder(makeOrder(orderType::BUY, 1, 3, 90));
  expectOk(reusedFreshId, 0, 3, "invalid id can be reused");
  expectTop(book.getBestBid(), orderType::BUY, 1, 3, 90, "reused fresh id rests");

  SubmitResult zeroPrice = book.submitOrder(makeOrder(orderType::SELL, 8, 2, 0));
  expectRejected(zeroPrice, SubmitStatus::InvalidOrder, 2, "zero price");
  expectTop(book.getBestAsk(), orderType::SELL, 7, 5, 100, "zero price does not rest");
  expectTop(book.getBestBid(), orderType::BUY, 1, 3, 90, "zero price leaves bid");
}

// Independent book used to check long random sequences. Price-time rules are
// applied by scanning orders, so a bug copied from the engine's containers
// does not hide inside the same map and list layout.
class ShadowBook {
 public:
  SubmitResult submit(const Order& order) {
    if (used_.contains(order.orderID)) {
      return SubmitResult{SubmitStatus::DuplicateId, 0, order.amount, {}};
    }
    if (order.amount <= 0 || order.price <= 0) {
      return SubmitResult{SubmitStatus::InvalidOrder, 0, order.amount, {}};
    }

    used_.insert(order.orderID);
    int remaining = order.amount;
    std::vector<Trade> trades;
    const orderType restingSide =
        order.side == orderType::BUY ? orderType::SELL : orderType::BUY;

    while (remaining > 0) {
      const int index = findMatch(restingSide, order.price);
      if (index < 0) {
        break;
      }
      OpenOrder& resting = orders_[static_cast<std::size_t>(index)];
      const int fill = std::min(remaining, resting.amount);
      const int buyer = order.side == orderType::BUY ? order.orderID : resting.id;
      const int seller = order.side == orderType::BUY ? resting.id : order.orderID;
      trades.push_back(Trade{buyer, seller, fill, resting.price});
      remaining -= fill;
      resting.amount -= fill;
      if (resting.amount == 0) {
        orders_.erase(orders_.begin() + index);
      }
    }

    if (remaining > 0) {
      orders_.push_back(OpenOrder{order.side, order.orderID, remaining, order.price, nextPriority_++});
    }
    return SubmitResult{SubmitStatus::Ok, order.amount - remaining, remaining, std::move(trades)};
  }

  // Returns the size removed from the book. Zero means the id was not resting.
  int cancel(int id) {
    const int index = findId(id);
    if (index < 0) {
      return 0;
    }
    const int removed = orders_[static_cast<std::size_t>(index)].amount;
    orders_.erase(orders_.begin() + index);
    return removed;
  }

  // Returns how much size was removed without a trade. An increase returns 0
  // and the caller accounts for the added size.
  int change(int id, int amount) {
    const int index = findId(id);
    if (index < 0 || amount < 0) {
      return 0;
    }
    if (amount == 0) {
      return cancel(id);
    }

    OpenOrder& order = orders_[static_cast<std::size_t>(index)];
    const int oldAmount = order.amount;
    order.amount = amount;
    if (amount > oldAmount) {
      order.priority = nextPriority_++;
      return 0;
    }
    return oldAmount - amount;
  }

  std::optional<Order> bestBid() const { return best(orderType::BUY); }
  std::optional<Order> bestAsk() const { return best(orderType::SELL); }

  int amountOf(int id) const {
    const int index = findId(id);
    if (index < 0) {
      return -1;
    }
    return orders_[static_cast<std::size_t>(index)].amount;
  }

  int openVolume() const {
    int total = 0;
    for (const OpenOrder& order : orders_) {
      total += order.amount;
    }
    return total;
  }

  const std::unordered_set<int>& usedIds() const { return used_; }

  std::vector<int> liveIds() const {
    std::vector<int> ids;
    ids.reserve(orders_.size());
    for (const OpenOrder& order : orders_) {
      ids.push_back(order.id);
    }
    return ids;
  }

 private:
  struct OpenOrder {
    orderType side;
    int id;
    int amount;
    std::int64_t price;
    int priority;
  };

  std::optional<Order> best(orderType side) const {
    const OpenOrder* found = nullptr;
    for (const OpenOrder& order : orders_) {
      if (order.side != side) {
        continue;
      }
      if (found == nullptr || better(order, *found)) {
        found = &order;
      }
    }
    if (found == nullptr) {
      return std::nullopt;
    }
    return Order{found->side, found->id, found->amount, found->price};
  }

  static bool better(const OpenOrder& candidate, const OpenOrder& current) {
    if (candidate.side == orderType::BUY) {
      return candidate.price > current.price ||
             (candidate.price == current.price && candidate.priority < current.priority);
    }
    return candidate.price < current.price ||
           (candidate.price == current.price && candidate.priority < current.priority);
  }

  int findMatch(orderType restingSide, std::int64_t incomingPrice) const {
    int best = -1;
    for (int i = 0; i < static_cast<int>(orders_.size()); ++i) {
      const OpenOrder& order = orders_[static_cast<std::size_t>(i)];
      if (order.side != restingSide) {
        continue;
      }
      if (restingSide == orderType::SELL && order.price > incomingPrice) {
        continue;
      }
      if (restingSide == orderType::BUY && order.price < incomingPrice) {
        continue;
      }
      if (best < 0 || better(order, orders_[static_cast<std::size_t>(best)])) {
        best = i;
      }
    }
    return best;
  }

  int findId(int id) const {
    for (int i = 0; i < static_cast<int>(orders_.size()); ++i) {
      if (orders_[static_cast<std::size_t>(i)].id == id) {
        return i;
      }
    }
    return -1;
  }

  std::vector<OpenOrder> orders_;
  std::unordered_set<int> used_;
  int nextPriority_ = 0;
};

std::string statusName(SubmitStatus status) {
  switch (status) {
    case SubmitStatus::Ok:
      return "Ok";
    case SubmitStatus::DuplicateId:
      return "DuplicateId";
    case SubmitStatus::InvalidOrder:
      return "InvalidOrder";
  }
  return "?";
}

bool sameTrades(const std::vector<Trade>& engine, const std::vector<Trade>& model,
                const std::string& where) {
  if (engine.size() != model.size()) {
    std::cerr << "FAIL: " << where << " trade count engine=" << engine.size()
              << " model=" << model.size() << "\n";
    ++g_failures;
    return false;
  }
  for (std::size_t i = 0; i < engine.size(); ++i) {
    const bool same = engine[i].BuyerID == model[i].BuyerID &&
                      engine[i].SellerID == model[i].SellerID &&
                      engine[i].amount == model[i].amount && engine[i].price == model[i].price;
    if (!same) {
      std::cerr << "FAIL: " << where << " trade " << i << " engine=" << engine[i].BuyerID << "/"
                << engine[i].SellerID << " x" << engine[i].amount << "@" << engine[i].price
                << " model=" << model[i].BuyerID << "/" << model[i].SellerID << " x"
                << model[i].amount << "@" << model[i].price << "\n";
      ++g_failures;
      return false;
    }
  }
  return true;
}

bool sameResult(const SubmitResult& engine, const SubmitResult& model, const std::string& where) {
  bool ok = true;
  if (engine.status != model.status || engine.filled != model.filled ||
      engine.remaining != model.remaining) {
    std::cerr << "FAIL: " << where << " result engine=" << statusName(engine.status) << " filled "
              << engine.filled << " remaining " << engine.remaining
              << " model=" << statusName(model.status) << " filled " << model.filled
              << " remaining " << model.remaining << "\n";
    ++g_failures;
    ok = false;
  }
  if (!sameTrades(engine.trades, model.trades, where)) {
    ok = false;
  }
  return ok;
}

bool sameTop(const std::optional<Order>& engine, const std::optional<Order>& model,
             const std::string& where) {
  if (!engine && !model) {
    return true;
  }
  if (!engine || !model || engine->orderID != model->orderID || engine->amount != model->amount ||
      engine->price != model->price || engine->side != model->side) {
    std::cerr << "FAIL: " << where << " top mismatch\n";
    ++g_failures;
    return false;
  }
  return true;
}

bool bookAgrees(const OrderBook& engine, const ShadowBook& model, const std::string& where) {
  bool ok = sameTop(engine.getBestBid(), model.bestBid(), where + " bid");
  ok = sameTop(engine.getBestAsk(), model.bestAsk(), where + " ask") && ok;

  const std::optional<Order> bid = engine.getBestBid();
  const std::optional<Order> ask = engine.getBestAsk();
  if (bid && ask && bid->price >= ask->price) {
    std::cerr << "FAIL: " << where << " crossed book bid " << bid->price << " ask " << ask->price
              << "\n";
    ++g_failures;
    ok = false;
  }
  return ok;
}

// Thousands of submits, cancels, and size changes. Each step must match the
// shadow book, leave the real book uncrossed, and conserve size.
void randomizedEventSequences() {
  constexpr std::uint32_t kSeed = 20261016;
  constexpr int kSteps = 5000;
  std::mt19937 rng(kSeed);
  std::uniform_int_distribution<int> rollDist(0, 99);
  std::uniform_int_distribution<int> amountDist(1, 12);
  std::uniform_int_distribution<std::int64_t> priceDist(1, 8);
  std::uniform_int_distribution<int> rareDist(0, 19);

  OrderBook engine;
  ShadowBook model;
  int nextId = 1;
  long long added = 0;
  long long removed = 0;
  long long traded = 0;

  for (int step = 0; step < kSteps; ++step) {
    const std::vector<int> live = model.liveIds();
    const int roll = rollDist(rng);
    const std::string where = "random step " + std::to_string(step);

    if (roll < 80 || live.empty()) {
      int id = nextId;
      const std::vector<int> used(model.usedIds().begin(), model.usedIds().end());
      if (!used.empty() && rareDist(rng) == 0) {
        id = used[static_cast<std::size_t>(rng() % used.size())];
      } else {
        ++nextId;
      }

      int amount = amountDist(rng);
      std::int64_t price = priceDist(rng);
      if (rareDist(rng) == 0) {
        amount = 0;
      } else if (rareDist(rng) == 0) {
        amount = -4;
      }
      if (rareDist(rng) == 0) {
        price = rareDist(rng) % 2 == 0 ? 0 : -1;
      }

      const orderType side = roll % 2 == 0 ? orderType::BUY : orderType::SELL;
      const Order order = makeOrder(side, id, amount, price);
      const SubmitResult engineResult = engine.submitOrder(order);
      const SubmitResult modelResult = model.submit(order);
      if (!sameResult(engineResult, modelResult, where + " submit")) {
        return;
      }
      if (engineResult.status == SubmitStatus::Ok) {
        added += amount;
        for (const Trade& trade : engineResult.trades) {
          traded += trade.amount;
        }
        if (engineResult.filled + engineResult.remaining != amount) {
          std::cerr << "FAIL: " << where << " filled + remaining != amount\n";
          ++g_failures;
          return;
        }
      }
    } else if (roll < 90) {
      const int id = live[static_cast<std::size_t>(rng() % live.size())];
      removed += model.cancel(id);
      engine.cancelOrder(id);
    } else {
      const int id = live[static_cast<std::size_t>(rng() % live.size())];
      const int before = model.amountOf(id);
      int newAmount = 0;
      if (roll < 94 && before > 1) {
        newAmount = 1 + static_cast<int>(rng() % static_cast<unsigned>(before - 1));
      } else if (roll < 98) {
        newAmount = before + amountDist(rng);
      }
      model.change(id, newAmount);
      engine.changeOrder(id, newAmount);
      if (newAmount > before) {
        added += static_cast<long long>(newAmount - before);
      } else {
        removed += static_cast<long long>(before - newAmount);
      }
    }

    if (!bookAgrees(engine, model, where)) {
      return;
    }

    const long long open = model.openVolume();
    // Size enters through accepts and increases. A trade removes one unit
    // from each side. Cancels and decreases remove the rest.
    if (added != 2 * traded + open + removed) {
      std::cerr << "FAIL: " << where << " size not conserved added=" << added
                << " traded=" << traded << " open=" << open << " removed=" << removed << "\n";
      ++g_failures;
      return;
    }
  }
}

}  // namespace

int main() {
  basicOrderInsertion();
  simpleMatching();
  partialFill();
  multiplePriceLevels();
  fifoPriority();
  cancellation();
  quantityModification();
  invalidInputs();
  randomizedEventSequences();

  if (g_failures != 0) {
    std::cerr << g_failures << " assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "all order book tests passed\n";
  return EXIT_SUCCESS;
}
