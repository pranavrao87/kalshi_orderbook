#pragma once

#include <map>
#include <string>

struct KalshiCredentials {
    std::string key_id;
    std::string private_key_path;
};

void load_dotenv_from_search_paths();

KalshiCredentials load_credentials();

std::map<std::string, std::string> create_auth_headers(
    const KalshiCredentials& credentials,
    const std::string& method,
    const std::string& path);
