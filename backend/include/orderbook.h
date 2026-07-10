#pragma once

#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

class MarketOrderbook {
public:
    void load_snapshot(const nlohmann::json& message);
    void apply_delta(const nlohmann::json& message);
    void print_summary(std::ostream& out) const;

    const std::string& market_ticker() const { return market_ticker_; }

private:
    std::string market_ticker_;
    std::map<std::string, std::string> yes_levels_;
    std::map<std::string, std::string> no_levels_;

    static void set_levels(
        std::map<std::string, std::string>& levels,
        const nlohmann::json& side_levels);
};
