#pragma once
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace player {
// One SD producer and one audio consumer. Payload is in PSRAM; indices are
// published only after the corresponding copy completes.
class PcmQueue {
    int16_t *data_;
    uint32_t capacity_;
    std::atomic<uint32_t> read_{0}, write_{0};
public:
    PcmQueue(int16_t *data, uint32_t capacity):data_(data),capacity_(capacity){}
    uint32_t available() const { return write_.load(std::memory_order_acquire)-read_.load(std::memory_order_acquire); }
    uint32_t free_space() const { return capacity_-available(); }
    bool push(const int16_t *src,uint32_t n) {
        const uint32_t w=write_.load(std::memory_order_relaxed);
        if(n>capacity_-(w-read_.load(std::memory_order_acquire)))return false;
        const uint32_t at=w%capacity_, first=std::min(n,capacity_-at);
        memcpy(data_+at,src,first*sizeof(int16_t));
        memcpy(data_,src+first,(n-first)*sizeof(int16_t));
        write_.store(w+n,std::memory_order_release);return true;
    }
    uint32_t pop(int16_t *dst,uint32_t limit) {
        const uint32_t r=read_.load(std::memory_order_relaxed);
        const uint32_t n=std::min(limit,write_.load(std::memory_order_acquire)-r);
        const uint32_t at=r%capacity_, first=std::min(n,capacity_-at);
        memcpy(dst,data_+at,first*sizeof(int16_t));
        memcpy(dst+first,data_,(n-first)*sizeof(int16_t));
        read_.store(r+n,std::memory_order_release);return n;
    }
};
}
