#include "arb_detector.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <spdlog/spdlog.h>

namespace {

constexpr double kLogEdgeDelta = 0.001;
constexpr double kLogSizeDelta = 1.0;

std::string market_label(const MarketInfo& market) {
    if (!market.yes_sub_title.empty()) {
        return market.yes_sub_title;
    }
    if (!market.title.empty()) {
        return market.title;
    }
    return market.ticker;
}

bool approximately_equal(double lhs, double rhs, double tolerance) {
    return std::abs(lhs - rhs) <= tolerance;
}

}  // namespace

std::string arb_opportunity_key(const ArbOpportunity& opportunity) {
    return opportunity.rule + "|" + opportunity.match_slug + "|" + opportunity.event_ticker;
}

ArbDetector::ArbDetector(double min_edge) : min_edge_(min_edge) {}

std::vector<ArbOpportunity> ArbDetector::scan(
    const MarketRegistry& registry,
    const std::map<std::string, MarketOrderbook>& books) const {
    std::vector<ArbOpportunity> opportunities;

    for (const auto& match : registry.matches()) {
        for (const auto& event_group : match.event_groups) {
            if (!event_group.mutually_exclusive || event_group.markets.size() < 2) {
                continue;
            }

            if (const auto opportunity = scan_mutually_exclusive_group(match, event_group, books)) {
                opportunities.push_back(*opportunity);
            }
        }
    }

    return opportunities;
}

std::optional<ArbOpportunity> ArbDetector::scan_mutually_exclusive_group(
    const MatchGroup& match,
    const MarketEventGroup& event_group,
    const std::map<std::string, MarketOrderbook>& books) const {
    ArbOpportunity opportunity{
        .rule = "mutually_exclusive",
        .match_slug = match.match_slug,
        .match_title = match.title,
        .event_ticker = event_group.event_ticker,
        .series_ticker = event_group.series_ticker,
    };

    double sum_yes_asks = 0.0;
    double max_executable_size = std::numeric_limits<double>::max();

    for (const auto& market : event_group.markets) {
        const auto book_it = books.find(market.ticker);
        if (book_it == books.end() || !book_it->second.has_liquidity()) {
            return std::nullopt;
        }

        const auto yes_ask = book_it->second.best_yes_ask();
        if (!yes_ask) {
            return std::nullopt;
        }

        sum_yes_asks += yes_ask->price;
        max_executable_size = std::min(max_executable_size, yes_ask->size);

        opportunity.legs.push_back(ArbLeg{
            .ticker = market.ticker,
            .label = market_label(market),
            .yes_ask_price = yes_ask->price,
            .yes_ask_size = yes_ask->size,
        });
    }

    if (opportunity.legs.size() < 2) {
        return std::nullopt;
    }

    opportunity.sum_yes_asks = sum_yes_asks;
    opportunity.edge = 1.0 - sum_yes_asks;
    opportunity.edge_pct = opportunity.edge * 100.0;
    opportunity.max_executable_size = max_executable_size;

    if (opportunity.edge < min_edge_) {
        return std::nullopt;
    }

    return opportunity;
}

void ArbDetector::log_opportunities(const std::vector<ArbOpportunity>& opportunities) {
    for (const auto& opportunity : opportunities) {
        const std::string key = arb_opportunity_key(opportunity);
        const auto previous = last_logged_.find(key);
        if (previous != last_logged_.end()) {
            const bool unchanged_edge =
                approximately_equal(previous->second.edge, opportunity.edge, kLogEdgeDelta);
            const bool unchanged_size = approximately_equal(
                previous->second.max_executable_size,
                opportunity.max_executable_size,
                kLogSizeDelta);
            if (unchanged_edge && unchanged_size) {
                continue;
            }
        }

        last_logged_[key] = opportunity;

        std::ostringstream legs_summary;
        legs_summary << std::fixed << std::setprecision(4);
        for (std::size_t i = 0; i < opportunity.legs.size(); ++i) {
            if (i > 0) {
                legs_summary << ", ";
            }
            legs_summary << opportunity.legs[i].label << " @ $" << opportunity.legs[i].yes_ask_price
                         << " x " << opportunity.legs[i].yes_ask_size;
        }

        spdlog::info(
            "[ARB] {} | {} | {} | sum_yes_asks={:.4f} | edge={:.2f}% | max_size={:.2f} | legs: {}",
            opportunity.rule,
            opportunity.match_slug,
            opportunity.series_ticker,
            opportunity.sum_yes_asks,
            opportunity.edge_pct,
            opportunity.max_executable_size,
            legs_summary.str());
    }
}
