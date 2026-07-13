#pragma once

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "orderbook.h"

enum class BookUpdateResult {
    Ignored,
    Updated,
    GapDetected,
};

class OrderbookSync {
public:
    void reset();
    void set_subscription_id(int sid);
    int subscription_id() const;

    BookUpdateResult handle_message(
        const nlohmann::json& message,
        MarketOrderbook& book);

    std::optional<nlohmann::json> make_resnapshot_request(
        const std::string& market_ticker,
        int command_id) const;

private:
    struct MarketState {
        int last_seq = -1;
        bool has_snapshot = false;
    };

    int subscription_id_ = -1;
    std::map<std::string, MarketState> markets_;

    static std::optional<int> read_seq(const nlohmann::json& message);
};
