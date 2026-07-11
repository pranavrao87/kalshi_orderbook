#include <csignal>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "kalshi_auth.h"
#include "kalshi_ws.h"
#include "market_registry.h"
#include "orderbook.h"

using json = nlohmann::json;

namespace {

volatile std::sig_atomic_t g_running = 1;

void handle_signal(int) {
    g_running = 0;
}

std::string read_env_or_default(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : fallback;
}

json make_subscribe_message(
    int command_id,
    const std::vector<std::string>& market_tickers) {
    return {
        {"id", command_id},
        {"cmd", "subscribe"},
        {"params",
         {
             {"channels", json::array({"orderbook_delta"})},
             {"market_tickers", market_tickers},
             {"use_yes_price", true},
         }},
    };
}

void handle_message(
    const json& message,
    std::map<std::string, MarketOrderbook>& books) {
    const std::string type = message.value("type", "");

    if (type == "error") {
        spdlog::error("websocket error: {}", message.dump());
        return;
    }

    if (type == "subscribed") {
        spdlog::info("subscribed: {}", message.dump());
        return;
    }

    if (type == "orderbook_snapshot") {
        const std::string ticker = message["msg"]["market_ticker"].get<std::string>();
        try {
            books[ticker].load_snapshot(message);
            spdlog::debug("loaded snapshot for {}", ticker);
        } catch (const std::exception& e) {
            spdlog::warn("failed to load snapshot for {}: {}", ticker, e.what());
            spdlog::debug("snapshot payload: {}", message.dump());
        }
        return;
    }

    if (type == "orderbook_delta") {
        const std::string ticker = message["msg"]["market_ticker"].get<std::string>();
        try {
            books[ticker].apply_delta(message);
            spdlog::debug(
                "delta {} {} {} @ {}",
                ticker,
                message["msg"]["side"].get<std::string>(),
                message["msg"]["delta_fp"].get<std::string>(),
                message["msg"]["price_dollars"].get<std::string>());
        } catch (const std::exception& e) {
            spdlog::warn("failed to apply delta for {}: {}", ticker, e.what());
        }
        return;
    }

    spdlog::debug("ignored message type {}: {}", type, message.dump());
}

void print_pricing_summary(
    const MarketRegistry& registry,
    const std::map<std::string, MarketOrderbook>& books,
    std::ostream& out) {
    static const std::vector<std::string> key_series = {
        "KXWCADVANCE",
        "KXWCGAME",
        "KXWCSCORE",
        "KXWCTOTAL",
        "KXWCMOV",
    };

    out << "\nPricing summary (executable quotes)\n";
    out << "Only markets with loaded orderbook data are shown.\n";
    out << "Let the app run ~30s after subscribing so snapshots can arrive.\n\n";

    for (const auto& match : registry.matches()) {
        out << match.match_slug;
        if (!match.title.empty()) {
            out << " | " << match.title;
        }
        out << "\n";

        for (const std::string& series_ticker : key_series) {
            bool printed_series = false;

            for (const auto& event_group : match.event_groups) {
                if (event_group.series_ticker != series_ticker) {
                    continue;
                }

                if (!printed_series) {
                    out << "  " << series_role_label(event_group.series_ticker) << " ["
                        << event_group.competition_scope << "]:\n";
                    printed_series = true;
                }

                std::size_t shown = 0;
                for (const auto& market : event_group.markets) {
                    const auto book_it = books.find(market.ticker);
                    if (book_it == books.end() || !book_it->second.has_liquidity()) {
                        continue;
                    }

                    out << "    ";
                    if (!market.yes_sub_title.empty()) {
                        out << market.yes_sub_title << " | ";
                    } else if (!market.title.empty()) {
                        out << market.title << " | ";
                    }
                    book_it->second.print_quote(out);
                    ++shown;
                }

                if (shown == 0) {
                    out << "    (no orderbook data loaded)\n";
                }
            }
        }
    }
}

}  // namespace

int main() {
    std::signal(SIGINT, handle_signal);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    try {
        const KalshiCredentials credentials = load_credentials();
        const std::string ws_url = read_env_or_default(
            "KALSHI_WS_URL",
            "wss://external-api-ws.kalshi.com/trade-api/ws/v2");

        spdlog::info("loading men's world cup market registry...");
        const MarketRegistry registry = MarketRegistry::load_mens_world_cup_matches();
        registry.print_summary(std::cout);

        const std::vector<std::string> market_tickers = registry.all_tickers();
        if (market_tickers.empty()) {
            throw std::runtime_error("no open men's world cup match markets found");
        }

        spdlog::info(
            "found {} markets across {} matches",
            market_tickers.size(),
            registry.matches().size());

        const auto auth_headers = create_auth_headers(credentials, "GET", "/trade-api/ws/v2");

        KalshiWebSocket websocket(ws_url);
        websocket.connect(auth_headers);
        spdlog::info("connected to {}", ws_url);

        const json subscribe_message = make_subscribe_message(1, market_tickers);
        websocket.send_text(subscribe_message.dump());
        spdlog::info("subscribed to orderbook_delta for {} markets", market_tickers.size());

        std::map<std::string, MarketOrderbook> books;
        while (g_running) {
            const std::string payload = websocket.receive_text();
            json message;
            try {
                message = json::parse(payload);
            } catch (const json::parse_error& e) {
                spdlog::error("failed to parse websocket payload ({} bytes): {}", payload.size(), e.what());
                const std::size_t preview_length = std::min(payload.size(), std::size_t{200});
                spdlog::error("payload starts with: {}", payload.substr(0, preview_length));
                throw;
            }
            handle_message(message, books);
        }

        print_pricing_summary(registry, books, std::cout);
        websocket.close();
        spdlog::info("disconnected");
    } catch (const std::exception& e) {
        spdlog::error("{}", e.what());
        curl_global_cleanup();
        return 1;
    }

    curl_global_cleanup();
    return 0;
}
