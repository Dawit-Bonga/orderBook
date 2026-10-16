#include "include/orderBook.h"

bool OrderBook::isValidOrder(const Order &order){
  return order.amount > 0 && order.price > 0;
}

    std::optional<Order> OrderBook::getBestBid() const {
  if (buys.empty()){
    return std::nullopt;
  }

  auto bestPrice = buys.begin();

  return bestPrice->second.front();
}

std::optional<Order> OrderBook::getBestAsk() const{
  if (sells.empty()) {
    return std::nullopt;
  }

  auto bestPrice = sells.begin();

  return bestPrice->second.front();
}

SubmitResult OrderBook::submitOrder(Order order) {
  if (usedIDs.contains(order.orderID)){
    //only one allowed
    return SubmitResult{SubmitStatus::DuplicateId, 0, order.amount, {}};
  }

  if (!isValidOrder(order)){
    return SubmitResult{SubmitStatus::InvalidOrder, 0, order.amount, {}};
  }

    usedIDs.insert(order.orderID);
  
  int original = order.amount;
  std::vector<Trade> trades;
  int left = matchOrder(order, trades);
  order.amount = left;
  if (left == 0)
    return SubmitResult{SubmitStatus::Ok, original, 0, std::move(trades)};

  if (order.side == orderType::BUY){
    auto &level = buys[order.price];

    level.push_back(order);

    auto it = std::prev(level.end());
    orderIdtoPosition[order.orderID] = OrderPosition {
      order.side, order.price, it
    };

  } else if (order.side == orderType::SELL){
    auto &level = sells[order.price];

    level.push_back(order);

    auto it = std::prev(level.end());
    orderIdtoPosition[order.orderID] = OrderPosition {
      order.side, order.price, it
    };
  }

  return SubmitResult{SubmitStatus::Ok, original - left, left, std::move(trades)};
};

void OrderBook::cancelOrder(int orderId){
  auto it = orderIdtoPosition.find(orderId);
  if (it == orderIdtoPosition.end()){
    return;
  }

  auto& orderPosition = it->second;

  if (orderPosition.side == orderType::BUY){
    auto level = buys.find(orderPosition.price); 
    level->second.erase(orderPosition.it);
    if (level->second.empty()){
      buys.erase(level);
    }
  } else {
    auto level = sells.find(orderPosition.price);
    level->second.erase(orderPosition.it);
    if (level->second.empty()) {
      sells.erase(level);
    }
  }

  //later we can make it so orderID are always unique so we might keep this or have another way to check if an orderid has been used
  orderIdtoPosition.erase(orderId);
};

void OrderBook::changeOrder(int orderID, int amount){
  //okay we need to be able to change it
  // std::unordered_map<std::int64_t, std::list<Order>> side;
  auto found =  orderIdtoPosition.find(orderID);
  if (found == orderIdtoPosition.end()){
    return;
  }

  if (amount == 0){
    cancelOrder(orderID);
    return;
  } else if (amount > 0){
    return;
  }
  auto &orderPosition = found->second;

  auto it = orderPosition.it;
  int old_amount = it->amount;
  it->amount = amount;
  std::list<Order> *level = nullptr;
  if (orderPosition.side == orderType::BUY) {
    level = &buys.at(orderPosition.price);
  } else {
    level = &sells.at(orderPosition.price);
  }

  if (old_amount < amount && std::next(it) != level->end()){
    level->splice(level->end(), *level, it);
  }
}


int OrderBook::matchOrder(Order &order, std::vector<Trade> &trades) {
  int remaining = order.amount;
  
  if (order.side == orderType::BUY){
    //we need to go from cheaptest sells and up and if it's cheaper than the buy price than we add them
    for (auto it = sells.begin(); it != sells.end();){
      auto& [price, orders] = *it;
      if (price > order.price) break;
      remaining = matchAtPrice(orders, remaining, trades, true, order.orderID);

      if (orders.empty()){
        it = sells.erase(it);
      } else{
        ++it;
      }
      if (remaining == 0) break;
    }

  } else if (order.side == orderType::SELL){
    // need to find highest priced buys and go down from there
    for (auto it = buys.begin(); it != buys.end();) {
      auto& [price, orders] = *it;
      if (price < order.price) break;
      //
      remaining = matchAtPrice(orders, remaining, trades, false, order.orderID);
      if (orders.empty()) {
        it = buys.erase(it);
      } else {
        ++it;
      }
      if (remaining == 0) break;
    }
  }

  return remaining;
}

int OrderBook::matchAtPrice(std::list<Order> &current_list, int remaining, std::vector<Trade>&trades, bool buy, int incomingId) {
  auto it = current_list.begin();

  while (it != current_list.end() && remaining > 0) {
    int current {};
    std::int64_t price = it->price;
    int buyerID, sellerID;
    if (buy){
      buyerID = incomingId;
      sellerID = it->orderID;
    } else{
      buyerID = it->orderID;
      sellerID = incomingId;
    }
    if (it->amount > remaining) {
      it->amount -= remaining;
      current = remaining;
      remaining = 0;
      ++it;
    } else {
      remaining -= it->amount;
      current = it->amount;
      int orderId = it->orderID;

      it = current_list.erase(it);
      orderIdtoPosition.erase(orderId);
    }

    trades.emplace_back(buyerID, sellerID, current, price);

  }

  return remaining;
}