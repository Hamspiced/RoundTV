#pragma once
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include <cstdint>
namespace board {
constexpr int width=360,height=360;
constexpr int audio_bclk=48,audio_ws=38,audio_dout=47;
esp_lcd_panel_handle_t init_display();
void draw(int x1,int y1,int x2,int y2,const void *pixels);
esp_err_t mount_sd();
bool touch(int &x,int &y);
void backlight(bool on);
esp_err_t audio_init(uint32_t rate);
int audio_mclk();
void amplifier(bool enabled);
const char *revision();
}
