#pragma once

struct Counts {
    int constructed = 0;
    int moved       = 0;
    int dropped     = 0;
};

struct alignas(64) Tracked {
    Counts* counts;
    int     value;

    Tracked(Counts& counts, int value): counts(&counts), value(value) { ++counts.constructed; }
    Tracked(const Tracked&) = delete;
    Tracked(Tracked&& other) noexcept: counts(other.counts), value(other.value) {
        ++counts->moved;
        other.counts = nullptr;
    }
    ~Tracked() {
        if (counts) ++counts->dropped;
    }
};
