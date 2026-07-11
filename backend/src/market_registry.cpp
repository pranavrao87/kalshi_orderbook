#include <algorithm>
#include "market_registry.h"

#include "kalshi_rest.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace {

constexpr const char* kApiBase = "https://external-api.kalshi.com/trade-api/v2";

std::string url_encode(const std::string& value) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("failed to initialize curl for url encoding");
    }

    char* encoded =
        curl_easy_escape(curl, value.c_str(), static_cast<int>(value.length()));
    if (!encoded) {
        curl_easy_cleanup(curl);
        throw std::runtime_error("failed to url-encode value");
    }

    const std::string result(encoded);
    curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

std::vector<json> fetch_paginated(const std::string& base_url, const std::string& field_name) {
    std::vector<json> items;
    std::string cursor;

    while (true) {
        std::string url = base_url;
        if (!cursor.empty()) {
            url += "&cursor=" + url_encode(cursor);
        }

        const json response = json::parse(fetch(url));
        if (!response.contains(field_name) || !response[field_name].is_array()) {
            break;
        }

        for (const auto& item : response[field_name]) {
            items.push_back(item);
        }

        if (!response.contains("cursor") || response["cursor"].is_null() ||
            response["cursor"].get<std::string>().empty()) {
            break;
        }

        cursor = response["cursor"].get<std::string>();
    }

    return items;
}

bool is_fifa_world_cup_event(const json& event) {
    if (!event.contains("product_metadata") || !event["product_metadata"].is_object()) {
        return false;
    }

    const auto& metadata = event["product_metadata"];
    return metadata.value("competition", "") == "FIFA World Cup";
}

bool is_match_slug(const std::string& slug) {
    static const std::regex pattern(R"(^\d{2}[A-Z]{3}\d{2}[A-Z]{3,}$)");
    return std::regex_match(slug, pattern);
}

std::string extract_match_slug(const std::string& event_ticker) {
    const auto dash = event_ticker.find('-');
    if (dash == std::string::npos || dash + 1 >= event_ticker.size()) {
        return {};
    }

    return event_ticker.substr(dash + 1);
}


std::string extract_outcome_key(const std::string& market_ticker, const std::string& event_ticker) {
    if (market_ticker.size() <= event_ticker.size() + 1) {
        return {};
    }

    if (market_ticker.compare(0, event_ticker.size(), event_ticker) != 0) {
        return {};
    }

    return market_ticker.substr(event_ticker.size() + 1);
}

std::string pick_match_title(const std::vector<MarketEventGroup>& groups) {
    static const std::vector<std::string> preferred_series = {
        "KXWCADVANCE",
        "KXWCGAME",
        "KXWC1H",
        "KXWCSCORE",
        "KXWCTOTAL",
    };

    for (const std::string& series_ticker : preferred_series) {
        for (const auto& group : groups) {
            if (group.series_ticker != series_ticker || group.title.empty()) {
                continue;
            }

            std::string title = group.title;
            const auto colon = title.find(':');
            if (colon != std::string::npos) {
                title = title.substr(0, colon);
            }
            return title;
        }
    }

    if (!groups.empty() && !groups.front().title.empty()) {
        std::string title = groups.front().title;
        const auto colon = title.find(':');
        if (colon != std::string::npos) {
            title = title.substr(0, colon);
        }
        return title;
    }

    return {};
}

MarketInfo parse_market(
    const json& market,
    const json& event,
    const std::string& series_ticker,
    const std::string& competition_scope,
    bool mutually_exclusive) {
    const std::string event_ticker = market.value("event_ticker", event.value("event_ticker", ""));
    const std::string ticker = market.at("ticker").get<std::string>();

    return MarketInfo{
        .ticker = ticker,
        .event_ticker = event_ticker,
        .series_ticker = series_ticker,
        .title = market.value("title", ""),
        .yes_sub_title = market.value("yes_sub_title", ""),
        .competition_scope = competition_scope,
        .mutually_exclusive = mutually_exclusive,
        .outcome_key = extract_outcome_key(ticker, event_ticker),
    };
}

std::vector<std::string> fetch_world_cup_series_tickers() {
    const auto series_items =
        fetch_paginated(std::string(kApiBase) + "/series?limit=200", "series");

    std::vector<std::string> series_tickers;
    for (const auto& series : series_items) {
        const std::string ticker = series.value("ticker", "");
        if (ticker.rfind("KXWC", 0) == 0) {
            series_tickers.push_back(ticker);
        }
    }

    return series_tickers;
}

}  // namespace

