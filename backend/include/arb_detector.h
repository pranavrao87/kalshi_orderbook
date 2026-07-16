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
    std::string side = "yes";
    double price = 0.0;
    double size = 0.0;
    double fee = 0.0;
    double all_in_cost = 0.0;
};

struct ArbOpportunity {
    std::string rule;
    std::string match_slug;
    std::string match_title;
    std::string event_ticker;
    std::string series_ticker;
    std::string detail;
    double basket_cost = 0.0;
    double target_cost = 0.0;
    double fees = 0.0;
    double edge_before_fees = 0.0;
    double edge = 0.0;
    double edge_pct = 0.0;
    double max_executable_size = 0.0;
    std::vector<ArbLeg> legs;
};

std::string arb_opportunity_key(const ArbOpportunity& opportunity);

class ArbDetector {
public:
    ArbDetector(double min_edge = 0.01, double taker_fee_coeff = 0.07);

    std::vector<ArbOpportunity> scan(
        const MarketRegistry& registry,
        const std::map<std::string, MarketOrderbook>& books) const;

    void log_opportunities(const std::vector<ArbOpportunity>& opportunities);

private:
    double min_edge_;
    double taker_fee_coeff_;
    mutable std::map<std::string, ArbOpportunity> last_logged_;

    ArbLeg make_yes_leg(const MarketInfo& market, const Quote& quote) const;
    ArbLeg make_no_leg(const MarketInfo& market, const Quote& quote) const;

    std::optional<ArbOpportunity> scan_mutually_exclusive_group(
        const MatchGroup& match,
        const MarketEventGroup& event_group,
        const std::map<std::string, MarketOrderbook>& books) const;

    void scan_yes_no_parity(
        const MatchGroup& match,
        const std::map<std::string, MarketOrderbook>& books,
        std::vector<ArbOpportunity>& out) const;

    void scan_score_baskets(
        const MatchGroup& match,
        const std::map<std::string, MarketOrderbook>& books,
        std::vector<ArbOpportunity>& out) const;
};
