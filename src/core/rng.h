#pragma once
#include <esp_random.h>
#include <stdint.h>

namespace rng {
// Unbiased integer in [1, sides] from the ESP32 hardware RNG.
inline int roll(uint32_t sides) {
    if (sides <= 1) return 1;
    uint32_t limit = UINT32_MAX - (UINT32_MAX % sides);
    uint32_t r;
    do { r = esp_random(); } while (r >= limit);
    return (int)(r % sides) + 1;
}
// Integer in [0, n)
inline int below(uint32_t n) { return n ? roll(n) - 1 : 0; }
inline float unit() { return (esp_random() >> 8) * (1.0f / 16777216.0f); }
template <typename T, size_t N> inline const T& pick(const T (&arr)[N]) { return arr[below(N)]; }
}  // namespace rng
