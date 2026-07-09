# kalshi_orderbook

A C++ engine for maintaining a local Kalshi orderbook and detecting arbitrage opportunities within Kalshi markets.

## Scope

For now, this project is focused on Kalshi only. The goal is to:

1. **Maintain a local orderbook** — ingest Kalshi market data and keep an up-to-date view of bids, asks, and depth per market.
2. **Detect intra-Kalshi arbitrage** — find mispricings and structural opportunities across related Kalshi markets (e.g. complementary outcomes, linked events, or inconsistent implied probabilities).

Cross-platform arbitrage (e.g. Kalshi vs Polymarket) is out of scope for the current phase.

## Current status

Early prototype. The backend currently fetches open markets and event metadata from the Kalshi REST API. Orderbook state and arbitrage logic are not implemented yet.

## Project layout

```
backend/     C++ engine (CMake)
  src/       Application entry point and Kalshi API client
  include/   Public headers
config/      Runtime configuration (settings.yaml)
frontend/    Reserved for future UI / tooling
```

## Build

Requires CMake 3.20+, a C++20 compiler, and Homebrew packages on macOS:

```bash
brew install curl openssl spdlog nlohmann-json
```

From the repo root:

```bash
cd backend
cmake -S . -B build
cmake --build build
```

After renaming or moving the project, do a clean rebuild:

```bash
rm -rf build
cmake -S . -B build
cmake --build build
```

## Run

```bash
./backend/build/kalshi_orderbook
```

This runs a smoke test that lists active markets in the `KXWCADVANCE` series and prints details for the first event.

## Roadmap

- [ ] Subscribe to Kalshi orderbook / market data feeds
- [ ] Maintain per-market bid/ask books locally
- [ ] Define arbitrage rules for related Kalshi markets
- [ ] Surface opportunities (logs, alerts, or frontend)
