#include "config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <vector>

#include "kalshi_auth.h"

namespace {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool file_exists(const std::string& path) {
    std::ifstream file(path);
    return file.good();
}

std::string find_config_path() {
    static const std::vector<std::string> candidates = {
        "config/settings.yaml",
        "../config/settings.yaml",
        "../../config/settings.yaml",
    };

    for (const std::string& path : candidates) {
        if (file_exists(path)) {
            return path;
        }
    }

    return {};
}

int count_leading_spaces(const std::string& line) {
    int count = 0;
    for (char ch : line) {
        if (ch == ' ') {
            ++count;
        } else {
            break;
        }
    }
    return count;
}

std::string unquote(std::string value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

std::vector<std::string> parse_string_list(const std::string& value) {
    std::vector<std::string> items;
    std::string current;
    std::istringstream stream(value);
    while (std::getline(stream, current, ',')) {
        current = trim(unquote(current));
        if (!current.empty()) {
            items.push_back(current);
        }
    }
    return items;
}

std::string read_env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string{};
}

double parse_double(const std::string& value, double fallback) {
    try {
        return std::stod(value);
    } catch (const std::exception&) {
        return fallback;
    }
}

int parse_int(const std::string& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return fallback;
    }
}

void apply_yaml_value(AppConfig& config, const std::string& section, const std::string& key, const std::string& raw_value) {
    const std::string value = unquote(trim(raw_value));

    if (section == "kalshi") {
        if (key == "ws_url") {
            config.ws_url = value;
        } else if (key == "min_arb_edge") {
            config.min_arb_edge = parse_double(value, config.min_arb_edge);
        } else if (key == "taker_fee_coeff") {
            config.taker_fee_coeff = parse_double(value, config.taker_fee_coeff);
        }
        return;
    }

    if (section == "engine") {
        if (key == "log_level") {
            config.log_level = value;
        } else if (key == "min_arb_edge") {
            config.min_arb_edge = parse_double(value, config.min_arb_edge);
        } else if (key == "taker_fee_coeff") {
            config.taker_fee_coeff = parse_double(value, config.taker_fee_coeff);
        } else if (key == "reconnect_initial_ms") {
            config.reconnect_initial_ms = parse_int(value, config.reconnect_initial_ms);
        } else if (key == "reconnect_max_ms") {
            config.reconnect_max_ms = parse_int(value, config.reconnect_max_ms);
        } else if (key == "scan_interval_ms") {
            config.scan_interval_ms = parse_int(value, config.scan_interval_ms);
        } else if (key == "pricing_summary_series") {
            config.pricing_summary_series = parse_string_list(value);
        }
    }
}

void load_yaml_file(const std::string& path, AppConfig& config) {
    std::ifstream file(path);
    if (!file) {
        return;
    }

    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        if (trimmed.back() == ':' && trimmed.find(':') == trimmed.size() - 1) {
            section = trimmed.substr(0, trimmed.size() - 1);
            continue;
        }

        const auto colon = trimmed.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const int indent = count_leading_spaces(line);
        const std::string key = trim(trimmed.substr(0, colon));
        const std::string value = trim(trimmed.substr(colon + 1));

        if (indent == 0) {
            section = key;
            continue;
        }

        if (!section.empty()) {
            apply_yaml_value(config, section, key, value);
        }
    }
}

void apply_env_overrides(AppConfig& config) {
    const std::string ws_url = read_env_or_empty("KALSHI_WS_URL");
    if (!ws_url.empty()) {
        config.ws_url = ws_url;
    }

    const std::string min_edge = read_env_or_empty("KALSHI_MIN_ARB_EDGE");
    if (!min_edge.empty()) {
        config.min_arb_edge = parse_double(min_edge, config.min_arb_edge);
    }

    const std::string fee_coeff = read_env_or_empty("KALSHI_TAKER_FEE_COEFF");
    if (!fee_coeff.empty()) {
        config.taker_fee_coeff = parse_double(fee_coeff, config.taker_fee_coeff);
    }

    const std::string log_level = read_env_or_empty("KALSHI_LOG_LEVEL");
    if (!log_level.empty()) {
        config.log_level = log_level;
    }
}

}  // namespace

AppConfig load_config() {
    load_dotenv_from_search_paths();

    AppConfig config;
    const std::string config_path = find_config_path();
    if (!config_path.empty()) {
        load_yaml_file(config_path, config);
    }

    apply_env_overrides(config);
    return config;
}
