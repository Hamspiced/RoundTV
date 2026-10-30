#pragma once
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

// Deliberately constrained AVI reader: classic RIFF AVI, one MJPEG video stream
// followed by mono PCM16 48000Hz. OpenDML/AVIX and compressed audio are rejected.
// Host-testable; all chunk arithmetic is checked before seeking or allocation.
namespace media {
constexpr uint32_t max_jpeg = 192 * 1024;
struct Chunk { uint32_t id=0, size=0, data=0, end=0; };
struct Info {
    uint32_t width=0, height=0, scale=0, rate=0, frames=0, samples=0;
    uint32_t movi=0, movi_end=0;
    uint64_t frame_us(uint32_t n) const { return uint64_t(n)*scale*1000000/rate; }
};
uint32_t fourcc(const char *s);
class Avi {
    FILE *f=nullptr;
    uint32_t file_end=0, position=0;
    std::vector<uint32_t> list_ends;
    bool chunk_at(uint32_t pos, uint32_t limit, Chunk &c);
    bool header_list(uint32_t begin, uint32_t end, unsigned depth, int &stream);
    bool stream_list(uint32_t begin, uint32_t end, int stream);
public:
    Info info;
    std::string error;
    ~Avi();
    bool open(const std::string &path);
    // 1 = matching chunk, 0 = EOF, -1 = malformed/truncated.
    int next(bool audio, Chunk &c);
    bool read(uint32_t offset, void *dst, size_t size);
};
class PcmReader {
    Avi avi;
    Chunk chunk;
    uint32_t used=0, consumed=0;
public:
    bool open(const std::string &path) { return avi.open(path); }
    const Info &info() const { return avi.info; }
    const std::string &error() const { return avi.error; }
    // Returns samples; -1 signals malformed/truncated audio.
    int read(int16_t *out, unsigned count);
};
}
