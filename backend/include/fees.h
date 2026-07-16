#pragma once

#include <cmath>

// Kalshi general taker fee: round_up(coeff * C * P * (1-P)) to the next cent.
inline double kalshi_taker_fee_per_contract(double price, double coeff = 0.07) {
    if (price <= 0.0 || price >= 1.0 || coeff <= 0.0) {
        return 0.0;
    }

    const double raw = coeff * price * (1.0 - price);
    return std::ceil(raw * 100.0 - 1e-12) / 100.0;
}

inline double all_in_buy_cost(double price, double fee_coeff = 0.07) {
    return price + kalshi_taker_fee_per_contract(price, fee_coeff);
}
