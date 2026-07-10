#include "kalshi_ws.h"

#include <curl/curl.h>
#include <curl/websockets.h>
#include <stdexcept>
#include <vector>

KalshiWebSocket::KalshiWebSocket(std::string url) : url_(std::move(url)) {}

KalshiWebSocket::~KalshiWebSocket() {
    close();
}

void KalshiWebSocket::connect(const std::map<std::string, std::string>& headers) {
    if (curl_) {
        throw std::runtime_error("websocket is already connected");
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("failed to initialize curl for websocket");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);

    std::vector<std::string> header_storage;
    curl_slist* header_list = nullptr;
    for (const auto& [name, value] : headers) {
        header_storage.push_back(name + ": " + value);
        header_list = curl_slist_append(header_list, header_storage.back().c_str());
    }

    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    const CURLcode connect_result = curl_easy_perform(curl);
    if (connect_result != CURLE_OK) {
        if (header_list) {
            curl_slist_free_all(header_list);
        }
        curl_easy_cleanup(curl);
        throw std::runtime_error(std::string("websocket connect failed: ") + curl_easy_strerror(connect_result));
    }

    curl_ = curl;
    header_list_ = header_list;
}

void KalshiWebSocket::send_text(const std::string& message) {
    if (!curl_) {
        throw std::runtime_error("websocket is not connected");
    }

    size_t sent = 0;
    const CURLcode send_result = curl_ws_send(
        static_cast<CURL*>(curl_),
        message.data(),
        message.size(),
        &sent,
        0,
        CURLWS_TEXT);

    if (send_result != CURLE_OK) {
        throw std::runtime_error(std::string("websocket send failed: ") + curl_easy_strerror(send_result));
    }

    if (sent != message.size()) {
        throw std::runtime_error("websocket send was incomplete");
    }
}

std::string KalshiWebSocket::receive_text() {
    if (!curl_) {
        throw std::runtime_error("websocket is not connected");
    }

    std::string message;
    char buffer[16384];
    CURL* curl = static_cast<CURL*>(curl_);

    while (true) {
        size_t received = 0;
        const curl_ws_frame* meta = nullptr;
        CURLcode receive_result = curl_ws_recv(curl, buffer, sizeof(buffer), &received, &meta);

        if (receive_result == CURLE_AGAIN) {
            continue;
        }

        if (receive_result != CURLE_OK) {
            throw std::runtime_error(
                std::string("websocket receive failed: ") + curl_easy_strerror(receive_result));
        }

        if (meta && (meta->flags & CURLWS_CLOSE)) {
            throw std::runtime_error("websocket closed by server");
        }

        if (meta && (meta->flags & (CURLWS_PING | CURLWS_PONG))) {
            if (meta->bytesleft > 0) {
                continue;
            }
            if (meta->flags & CURLWS_CONT) {
                continue;
            }
            continue;
        }

        if (received > 0) {
            message.append(buffer, received);
        }

        if (!meta) {
            break;
        }

        if (meta->bytesleft > 0) {
            continue;
        }

        if (meta->flags & CURLWS_CONT) {
            continue;
        }

        break;
    }

    if (message.empty()) {
        throw std::runtime_error("websocket receive returned an empty message");
    }

    return message;
}

void KalshiWebSocket::close() {
    if (header_list_) {
        curl_slist_free_all(static_cast<curl_slist*>(header_list_));
        header_list_ = nullptr;
    }

    if (curl_) {
        curl_easy_cleanup(static_cast<CURL*>(curl_));
        curl_ = nullptr;
    }
}
