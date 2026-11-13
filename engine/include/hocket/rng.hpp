// The frozen arithmetic (SPEC §2.6): the hash that names every decision, the stream it seeds, Lemire's unbiased
// `below`, and integer rounding. Changing anything here changes every song: bump engineVersion and keep the old path.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace hocket {

constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ull;

// SplitMix64's finalizer.
constexpr std::uint64_t mix(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// FNV-1a 64 over the bytes of a UTF-8 string.
constexpr std::uint64_t fnv1a(std::string_view s) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (unsigned char c : s) h = (h ^ c) * 0x100000001B3ull;
    return h;
}

// One path component: an integer (kind 1) or a string (kind 2).
struct Part {
    std::uint64_t value;
    std::uint64_t kind;
    constexpr Part(std::int64_t v) : value(static_cast<std::uint64_t>(v)), kind(1) {}
    constexpr Part(std::uint64_t v) : value(v), kind(1) {}
    constexpr Part(int v) : value(static_cast<std::uint64_t>(static_cast<std::int64_t>(v))), kind(1) {}
    constexpr Part(std::string_view s) : value(fnv1a(s)), kind(2) {}
    constexpr Part(const char* s) : value(fnv1a(std::string_view(s))), kind(2) {}
};

constexpr std::uint64_t hash(std::uint64_t seed, std::initializer_list<Part> path) {
    std::uint64_t h = mix(seed);
    for (const Part& p : path) h = mix(mix(h ^ p.value) + p.kind);
    return h;
}

// The high and low 64 bits of a 64×64 product, the same on every compiler (no __int128, no _umul128).
inline void mul64(std::uint64_t a, std::uint64_t b, std::uint64_t& hi, std::uint64_t& lo) {
    const std::uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32, b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const std::uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    lo = (mid << 32) | (p00 & 0xFFFFFFFFu);
    hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

// A SplitMix64 generator seeded by a hash.
class Stream {
public:
    explicit Stream(std::uint64_t h) : state_(h) {}
    std::uint64_t next() {
        state_ += kGolden;
        return mix(state_);
    }
    // Uniform in 0…n−1 (n ≥ 1), Lemire's method with rejection.
    std::uint64_t below(std::uint64_t n) {
        std::uint64_t hi, lo;
        mul64(next(), n, hi, lo);
        if (lo < n) {
            const std::uint64_t t = (0 - n) % n;
            while (lo < t) mul64(next(), n, hi, lo);
        }
        return hi;
    }

private:
    std::uint64_t state_;
};

// A percentage roll, 0…99.
inline int roll(std::uint64_t h) { return static_cast<int>(Stream(h).below(100)); }

// Round half up: a ≥ 0, b > 0.
constexpr std::int64_t rdiv(std::int64_t a, std::int64_t b) { return (2 * a + b) / (2 * b); }

}  // namespace hocket
