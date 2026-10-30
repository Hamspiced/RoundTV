#include "video_pacing.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    player::VideoPacing normal;
    for(uint32_t frame=0;frame<24*30*60;++frame) {
        const uint64_t pts=uint64_t(frame)*1000000/24;
        assert(!normal.drop(pts,pts));
        normal.displayed(pts);
    }
    puts("PASS: 30 minutes of on-time video, no dropped pictures");

    player::VideoPacing lag;
    uint32_t r6_after=0,r7_after=0,dropped=0;
    uint64_t last=0,max_gap=0;
    for(uint32_t frame=0;frame<24*20*60;++frame) {
        const uint64_t pts=uint64_t(frame)*1000000/24;
        const bool delayed=frame>=24*10*60;
        const uint64_t audio=pts+(delayed?500000:0);
        if(delayed && !(audio>pts+100000))++r6_after;
        if(lag.drop(pts,audio)) { ++dropped; continue; }
        if(delayed) {
            ++r7_after;
            if(last) {
                const uint64_t gap=audio-last;
                if(gap>max_gap)max_gap=gap;
                assert(gap>=250000 && gap<=291667);
            }
            last=audio;
        }
        lag.displayed(audio);
    }
    assert(r6_after==0 && r7_after>2000 && dropped>0);
    printf("PASS: persistent 500 ms lag after ten minutes: R6 publishes %u, R7 publishes %u; simulated maximum steady-state gap %llu us\n",
        r6_after,r7_after,static_cast<unsigned long long>(max_gap));

    player::VideoPacing edges;
    assert(!edges.drop(0,1000000)); // First picture is always permitted.
    edges.displayed(1000000);
    assert(!edges.drop(900000,1000000)); // Exactly 100 ms late.
    assert(edges.drop(899999,1000000));
    assert(edges.drop(0,1249999));
    assert(!edges.drop(0,1250000)); // Exactly 250 ms since publication.
    assert(!edges.drop(0,999999)); // Audio clock moved backward.
    edges.displayed(999999);
    assert(edges.drop(0,1000000));

    const uint64_t large=(uint64_t(1)<<50)+123456;
    player::VideoPacing wide;
    assert(!wide.drop(large,large+500000));
    wide.displayed(large+500000);
    assert(wide.drop(large,large+749999));
    assert(!wide.drop(large,large+750000));
    assert(!wide.drop(large,large+499999));
    puts("PASS: first picture, late/drop boundaries, backward audio clock, and 64-bit timestamps");
}
