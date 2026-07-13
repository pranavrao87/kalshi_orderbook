#include "engine.h"

#include <csignal>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "arb_detector.h"
#include "config.h"
#include "kalshi_auth.h"
#include "kalshi_ws.h"
#include "market_registry.h"
#include "orderbook.h"
#include "orderbook_sync.h"

using json = nlohmann::json;

namespace {

volatile std::sig_atomic_t g_running = 1;

void handle_signal(int) {
    g_running = 0;
}

void configure_logging(const std::string& log_level) {
    if (log_level == "trace") {
        spdlog::set_level(spdlog::level::trace);
    } else if (log_level == "debug") {
        spdlog::set_level(spdlog::level::debug);
    } else if (log_level == "warn") {
        spdlog::set_level(spdlog::level::warn);
    } else if (log_level == "error") {
        spdlog::set_level(spdlog::level::err);
    } else {
        spdlog::set_level(spdlog::level::info);
    }
}

json make_subscribe_message(int command_id, const std::vector<std::string>& market_tickers) {
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

void print_pricing_summary(
    const MarketRegistry& registry,
    const std::map<std::string, MarketOrderbook>& books,
    const std::vector<std::string>& key_series,
    std::ostream& out) {
    out << "\nPricing summary (executable quotes)\n";
    out << "Only markets with loaded orderbook data are shown.\n\n";

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

class EngineImpl {
public:
    explicit EngineImpl(AppConfig config) : config_(std::move(config)) {}

    int run() {
        std::signal(SIGINT, handle_signal);

        const KalshiCredentials credentials = load_credentials();
        auth_headers_ = create_auth_headers(credentials, "GET", "/trade-api/ws/v2");

        spdlog::info("loading men's world cup market registry...");
        registry_ = MarketRegistry::load_mens_world_cup_matches();
        registry_.print_summary(std::cout);

        market_tickers_ = registry_.all_tickers();
        if (market_tickers_.empty()) {
            throw std::runtime_error("no open men's world cup match markets found");
        }

        spdlog::info(
            "found {} markets across {} matches",
            market_tickers_.size(),
            registry_.matches().size());

        arb_detector_ = ArbDetector(config_.min_arb_edge);
        spdlog::info("arb detector enabled (min edge {:.2f}%)", config_.min_arb_edge * 100.0);

        int reconnect_delay_ms = config_.reconnect_initial_ms;
        while (g_running) {
            try {
                run_session();
                reconnect_delay_ms = config_.reconnect_initial_ms;
            } catch (const std::exception& e) {
                websocket_.reset();
                books_.clear();
                sync_.reset();

                if (!g_running) {
                    break;
                }

                spdlog::warn(
                    "session error: {}. reconnecting in {}ms",
                    e.what(),
                    reconnect_delay_ms);
                std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms));
                reconnect_delay_ms = std::min(reconnect_delay_ms * 2, config_.reconnect_max_ms);
            }
        }

        scan_arbitrage();
        print_pricing_summary(registry_, books_, config_.pricing_summary_series, std::cout);
        websocket_.reset();
        spdlog::info("disconnected");
        return 0;
    }

private:
    AppConfig config_;
    std::map<std::string, std::string> auth_headers_;
    MarketRegistry registry_;
    std::vector<std::string> market_tickers_;
    std::unique_ptr<KalshiWebSocket> websocket_;
    OrderbookSync sync_;
    ArbDetector arb_detector_{0.01};
    std::map<std::string, MarketOrderbook> books_;
    int next_command_id_ = 1;
    std::chrono::steady_clock::time_point last_scan_{};

    void run_session() {
        websocket_ = std::make_unique<KalshiWebSocket>(config_.ws_url);
        websocket_->connect(auth_headers_);
        spdlog::info("connected to {}", config_.ws_url);

        next_command_id_ = 1;
        sync_.reset();
        books_.clear();

        const json subscribe_message = make_subscribe_message(next_command_id_++, market_tickers_);
        websocket_->send_text(subscribe_message.dump());
        spdlog::info("subscribed to orderbook_delta for {} markets", market_tickers_.size());

        last_scan_ = std::chrono::steady_clock::now();

        while (g_running) {
            const std::string payload = websocket_->receive_text();
            json message = json::parse(payload);
            process_message(message);
        }
    }

    void process_message(const json& message) {
        const std::string type = message.value("type", "");

        if (type == "error") {
            spdlog::error("websocket error: {}", message.dump());
            return;
        }

        if (type == "subscribed") {
            if (message.contains("msg") && message["msg"].contains("sid")) {
                sync_.set_subscription_id(message["msg"]["sid"].get<int>());
            }
            spdlog::info("subscribed: {}", message.dump());
            return;
        }

        if (type == "orderbook_snapshot" || type == "orderbook_delta") {
            const std::string ticker = message["msg"]["market_ticker"].get<std::string>();
            MarketOrderbook& book = books_[ticker];

            try {
                const BookUpdateResult result = sync_.handle_message(message, book);
                if (result == BookUpdateResult::GapDetected) {
                    request_resnapshot(ticker);
                    return;
                }

                if (result == BookUpdateResult::Updated) {
                    if (type == "orderbook_snapshot") {
                        spdlog::debug("loaded snapshot for {}", ticker);
                    } else {
                        spdlog::debug(
                            "delta {} {} {} @ {}",
                            ticker,
                            message["msg"]["side"].get<std::string>(),
                            message["msg"]["delta_fp"].get<std::string>(),
                            message["msg"]["price_dollars"].get<std::string>());
                    }
                    maybe_scan_arbitrage();
                }
            } catch (const std::exception& e) {
                spdlog::warn("failed to process {} for {}: {}", type, ticker, e.what());
            }
            return;
        }

        spdlog::debug("ignored message type {}: {}", type, message.dump());
    }

    void request_resnapshot(const std::string& ticker) {
        const auto request = sync_.make_resnapshot_request(ticker, next_command_id_++);
        if (!request) {
            spdlog::warn("cannot request resnapshot for {} without subscription id", ticker);
            return;
        }

        websocket_->send_text(request->dump());
        spdlog::info("requested orderbook resnapshot for {}", ticker);
    }

    void maybe_scan_arbitrage() {
        if (config_.scan_interval_ms > 0) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_scan_);
            if (elapsed.count() < config_.scan_interval_ms) {
                return;
            }
            last_scan_ = now;
        }

        scan_arbitrage();
    }

    void scan_arbitrage() {
        const auto opportunities = arb_detector_.scan(registry_, books_);
        if (!opportunities.empty()) {
            arb_detector_.log_opportunities(opportunities);
        }
    }
};

}  // namespace

int Engine::run() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    try {
        const AppConfig config = load_config();
        configure_logging(config.log_level);

        EngineImpl engine(config);
        const int result = engine.run();
        curl_global_cleanup();
        return result;
    } catch (const std::exception& e) {
        spdlog::error("{}", e.what());
        curl_global_cleanup();
        return 1;
    }
}
