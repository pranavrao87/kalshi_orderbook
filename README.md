# kalshi_orderbook

A C++ engine for maintaining a local Kalshi orderbook and detecting arbitrage opportunities within Kalshi markets.

## Scope

For now, this project is focused on Kalshi only. The goal is to:

1. **Maintain a local orderbook** — ingest Kalshi market data and keep an up-to-date view of bids, asks, and depth per market.
2. **Detect intra-Kalshi arbitrage** — find mispricings and structural opportunities across related Kalshi markets (e.g. complementary outcomes, linked events, or inconsistent implied probabilities).

Cross-platform arbitrage (e.g. Kalshi vs Polymarket) is out of scope for the current phase.

## Current status

Early prototype. The backend discovers open markets via REST, then connects to Kalshi's authenticated WebSocket feed to receive `orderbook_snapshot` and `orderbook_delta` messages and maintain a local orderbook per market.

## Credentials

WebSocket connections require Kalshi API credentials. Create a `.env` file from the template:

```bash
cp .env.example .env
```

Put your API key ID in `KALSHI_ACCESS_KEY` and point `KALSHI_PRIVATE_KEY_PATH` at a separate PEM file (not the `.env` file itself). Then load it:

```bash
set -a && source .env && set +a
```

The app reads environment variables only; it does not parse multi-line PEM content inside `.env`.

Optional overrides:

```bash
export KALSHI_WS_URL="wss://external-api-ws.kalshi.com/trade-api/ws/v2"
export KALSHI_SERIES_TICKER="KXWCADVANCE"
```

See `config/settings.yaml` for a template.

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
export KALSHI_ACCESS_KEY="your_api_key_id"
export KALSHI_PRIVATE_KEY_PATH="/path/to/kalshi_private_key.pem"
./backend/build/kalshi_orderbook
```

This connects to Kalshi's WebSocket API, subscribes to `orderbook_delta` for all open markets in the configured series, prints the initial snapshot for each market, and logs incremental updates until you press `Ctrl+C`.

## Roadmap

- [x] Subscribe to Kalshi orderbook / market data feeds
- [x] Maintain per-market bid/ask books locally
- [x] Add executable pricing helpers (best bid/ask, implied probability)
- [ ] Define arbitrage rules for related Kalshi markets
- [ ] Surface opportunities (logs, alerts, or frontend)
