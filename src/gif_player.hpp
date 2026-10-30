#pragma once
#include <cstdint>
struct gd_GIF;
namespace splash_render {
struct Update { bool frame=false,loop=false,error=false; };
class Player {
public:
    Player()=default;
    Player(const Player&)=delete;
    Player& operator=(const Player&)=delete;
    ~Player();
    bool open(const void *bytes,uint32_t now);
    void close();
    Update tick(uint32_t now);
    bool restart(uint32_t now);
    const uint16_t *pixels() const { return pixels_; }
    uint32_t late_frames() const { return late_; }
private:
    bool decode();
    void expand();
    uint32_t delay() const;
    gd_GIF *decoder_=nullptr;
    uint16_t *pixels_=nullptr;
    uint16_t x_[360]{}, y_[360]{};
    uint32_t due_=0,late_=0;
    bool failed_=false;
};
}
