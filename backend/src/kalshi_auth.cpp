#include "kalshi_auth.h"

#include <chrono>
#include <cstdlib>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdexcept>
#include <vector>

namespace {

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

KalshiCredentials load_credentials() {
    KalshiCredentials credentials{
        .key_id = read_env("KALSHI_ACCESS_KEY"),
        .private_key_path = read_env("KALSHI_PRIVATE_KEY_PATH"),
    };

    if (credentials.key_id.empty() || credentials.private_key_path.empty()) {
        throw std::runtime_error(
            "missing credentials: set KALSHI_ACCESS_KEY and KALSHI_PRIVATE_KEY_PATH");
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
