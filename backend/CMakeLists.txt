cmake_minimum_required(VERSION 3.20)
project(kalshi_orderbook LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
if(APPLE)
  set(CURL_ROOT "/opt/homebrew/opt/curl")
  set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl@3")
endif()
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
add_executable(kalshi_orderbook
  src/main.cpp
  src/kalshi_rest.cpp
  src/kalshi_auth.cpp
  src/kalshi_ws.cpp
  src/orderbook.cpp
  src/market_registry.cpp
  src/arb_detector.cpp
)
target_link_libraries(kalshi_orderbook PRIVATE CURL::libcurl OpenSSL::SSL OpenSSL::Crypto spdlog::spdlog nlohmann_json::nlohmann_json)
target_include_directories(kalshi_orderbook PRIVATE include)
