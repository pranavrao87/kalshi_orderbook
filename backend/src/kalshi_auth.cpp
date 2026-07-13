#include "kalshi_auth.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdexcept>
#include <vector>

namespace {

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string strip_quotes(std::string value) {
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }

    return value;
}

bool file_exists(const std::string& path) {
    std::ifstream file(path);
    return file.good();
}

void load_dotenv_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto comment = line.find(" #");
        if (comment != std::string::npos) {
            line = trim(line.substr(0, comment));
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, equals));
        const std::string value = strip_quotes(trim(line.substr(equals + 1)));
        if (key.empty() || value.empty()) {
            continue;
        }

        setenv(key.c_str(), value.c_str(), 0);
    }
}

std::string read_env(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string{};
}

std::string base64_encode(const unsigned char* data, size_t length) {
    const int encoded_length = 4 * static_cast<int>((length + 2) / 3);
    std::string encoded(encoded_length, '\0');
    const int actual_length =
        EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()), data, static_cast<int>(length));
    encoded.resize(static_cast<size_t>(actual_length));
    return encoded;
}

EVP_PKEY* load_private_key(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        throw std::runtime_error("failed to open private key file: " + path);
    }

    EVP_PKEY* key = PEM_read_PrivateKey(file, nullptr, nullptr, nullptr);
    fclose(file);

    if (!key) {
        throw std::runtime_error("failed to parse private key: " + path);
    }

    return key;
}

std::string sign_message(EVP_PKEY* private_key, const std::string& message) {
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) {
        throw std::runtime_error("failed to create signing context");
    }

    EVP_PKEY_CTX* key_context = nullptr;
    if (EVP_DigestSignInit(context, &key_context, EVP_sha256(), nullptr, private_key) != 1) {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("failed to initialize digest signer");
    }

    if (EVP_PKEY_CTX_set_rsa_padding(key_context, RSA_PKCS1_PSS_PADDING) != 1 ||
        EVP_PKEY_CTX_set_rsa_pss_saltlen(key_context, RSA_PSS_SALTLEN_DIGEST) != 1) {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("failed to configure RSA-PSS signer");
    }

    size_t signature_length = 0;
    if (EVP_DigestSignUpdate(context, message.data(), message.size()) != 1 ||
        EVP_DigestSignFinal(context, nullptr, &signature_length) != 1) {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("failed to size signature");
    }

    std::vector<unsigned char> signature(signature_length);
    if (EVP_DigestSignFinal(context, signature.data(), &signature_length) != 1) {
        EVP_MD_CTX_free(context);
        throw std::runtime_error("failed to sign message");
    }

    EVP_MD_CTX_free(context);
    return base64_encode(signature.data(), signature_length);
}

}  // namespace

void load_dotenv_from_search_paths() {
    static const std::vector<std::string> candidates = {
        ".env",
        "../.env",
        "../../.env",
    };

    for (const std::string& path : candidates) {
        if (file_exists(path)) {
            load_dotenv_file(path);
            return;
        }
    }
}

KalshiCredentials load_credentials() {
    load_dotenv_from_search_paths();

    KalshiCredentials credentials{
        .key_id = read_env("KALSHI_ACCESS_KEY"),
        .private_key_path = read_env("KALSHI_PRIVATE_KEY_PATH"),
    };

    if (credentials.key_id.empty() || credentials.private_key_path.empty()) {
        throw std::runtime_error(
            "missing credentials: set KALSHI_ACCESS_KEY and KALSHI_PRIVATE_KEY_PATH "
            "in your environment or in a .env file at the repo root");
    }

    return credentials;
}

std::map<std::string, std::string> create_auth_headers(
    const KalshiCredentials& credentials,
    const std::string& method,
    const std::string& path) {
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

    const std::string timestamp_string = std::to_string(timestamp);
    const std::string message = timestamp_string + method + path;

    EVP_PKEY* private_key = load_private_key(credentials.private_key_path);
    const std::string signature = sign_message(private_key, message);
    EVP_PKEY_free(private_key);

    return {
        {"KALSHI-ACCESS-KEY", credentials.key_id},
        {"KALSHI-ACCESS-SIGNATURE", signature},
        {"KALSHI-ACCESS-TIMESTAMP", timestamp_string},
        {"Content-Type", "application/json"},
    };
}
