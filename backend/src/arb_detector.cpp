#include "arb_detector.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <regex>
#include <sstream>

#include <spdlog/spdlog.h>

#include "fees.h"

namespace {

constexpr double kLogEdgeDelta = 0.001;
constexpr double kLogSizeDelta = 1.0;

struct Scoreline {
    std::string home_code;
    std::string away_code;
    int home_goals = 0;
    int away_goals = 0;
};

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

const MarketEventGroup* find_series_group(const MatchGroup& match, const std::string& series_ticker) {
    for (const auto& group : match.event_groups) {
        if (group.series_ticker == series_ticker) {
            return &group;
        }
    }
    return nullptr;
}

std::optional<Scoreline> parse_scoreline(const std::string& outcome_key) {
    static const std::regex pattern(R"(^([A-Z]+)(\d+)([A-Z]+)(\d+)$)");
    std::smatch match;
    if (!std::regex_match(outcome_key, match, pattern)) {
        return std::nullopt;
    }

    return Scoreline{
        .home_code = match[1].str(),
        .away_code = match[3].str(),
        .home_goals = std::stoi(match[2].str()),
        .away_goals = std::stoi(match[4].str()),
    };
}

std::string score_winner_code(const Scoreline& score) {
    if (score.home_goals > score.away_goals) {
        return score.home_code;
    }
    if (score.away_goals > score.home_goals) {
        return score.away_code;
    }
    return "TIE";
}

std::optional<double> parse_total_threshold(const MarketInfo& market) {
    // KXWCTOTAL outcome keys are 1..6 meaning Over 0.5 .. Over 5.5.
    try {
        const int index = std::stoi(market.outcome_key);
        if (index >= 1 && index <= 20) {
            return static_cast<double>(index) - 0.5;
        }
    } catch (const std::exception&) {
    }

    static const std::regex over_pattern(R"(Over\s+([0-9]+(?:\.[0-9]+)?))", std::regex::icase);
    std::smatch match;
    const std::string text = market.yes_sub_title.empty() ? market.title : market.yes_sub_title;
    if (std::regex_search(text, match, over_pattern)) {
        return std::stod(match[1].str());
    }

    return std::nullopt;
}

const MarketOrderbook* find_book(
    const std::map<std::string, MarketOrderbook>& books,
    const std::string& ticker) {
    const auto it = books.find(ticker);
    if (it == books.end() || !it->second.has_liquidity()) {
        return nullptr;
    }
    return &it->second;
}

}  // namespace

std::string arb_opportunity_key(const ArbOpportunity& opportunity) {
    return opportunity.rule + "|" + opportunity.match_slug + "|" + opportunity.event_ticker + "|" +
           opportunity.detail;
}

ArbDetector::ArbDetector(double min_edge, double taker_fee_coeff)
    : min_edge_(min_edge), taker_fee_coeff_(taker_fee_coeff) {}

ArbLeg ArbDetector::make_yes_leg(const MarketInfo& market, const Quote& quote) const {
    const double fee = kalshi_taker_fee_per_contract(quote.price, taker_fee_coeff_);
    return ArbLeg{
        .ticker = market.ticker,
        .label = market_label(market),
        .side = "yes",
        .price = quote.price,
        .size = quote.size,
        .fee = fee,
        .all_in_cost = quote.price + fee,
    };
}

ArbLeg ArbDetector::make_no_leg(const MarketInfo& market, const Quote& quote) const {
    const double fee = kalshi_taker_fee_per_contract(quote.price, taker_fee_coeff_);
    return ArbLeg{
        .ticker = market.ticker,
        .label = market_label(market) + " (NO)",
        .side = "no",
        .price = quote.price,
        .size = quote.size,
        .fee = fee,
        .all_in_cost = quote.price + fee,
    };
}

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

        scan_yes_no_parity(match, books, opportunities);
        scan_score_baskets(match, books, opportunities);
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
        .detail = event_group.series_ticker,
    };

    double sum_asks = 0.0;
    double fees = 0.0;
    double max_executable_size = std::numeric_limits<double>::max();

    for (const auto& market : event_group.markets) {
        const auto* book = find_book(books, market.ticker);
        if (!book) {
            return std::nullopt;
        }

        const auto yes_ask = book->best_yes_ask();
        if (!yes_ask) {
            return std::nullopt;
        }

        const ArbLeg leg = make_yes_leg(market, *yes_ask);
        sum_asks += leg.price;
        fees += leg.fee;
        max_executable_size = std::min(max_executable_size, leg.size);
        opportunity.legs.push_back(leg);
    }

    if (opportunity.legs.size() < 2) {
        return std::nullopt;
    }

    const double basket_cost = sum_asks + fees;
    opportunity.basket_cost = basket_cost;
    opportunity.target_cost = 1.0;
    opportunity.fees = fees;
    opportunity.edge_before_fees = 1.0 - sum_asks;
    opportunity.edge = 1.0 - basket_cost;
    opportunity.edge_pct = opportunity.edge * 100.0;
    opportunity.max_executable_size = max_executable_size;

    if (opportunity.edge < min_edge_) {
        return std::nullopt;
    }

    return opportunity;
}

