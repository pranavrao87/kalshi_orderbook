#include <iostream>
#include <string>
#include <curl/curl.h>
#include "kalshi_rest.h"

int main() {
    std::string url = "https://external-api.kalshi.com/trade-api/v2/series/KXHIGHNY";

    // 1. Initialize libcurl globally (should be called once in your app)
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // 2. Initialize a local easy handle session
    CURL* curl = curl_easy_init();
    std::string responseString;
    
    if (curl) {
        // 3. Set the target URL
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // 4. Custom User-Agent (Highly recommended; some APIs reject empty agents)
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        // 5. Configure the callback to capture the response data
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseString);

        // 6. Perform the blocking network request
        CURLcode res = curl_easy_perform(curl);

        // 7. Check for errors
        if (res != CURLE_OK) {
            std::cerr << "cURL Error: " << curl_easy_strerror(res) << std::endl;
        } else {
            // Success! Print the response
            std::cout << "Response:\n" << responseString << std::endl;
        }

        // 8. Clean up the local handle
        curl_easy_cleanup(curl);
    }

    // 9. Clean up global state
    curl_global_cleanup();
            
    return 0;
}