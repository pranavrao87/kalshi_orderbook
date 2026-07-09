#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "kalshi_rest.h"

using json = nlohmann::json;

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    try {
        const std::string markets_url =
            "https://external-api.kalshi.com/trade-api/v2/markets"
            "?series_ticker=KXWCADVANCE&status=open";

        json markets_data = json::parse(fetch(markets_url));

        std::cout << "\nActive markets in KXWCADVANCE series:\n";

        for (const auto& market : markets_data["markets"]) {
            std::cout << "- " << market["ticker"].get<std::string>()
                      << ": " << market["title"].get<std::string>() << "\n";
            std::cout << "  Event: " << market["event_ticker"].get<std::string>() << "\n";
            std::cout << "  Yes Price: $" << market["yes_bid_dollars"].get<std::string>()
                      << " | Volume: " << market["volume_fp"].get<std::string>() << "\n\n";
        }

        if (!markets_data["markets"].empty()) {
            const std::string event_ticker =
                markets_data["markets"][0]["event_ticker"].get<std::string>();

            const std::string event_url =
                "https://external-api.kalshi.com/trade-api/v2/events/" + event_ticker;

            json event_data = json::parse(fetch(event_url));

            std::cout << "Event Details:\n";
            std::cout << "Title: " << event_data["event"]["title"].get<std::string>() << "\n";
            std::cout << "Category: " << event_data["event"]["category"].get<std::string>() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        curl_global_cleanup();
        return 1;
    }

    curl_global_cleanup();
    return 0;
}
