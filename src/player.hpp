#pragma once
#include <string>
#include <cstdint>
namespace player {
constexpr int default_volume_percent=50;
constexpr int video_w=360, video_h=360, window_h=360;
enum class State { idle, starting, playing, paused, ended, failed };
struct Status {
    State state=State::idle;
    uint32_t token=0, elapsed_ms=0, duration_ms=0, dropped=0, audio_peak=0;
    uint32_t video_frames=0, buffered_ms=0, read_max_ms=0, video_stage=0;
    std::string error;
};
void init();
// UI thread owns these calls; starting a new title cancels and joins both old workers.
uint32_t play(const std::string &path);
uint32_t test_sound(); // one-second generated tone; independent of SD/media
void stop();
void pause(bool paused);
void volume(int percent);
void mute(bool muted);
Status status();
// Copy into UI-owned buffer. Decoder never writes LVGL's live image storage.
bool copy_frame(uint16_t *destination);
}
