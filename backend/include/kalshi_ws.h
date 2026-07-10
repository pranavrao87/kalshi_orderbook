#pragma once

#include <map>
#include <string>

class KalshiWebSocket {
public:
    explicit KalshiWebSocket(std::string url);
    ~KalshiWebSocket();

    KalshiWebSocket(const KalshiWebSocket&) = delete;
    KalshiWebSocket& operator=(const KalshiWebSocket&) = delete;

    void connect(const std::map<std::string, std::string>& headers);
    void send_text(const std::string& message);
    std::string receive_text();
    void close();

private:
    std::string url_;
    void* curl_ = nullptr;
    void* header_list_ = nullptr;
};
