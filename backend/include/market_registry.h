#pragma once

#include <iostream>
#include <string>
#include <vector>

struct MarketInfo {
    std::string ticker;
    std::string event_ticker;
    std::string series_ticker;
    std::string title;
    std::string yes_sub_title;
    std::string competition_scope;
    bool mutually_exclusive = false;
    std::string outcome_key;
};

struct MarketEventGroup {
    std::string event_ticker;
    std::string series_ticker;
    std::string title;
    std::string competition_scope;
    bool mutually_exclusive = false;
    std::vector<MarketInfo> markets;
};

struct MatchGroup {
    std::string match_slug;
    std::string title;
    std::vector<MarketEventGroup> event_groups;
    std::vector<MarketInfo> all_markets;
};

class MarketRegistry {
public:
    static MarketRegistry load_mens_world_cup_matches();

    const std::vector<MatchGroup>& matches() const { return matches_; }
    std::vector<std::string> all_tickers() const;
    void print_summary(std::ostream& out) const;

private:
    std::vector<MatchGroup> matches_;
};

std::string series_role_label(const std::string& series_ticker);
