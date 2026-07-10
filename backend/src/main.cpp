#include <csignal>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "kalshi_auth.h"
#include "kalshi_rest.h"
#include "kalshi_ws.h"
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

std::vector<std::string> fetch_market_tickers(const std::string& series_ticker) {
    const std::string markets_url =
        "https://external-api.kalshi.com/trade-api/v2/markets"
        "?series_ticker=" +
        series_ticker + "&status=open";

    const json markets_data = json::parse(fetch(markets_url));
    std::vector<std::string> tickers;

    for (const auto& market : markets_data["markets"]) {
        tickers.push_back(market["ticker"].get<std::string>());
    }

    return tickers;
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
        books[ticker].load_snapshot(message);
        spdlog::info("loaded snapshot for {}", ticker);
        books[ticker].print_summary(std::cout);
        return;
    }

    if (type == "orderbook_delta") {
        const std::string ticker = message["msg"]["market_ticker"].get<std::string>();
        books[ticker].apply_delta(message);
        spdlog::info(
            "delta {} {} {} @ {}",
            ticker,
            message["msg"]["side"].get<std::string>(),
            message["msg"]["delta_fp"].get<std::string>(),
            message["msg"]["price_dollars"].get<std::string>());
        return;
    }

    spdlog::debug("ignored message type {}: {}", type, message.dump());
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
        const std::string series_ticker = read_env_or_default("KALSHI_SERIES_TICKER", "KXWCADVANCE");

        const std::vector<std::string> market_tickers = fetch_market_tickers(series_ticker);
        if (market_tickers.empty()) {
            throw std::runtime_error("no open markets found for series " + series_ticker);
        }

        spdlog::info("found {} open markets in {}", market_tickers.size(), series_ticker);

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
