#pragma once

#include <string>
#include <vector>

struct AppConfig {
    std::string ws_url = "wss://external-api-ws.kalshi.com/trade-api/ws/v2";
    double min_arb_edge = 0.01;
    std::string log_level = "info";
    int reconnect_initial_ms = 1000;
    int reconnect_max_ms = 30000;
    int scan_interval_ms = 0;
    std::vector<std::string> pricing_summary_series = {
        "KXWCADVANCE",
        "KXWCGAME",
        "KXWCSCORE",
        "KXWCTOTAL",
        "KXWCMOV",
    };
};

AppConfig load_config();
