#pragma once
#include <cstdint>

namespace player {
class VideoPacing {
    uint64_t last_presented_us = 0;
    bool presented = false;

public:
    bool drop(uint64_t pts, uint64_t audio_us) const {
        return presented &&
               audio_us > pts + 100000 &&
               audio_us >= last_presented_us &&
               audio_us - last_presented_us < 250000;
    }

    void displayed(uint64_t audio_us) {
        last_presented_us = audio_us;
        presented = true;
    }
};
}
