#include "orderbook.h"

#include <cmath>
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

constexpr double kPriceTolerance = 0.0001;

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

std::optional<Quote> MarketOrderbook::best_bid(const std::map<std::string, std::string>& levels) {
    if (levels.empty()) {
        return std::nullopt;
    }

    const auto best = levels.rbegin();
    return Quote{
        .price = parse_fixed_point(best->first),
        .size = parse_fixed_point(best->second),
    };
}

std::optional<Quote> MarketOrderbook::best_ask(const std::map<std::string, std::string>& levels) {
    if (levels.empty()) {
        return std::nullopt;
    }

    const auto best = levels.begin();
    return Quote{
        .price = parse_fixed_point(best->first),
        .size = parse_fixed_point(best->second),
    };
}

std::optional<double> MarketOrderbook::size_at_price(
    const std::map<std::string, std::string>& levels,
    double price) {
    for (const auto& [level_price, level_size] : levels) {
        if (std::abs(parse_fixed_point(level_price) - price) <= kPriceTolerance) {
            return parse_fixed_point(level_size);
        }
    }

    return std::nullopt;
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

bool MarketOrderbook::has_liquidity() const {
    return !yes_levels_.empty() || !no_levels_.empty();
}

std::optional<Quote> MarketOrderbook::best_yes_bid() const {
    return best_bid(yes_levels_);
}

std::optional<Quote> MarketOrderbook::best_yes_ask() const {
    // Subscriptions use use_yes_price=true, so no_levels are YES asks on the yes-leg scale.
    return best_ask(no_levels_);
}

std::optional<Quote> MarketOrderbook::best_no_bid() const {
    const auto yes_ask = best_yes_ask();
    if (!yes_ask) {
        return std::nullopt;
    }

    return Quote{
        .price = 1.0 - yes_ask->price,
        .size = yes_ask->size,
    };
}

std::optional<Quote> MarketOrderbook::best_no_ask() const {
    const auto yes_bid = best_yes_bid();
    if (!yes_bid) {
        return std::nullopt;
    }

    return Quote{
        .price = 1.0 - yes_bid->price,
        .size = yes_bid->size,
    };
}

std::optional<double> MarketOrderbook::implied_yes_prob() const {
    const auto ask = best_yes_ask();
    if (!ask) {
        return std::nullopt;
    }

    return ask->price;
}

std::optional<double> MarketOrderbook::implied_no_prob() const {
    const auto ask = best_no_ask();
    if (!ask) {
        return std::nullopt;
    }

    return ask->price;
}

std::optional<double> MarketOrderbook::mid_yes_prob() const {
    const auto bid = best_yes_bid();
    const auto ask = best_yes_ask();
    if (!bid || !ask) {
        return std::nullopt;
    }

    return (bid->price + ask->price) / 2.0;
}

std::optional<double> MarketOrderbook::executable_size_at(const std::string& side, double price) const {
    if (side == "yes") {
        return size_at_price(yes_levels_, price);
    }
    if (side == "no") {
        return size_at_price(no_levels_, price);
    }

    return std::nullopt;
}

void MarketOrderbook::print_quote(std::ostream& out) const {
    out << market_ticker_;

    const auto yes_bid = best_yes_bid();
    const auto yes_ask = best_yes_ask();

    out << std::fixed << std::setprecision(4);
    if (yes_bid) {
        out << " | yes bid $" << yes_bid->price << " x " << yes_bid->size;
    } else {
        out << " | yes bid n/a";
    }

    if (yes_ask) {
        out << " | yes ask $" << yes_ask->price << " x " << yes_ask->size;
    } else {
        out << " | yes ask n/a";
    }

    if (const auto mid = mid_yes_prob()) {
        out << " | mid $" << *mid;
    }

    out << "\n";
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
