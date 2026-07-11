#pragma once

#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

struct Quote {
    double price = 0.0;
    double size = 0.0;
};

class MarketOrderbook {
public:
    void load_snapshot(const nlohmann::json& message);
    void apply_delta(const nlohmann::json& message);
    void print_summary(std::ostream& out) const;
    void print_quote(std::ostream& out) const;

    const std::string& market_ticker() const { return market_ticker_; }
    bool has_liquidity() const;

    std::optional<Quote> best_yes_bid() const;
    std::optional<Quote> best_yes_ask() const;
    std::optional<Quote> best_no_bid() const;
    std::optional<Quote> best_no_ask() const;

    std::optional<double> implied_yes_prob() const;
    std::optional<double> implied_no_prob() const;
    std::optional<double> mid_yes_prob() const;

    std::optional<double> executable_size_at(const std::string& side, double price) const;

private:
    std::string market_ticker_;
    std::map<std::string, std::string> yes_levels_;
    std::map<std::string, std::string> no_levels_;

    static void set_levels(
        std::map<std::string, std::string>& levels,
        const nlohmann::json& side_levels);

    static std::optional<Quote> best_bid(const std::map<std::string, std::string>& levels);
    static std::optional<Quote> best_ask(const std::map<std::string, std::string>& levels);
    static std::optional<double> size_at_price(
        const std::map<std::string, std::string>& levels,
        double price);
};
