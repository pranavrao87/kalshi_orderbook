#include "orderbook.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {

double parse_fixed_point(const std::string& value) {
    return std::stod(value);
}

std::string format_fixed_point(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}

const nlohmann::json* find_side_levels(const nlohmann::json& payload, const std::string& side) {
    static const std::vector<std::string> yes_keys = {
        "yes_dollars_fp",
        "yes_dollars",
        "yes",
    };
    static const std::vector<std::string> no_keys = {
        "no_dollars_fp",
        "no_dollars",
        "no",
    };

    const auto& keys = side == "yes" ? yes_keys : no_keys;
    for (const auto& key : keys) {
        if (payload.contains(key)) {
            return &payload.at(key);
        }
    }

    return nullptr;
}

}  // namespace

void MarketOrderbook::set_levels(
    std::map<std::string, std::string>& levels,
    const nlohmann::json& side_levels) {
    levels.clear();

    if (!side_levels.is_array()) {
        return;
    }

    for (const auto& level : side_levels) {
        if (!level.is_array() || level.size() < 2) {
            continue;
        }

        const std::string price = level[0].get<std::string>();
        const std::string size = level[1].get<std::string>();
        if (parse_fixed_point(size) > 0.0) {
            levels[price] = size;
        }
    }
}

void MarketOrderbook::load_snapshot(const nlohmann::json& message) {
    const auto& payload = message.at("msg");
    market_ticker_ = payload.at("market_ticker").get<std::string>();

    yes_levels_.clear();
    no_levels_.clear();

    if (const auto* yes_levels = find_side_levels(payload, "yes")) {
        set_levels(yes_levels_, *yes_levels);
    }

    if (const auto* no_levels = find_side_levels(payload, "no")) {
        set_levels(no_levels_, *no_levels);
    }
}

void MarketOrderbook::apply_delta(const nlohmann::json& message) {
    const auto& payload = message.at("msg");
    market_ticker_ = payload.at("market_ticker").get<std::string>();

    const std::string side = payload.at("side").get<std::string>();
    const std::string price = payload.at("price_dollars").get<std::string>();
    const double delta = parse_fixed_point(payload.at("delta_fp").get<std::string>());

    auto& levels = side == "yes" ? yes_levels_ : no_levels_;
    double next_size = delta;
    const auto existing = levels.find(price);
    if (existing != levels.end()) {
        next_size += parse_fixed_point(existing->second);
    }

    if (next_size <= 0.0) {
        levels.erase(price);
    } else {
        levels[price] = format_fixed_point(next_size);
    }
}

void MarketOrderbook::print_summary(std::ostream& out) const {
    out << market_ticker_ << " orderbook\n";

    out << "  YES levels:\n";
    for (const auto& [price, size] : yes_levels_) {
        out << "    $" << price << " x " << size << "\n";
    }

    out << "  NO levels:\n";
    for (const auto& [price, size] : no_levels_) {
        out << "    $" << price << " x " << size << "\n";
    }
}
