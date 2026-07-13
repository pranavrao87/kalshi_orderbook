#include "orderbook_sync.h"

#include <spdlog/spdlog.h>

void OrderbookSync::reset() {
    subscription_id_ = -1;
    last_seq_ = -1;
    markets_.clear();
}

void OrderbookSync::set_subscription_id(int sid) {
    subscription_id_ = sid;
}

int OrderbookSync::subscription_id() const {
    return subscription_id_;
}

std::optional<int> OrderbookSync::read_seq(const nlohmann::json& message) {
    if (!message.contains("seq") || !message["seq"].is_number_integer()) {
        return std::nullopt;
    }

    return message["seq"].get<int>();
}

bool OrderbookSync::validate_subscription_seq(
    const nlohmann::json& message,
    const std::string& type) {
    const auto seq = read_seq(message);
    if (!seq) {
        return true;
    }

    if (last_seq_ < 0) {
        last_seq_ = *seq;
        return true;
    }

    const int expected_seq = last_seq_ + 1;
    if (*seq == expected_seq) {
        last_seq_ = *seq;
        return true;
    }

    spdlog::warn(
        "subscription sequence gap: expected {}, got {} ({})",
        expected_seq,
        *seq,
        type);

    last_seq_ = *seq;

    // Snapshots are full state and safe to apply even after a gap.
    return type == "orderbook_snapshot";
}

BookUpdateResult OrderbookSync::handle_message(
    const nlohmann::json& message,
    MarketOrderbook& book) {
    const std::string type = message.value("type", "");
    if (type != "orderbook_snapshot" && type != "orderbook_delta") {
        return BookUpdateResult::Ignored;
    }

    const std::string ticker = message.at("msg").at("market_ticker").get<std::string>();
    MarketState& state = markets_[ticker];

    if (!validate_subscription_seq(message, type)) {
        state.has_snapshot = false;
        return BookUpdateResult::GapDetected;
    }

    if (type == "orderbook_snapshot") {
        book.load_snapshot(message);
        state.has_snapshot = true;
        return BookUpdateResult::Updated;
    }

    if (!state.has_snapshot) {
        spdlog::debug("delta for {} before snapshot, requesting resnapshot", ticker);
        return BookUpdateResult::GapDetected;
    }

    book.apply_delta(message);
    return BookUpdateResult::Updated;
}

std::optional<nlohmann::json> OrderbookSync::make_resnapshot_request(
    const std::string& market_ticker,
    int command_id) const {
    if (subscription_id_ < 0) {
        return std::nullopt;
    }

    return nlohmann::json{
        {"id", command_id},
        {"cmd", "update_subscription"},
        {"params",
         {
             {"sids", nlohmann::json::array({subscription_id_})},
             {"action", "get_snapshot"},
             {"market_tickers", nlohmann::json::array({market_ticker})},
         }},
    };
}