void ArbDetector::scan_yes_no_parity(
    const MatchGroup& match,
    const std::map<std::string, MarketOrderbook>& books,
    std::vector<ArbOpportunity>& out) const {
    for (const auto& event_group : match.event_groups) {
        for (const auto& market : event_group.markets) {
            const auto* book = find_book(books, market.ticker);
            if (!book) {
                continue;
            }

            const auto yes_ask = book->best_yes_ask();
            const auto no_ask = book->best_no_ask();
            if (!yes_ask || !no_ask) {
                continue;
            }

            const ArbLeg yes_leg = make_yes_leg(market, *yes_ask);
            const ArbLeg no_leg = make_no_leg(market, *no_ask);
            const double sum_asks = yes_leg.price + no_leg.price;
            const double fees = yes_leg.fee + no_leg.fee;
            const double basket_cost = sum_asks + fees;
            const double edge = 1.0 - basket_cost;

            if (edge < min_edge_) {
                continue;
            }

            ArbOpportunity opportunity{
                .rule = "yes_no_parity",
                .match_slug = match.match_slug,
                .match_title = match.title,
                .event_ticker = event_group.event_ticker,
                .series_ticker = event_group.series_ticker,
                .detail = market.ticker,
                .basket_cost = basket_cost,
                .target_cost = 1.0,
                .fees = fees,
                .edge_before_fees = 1.0 - sum_asks,
                .edge = edge,
                .edge_pct = edge * 100.0,
                .max_executable_size = std::min(yes_leg.size, no_leg.size),
                .legs = {yes_leg, no_leg},
            };
            out.push_back(std::move(opportunity));
        }
    }
}