std::string series_role_label(const std::string& series_ticker) {
    static const std::unordered_map<std::string, std::string> labels = {
        {"KXWCADVANCE", "advance"},
        {"KXWCGAME", "regulation_ml"},
        {"KXWC1H", "first_half_winner"},
        {"KXWC2H", "second_half_winner"},
        {"KXWCSCORE", "correct_score"},
        {"KXWCTOTAL", "match_total"},
        {"KXWC1HTOTAL", "first_half_total"},
        {"KXWC2HTOTAL", "second_half_total"},
        {"KXWCSPREAD", "spread"},
        {"KXWCBTTS", "btts"},
        {"KXWC1HBTTS", "first_half_btts"},
        {"KXWC2HBTTS", "second_half_btts"},
        {"KXWC1HSPREAD", "first_half_spread"},
        {"KXWC2HSPREAD", "second_half_spread"},
        {"KXWCPREPACK", "prepack_combo"},
        {"KXWCTEAMGOALS", "team_goals"},
        {"KXWCTEAMTOTAL", "team_total"},
        {"KXWCWINMARGIN", "win_margin"},
        {"KXWCMOV", "method_of_victory"},
        {"KXWCTTSF", "team_to_score_first"},
        {"KXWCFTTS", "first_team_to_score"},
    };

    const auto it = labels.find(series_ticker);
    return it != labels.end() ? it->second : series_ticker;
}

MarketRegistry MarketRegistry::load_mens_world_cup_matches() {
    MarketRegistry registry;
    std::unordered_map<std::string, MatchGroup> matches_by_slug;
    const std::vector<std::string> series_tickers = fetch_world_cup_series_tickers();

    for (const std::string& series_ticker : series_tickers) {
        const std::string events_url =
            std::string(kApiBase) + "/events?series_ticker=" + series_ticker +
            "&status=open&with_nested_markets=true&limit=200";

        const auto events = fetch_paginated(events_url, "events");
        for (const auto& event : events) {
            if (!is_fifa_world_cup_event(event)) {
                continue;
            }

            const std::string event_ticker = event.at("event_ticker").get<std::string>();
            const std::string match_slug = extract_match_slug(event_ticker);
            if (!is_match_slug(match_slug)) {
                continue;
            }

            const std::string competition_scope =
                event["product_metadata"].value("competition_scope", "");
            const bool mutually_exclusive = event.value("mutually_exclusive", false);

            MarketEventGroup event_group{
                .event_ticker = event_ticker,
                .series_ticker = series_ticker,
                .title = event.value("title", ""),
                .competition_scope = competition_scope,
                .mutually_exclusive = mutually_exclusive,
            };

            if (event.contains("markets") && event["markets"].is_array()) {
                for (const auto& market : event["markets"]) {
                    if (market.value("status", "") != "active") {
                        continue;
                    }

                    event_group.markets.push_back(parse_market(
                        market,
                        event,
                        series_ticker,
                        competition_scope,
                        mutually_exclusive));
                }
            }

            if (event_group.markets.empty()) {
                continue;
            }

            MatchGroup& match_group = matches_by_slug[match_slug];
            match_group.match_slug = match_slug;
            match_group.event_groups.push_back(std::move(event_group));
        }
    }

    for (auto& [slug, match_group] : matches_by_slug) {
        match_group.title = pick_match_title(match_group.event_groups);

        for (const auto& event_group : match_group.event_groups) {
            for (const auto& market : event_group.markets) {
                match_group.all_markets.push_back(market);
            }
        }

        registry.matches_.push_back(std::move(match_group));
    }

    std::sort(registry.matches_.begin(), registry.matches_.end(), [](const MatchGroup& lhs, const MatchGroup& rhs) {
        return lhs.match_slug < rhs.match_slug;
    });

    return registry;
}

std::vector<std::string> MarketRegistry::all_tickers() const {
    std::vector<std::string> tickers;
    std::unordered_set<std::string> seen;

    for (const auto& match : matches_) {
        for (const auto& market : match.all_markets) {
            if (seen.insert(market.ticker).second) {
                tickers.push_back(market.ticker);
            }
        }
    }

    return tickers;
}

void MarketRegistry::print_summary(std::ostream& out) const {
    out << "Men's World Cup match registry\n";
    out << "Matches: " << matches_.size() << "\n";

    std::size_t total_markets = 0;
    for (const auto& match : matches_) {
        total_markets += match.all_markets.size();
    }
    out << "Markets: " << total_markets << "\n\n";

    for (const auto& match : matches_) {
        out << match.match_slug;
        if (!match.title.empty()) {
            out << " | " << match.title;
        }
        out << "\n";

        for (const auto& event_group : match.event_groups) {
            out << "  " << series_role_label(event_group.series_ticker) << " ("
                << event_group.series_ticker << ")\n";
            out << "    scope: " << event_group.competition_scope << "\n";
            out << "    mutually_exclusive: "
                << (event_group.mutually_exclusive ? "true" : "false") << "\n";
            out << "    markets: " << event_group.markets.size() << "\n";

            for (const auto& market : event_group.markets) {
                out << "      - " << market.ticker;
                if (!market.yes_sub_title.empty()) {
                    out << " | " << market.yes_sub_title;
                } else if (!market.title.empty()) {
                    out << " | " << market.title;
                }
                out << "\n";
            }
        }

        out << "\n";
    }
}
