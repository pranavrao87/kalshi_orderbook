#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "market_registry.h"
#include "orderbook.h"

struct ArbLeg {
    std::string ticker;
    std::string label;
    double yes_ask_price = 0.0;
    double yes_ask_size = 0.0;
};

struct ArbOpportunity {
    std::string rule;
    std::string match_slug;
    std::string match_title;
    std::string event_ticker;
    std::string series_ticker;
    double sum_yes_asks = 0.0;
    double edge = 0.0;
    double edge_pct = 0.0;
    double max_executable_size = 0.0;
    std::vector<ArbLeg> legs;
};

std::string arb_opportunity_key(const ArbOpportunity& opportunity);

class ArbDetector {
public:
    explicit ArbDetector(double min_edge = 0.01);

    std::vector<ArbOpportunity> scan(
        const MarketRegistry& registry,
        const std::map<std::string, MarketOrderbook>& books) const;

    void log_opportunities(const std::vector<ArbOpportunity>& opportunities);

private:
    double min_edge_;
    std::map<std::string, ArbOpportunity> last_logged_;

    std::optional<ArbOpportunity> scan_mutually_exclusive_group(
        const MatchGroup& match,
        const MarketEventGroup& event_group,
        const std::map<std::string, MarketOrderbook>& books) const;
};
