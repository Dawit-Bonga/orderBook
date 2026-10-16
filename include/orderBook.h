#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <string>
#include <cstring>
#include <string_view>
#include <optional>
#include <map>
#include <list>


enum class orderType{
  BUY,
  SELL
};

struct Order{
  orderType side;
  int orderID{};
  int amount{};
  std::int64_t price{};
};

enum class SubmitStatus { Ok, DuplicateId, InvalidOrder };

struct Trade{
  int BuyerID;
  int SellerID;
  int amount;
  std::int64_t price;
};

struct OrderPosition {
  orderType side;
  std::int64_t price;
  std::list<Order>::iterator it;
};
struct SubmitResult {
  SubmitStatus status = SubmitStatus::Ok;
  int filled;
  int remaining;
  std::vector<Trade> trades;
};

class OrderBook{
  public:
    SubmitResult submitOrder(Order order);
    void cancelOrder(int orderId);
    void changeOrder(int orderID, int amount); // basically can increase amount they're selling or shortern but this will change their time priority
    std::optional<Order> getBestBid() const;
    std::optional<Order> getBestAsk() const;

  private:
    std::map<std::int64_t, std::list<Order>, std::greater<std::int64_t>> buys;
    std::map<std::int64_t, std::list<Order>> sells;
    std::unordered_map<int, OrderPosition> orderIdtoPosition;
    int matchAtPrice(std::list<Order> &current_list, int remaining, std::vector<Trade>&trades, bool buy, int incomingID);
    int matchOrder(
        Order &order, std::vector<Trade> &trades); // will return remaining amount in order
    std::unordered_set<int> usedIDs;
    bool isValidOrder(const Order &order);
};