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

WebSocket connections require Kalshi API credentials. Put them in a `.env` file at the repo root (already gitignored):

```bash
KALSHI_ACCESS_KEY=your_api_key_id
KALSHI_PRIVATE_KEY_PATH=/absolute/path/to/secrets/kalshi_private_key.pem
KALSHI_MIN_ARB_EDGE=0.01
```

The app loads `.env` automatically when you run it — no need to `export` or `source` each time. If you run from `backend/`, it will find `../.env`.

## Configuration

Non-secret runtime settings live in `config/settings.yaml`:

```yaml
kalshi:
  ws_url: wss://external-api-ws.kalshi.com/trade-api/ws/v2
  min_arb_edge: 0.01

engine:
  log_level: info
  reconnect_initial_ms: 1000
  reconnect_max_ms: 30000
  scan_interval_ms: 0
  pricing_summary_series: KXWCADVANCE,KXWCGAME,KXWCSCORE,KXWCTOTAL,KXWCMOV
```

Environment variables still override config when set:

```bash
KALSHI_WS_URL=...
KALSHI_MIN_ARB_EDGE=0.01
KALSHI_LOG_LEVEL=debug
```

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

This connects to Kalshi's WebSocket API, maintains local orderbooks, and continuously scans for fee-aware arbitrage:

- `mutually_exclusive` — sum of YES asks across a complete outcome set < $1
- `yes_no_parity` — YES ask + NO ask < $1 on the same market
- `score_to_ml` — regulation correct-score basket cheaper than moneyline outcome
- `score_to_btts` — both-teams-score scorelines cheaper than BTTS Yes
- `score_to_total` — scorelines implying Over X.5 cheaper than that totals market

Fees use Kalshi's taker formula `round_up(0.07 × P × (1-P))` per contract (configurable via `taker_fee_coeff`). Score-basket rules are relative-value signals because Kalshi's correct-score set is incomplete (not every possible score is listed). Press `Ctrl+C` to stop and print a pricing summary.

## Roadmap

- [x] Subscribe to Kalshi orderbook / market data feeds
- [x] Maintain per-market bid/ask books locally
- [x] Add executable pricing helpers (best bid/ask, implied probability)
- [x] Define arbitrage rules for related Kalshi markets (mutually exclusive, YES/NO parity, score baskets)
- [x] Surface opportunities via live logs during streaming
- [x] Fee-aware edge calculations (Kalshi taker fee)
- [x] Orderbook sequence validation and resnapshot on gaps
- [x] WebSocket auto-reconnect with exponential backoff
- [x] Engine orchestration (`engine.cpp`)
- [x] Config file support (`config/settings.yaml`)