void ArbDetector::scan_score_baskets(
    const MatchGroup& match,
    const std::map<std::string, MarketOrderbook>& books,
    std::vector<ArbOpportunity>& out) const {
    const auto* score_group = find_series_group(match, "KXWCSCORE");
    if (!score_group) {
        return;
    }

    struct ParsedScoreMarket {
        MarketInfo market;
        Scoreline score;
    };

    std::vector<ParsedScoreMarket> score_markets;
    score_markets.reserve(score_group->markets.size());
    for (const auto& market : score_group->markets) {
        const auto score = parse_scoreline(market.outcome_key);
        if (!score) {
            continue;
        }
        score_markets.push_back(ParsedScoreMarket{.market = market, .score = *score});
    }

    if (score_markets.empty()) {
        return;
    }

    auto emit_basket_vs_target =
        [&](const std::string& rule,
            const std::string& detail,
            const MarketEventGroup& target_group,
            const MarketInfo& target_market,
            const std::vector<ParsedScoreMarket>& basket) {
            if (basket.empty()) {
                return;
            }

            const auto* target_book = find_book(books, target_market.ticker);
            if (!target_book) {
                return;
            }

            const auto target_ask = target_book->best_yes_ask();
            if (!target_ask) {
                return;
            }

            ArbOpportunity opportunity{
                .rule = rule,
                .match_slug = match.match_slug,
                .match_title = match.title,
                .event_ticker = score_group->event_ticker,
                .series_ticker = target_group.series_ticker,
                .detail = detail,
            };

            double sum_asks = 0.0;
            double fees = 0.0;
            double max_executable_size = std::numeric_limits<double>::max();

            for (const auto& item : basket) {
                const auto* book = find_book(books, item.market.ticker);
                if (!book) {
                    return;
                }

                const auto yes_ask = book->best_yes_ask();
                if (!yes_ask) {
                    return;
                }

                const ArbLeg leg = make_yes_leg(item.market, *yes_ask);
                sum_asks += leg.price;
                fees += leg.fee;
                max_executable_size = std::min(max_executable_size, leg.size);
                opportunity.legs.push_back(leg);
            }

            // Target is the comparison benchmark (not a buy leg). Score markets are an
            // incomplete subset of the target event, so a cheaper basket is a relative-value
            // signal: buy the implying scores when they price below the richer target.
            const ArbLeg target_leg = make_yes_leg(target_market, *target_ask);
            const double basket_cost = sum_asks + fees;
            const double target_cost = target_leg.all_in_cost;
            const double edge_before_fees = target_ask->price - sum_asks;
            const double edge = target_cost - basket_cost;

            if (edge < min_edge_) {
                return;
            }

            opportunity.basket_cost = basket_cost;
            opportunity.target_cost = target_cost;
            opportunity.fees = fees;
            opportunity.edge_before_fees = edge_before_fees;
            opportunity.edge = edge;
            opportunity.edge_pct = edge * 100.0;
            opportunity.max_executable_size = max_executable_size;
            opportunity.detail += " vs " + target_leg.label + " @ $" +
                                  [&] {
                                      std::ostringstream stream;
                                      stream << std::fixed << std::setprecision(4)
                                             << target_leg.price;
                                      return stream.str();
                                  }();
            out.push_back(std::move(opportunity));
        };

    // Score basket -> moneyline outcome
    if (const auto* ml_group = find_series_group(match, "KXWCGAME")) {
        for (const auto& ml_market : ml_group->markets) {
            const std::string winner = ml_market.outcome_key;
            if (winner.empty()) {
                continue;
            }

            std::vector<ParsedScoreMarket> basket;
            for (const auto& item : score_markets) {
                if (score_winner_code(item.score) == winner) {
                    basket.push_back(item);
                }
            }

            emit_basket_vs_target(
                "score_to_ml",
                "scores=>" + winner,
                *ml_group,
                ml_market,
                basket);
        }
    }

    // Score basket -> BTTS Yes (both teams score)
    if (const auto* btts_group = find_series_group(match, "KXWCBTTS")) {
        for (const auto& btts_market : btts_group->markets) {
            std::vector<ParsedScoreMarket> basket;
            for (const auto& item : score_markets) {
                if (item.score.home_goals > 0 && item.score.away_goals > 0) {
                    basket.push_back(item);
                }
            }

            emit_basket_vs_target(
                "score_to_btts",
                "scores=>BTTS",
                *btts_group,
                btts_market,
                basket);
        }
    }

    // Score basket -> totals Over X.5
    if (const auto* total_group = find_series_group(match, "KXWCTOTAL")) {
        for (const auto& total_market : total_group->markets) {
            const auto threshold = parse_total_threshold(total_market);
            if (!threshold) {
                continue;
            }

            const int min_goals = static_cast<int>(std::floor(*threshold)) + 1;
            std::vector<ParsedScoreMarket> basket;
            for (const auto& item : score_markets) {
                if (item.score.home_goals + item.score.away_goals >= min_goals) {
                    basket.push_back(item);
                }
            }

            std::ostringstream detail;
            detail << "scores=>Over " << std::fixed << std::setprecision(1) << *threshold;
            emit_basket_vs_target(
                "score_to_total",
                detail.str(),
                *total_group,
                total_market,
                basket);
        }
    }
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
            const auto& leg = opportunity.legs[i];
            legs_summary << leg.label << " " << leg.side << " @ $" << leg.price << " +fee $"
                         << leg.fee << " x " << leg.size;
        }

        spdlog::info(
            "[ARB] {} | {} | {} | {} | basket={:.4f} target={:.4f} fees={:.4f} | "
            "edge={:.2f}% (pre-fee {:.2f}%) | max_size={:.2f} | legs: {}",
            opportunity.rule,
            opportunity.match_slug,
            opportunity.detail,
            opportunity.series_ticker,
            opportunity.basket_cost,
            opportunity.target_cost,
            opportunity.fees,
            opportunity.edge_pct,
            opportunity.edge_before_fees * 100.0,
            opportunity.max_executable_size,
            legs_summary.str());
    }
}
