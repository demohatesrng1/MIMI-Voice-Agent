#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace mimi::brain {

struct Event {
    std::string time;  // ISO-8601, local
    std::string kind;  // "chat", "open_site", "search", "page", …
    nlohmann::json record;
};

// A local, append-only log of what happened, one JSONL file per day under
// <data>/journal/. It is the raw material the daily reflection reads, and it
// never leaves the machine.
class Journal {
public:
    Journal();

    void log(const std::string& kind, nlohmann::json record);

    // Today by default; pass an ISO date ("2026-07-29") for another day.
    std::vector<Event> read_day(const std::string& day = {}) const;
    std::vector<Event> read_all() const;
    std::vector<std::string> days() const;

    // Deletes one day's file. Defaults to today.
    //
    // The Python original read *every* day and then deleted *every* file, so
    // running the reflection twice silently destroyed the backlog. Scoping both
    // ends to a single day is the fix.
    bool clear_day(const std::string& day = {});

    // Deletes journal days older than `keep_days`. Returns how many files went.
    //
    // Everything Mimi hears is written here, so "it never leaves the machine"
    // is only half a privacy answer -- the other half is that it does not sit
    // on the machine forever either. Without this the log grows without limit
    // and every day of it stays readable indefinitely.
    int prune(int keep_days = 90);

    static std::string today();
};

}  // namespace mimi::brain
