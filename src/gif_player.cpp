#include "gif_player.hpp"
#include "lvgl.h"
#include "src/extra/libs/gif/gifdec.h"
#include <algorithm>
#include <cstring>
static_assert(LV_COLOR_DEPTH==16 && LV_COLOR_16_SWAP==0,"RGB565 little-endian required");
namespace splash_render {
Player::~Player(){close();}
void Player::close(){
    if(decoder_) gd_close_gif(decoder_);
    if(pixels_) lv_mem_free(pixels_);
    decoder_=nullptr;pixels_=nullptr;failed_=false;late_=0;
}
uint32_t Player::delay()const{return std::max<uint32_t>(50,decoder_->gce.delay*10u);}
void Player::expand(){
    // Convert GIF's RGB565+alpha canvas once, outside LVGL's draw loop.
    // GIF alpha is binary; composite transparency on the screen background.
    const uint16_t bg=lv_color_hex(0x12151d).full;
    for(unsigned yy=0;yy<360;++yy){
        auto *dst=pixels_+yy*360;
        if(yy && y_[yy]==y_[yy-1]){std::memcpy(dst,dst-360,360*2);continue;}
        const auto *src=decoder_->canvas+unsigned(y_[yy])*decoder_->width*3;
        for(unsigned xx=0;xx<360;++xx){
            const auto *p=src+unsigned(x_[xx])*3;
            dst[xx]=p[2]?uint16_t(p[0]|uint16_t(p[1])<<8):bg;
        }
    }
}
bool Player::decode(){
    if(gd_get_frame(decoder_)!=1)return false;
    decoder_->loop_count=1;
    gd_render_frame(decoder_,decoder_->canvas);
    expand();return true;
}
bool Player::open(const void *bytes,uint32_t now){
    close();decoder_=gd_open_gif_data(bytes);if(!decoder_)return false;
    pixels_=static_cast<uint16_t*>(lv_mem_alloc(360*360*2));
    if(!pixels_){close();return false;}
    unsigned side=std::min(decoder_->width,decoder_->height);
    if(!side){close();return false;}
    unsigned left=(decoder_->width-side)/2,top=(decoder_->height-side)/2;
    for(unsigned i=0;i<360;++i){x_[i]=left+i*side/360;y_[i]=top+i*side/360;}
    if(!decode()){close();return false;}
    due_=now+delay();return true;
}
bool Player::restart(uint32_t now){
    if(!decoder_)return false;
    gd_rewind(decoder_);
    if(!decode()){failed_=true;return false;}
    failed_=false;due_=now+delay();return true;
}
Update Player::tick(uint32_t now){
    if(!decoder_ || failed_ || int32_t(now-due_)<0)return {};
    int result=gd_get_frame(decoder_);bool loop=false;
    if(result==0){
        loop=true;gd_rewind(decoder_);result=gd_get_frame(decoder_);
    }
    if(result!=1){failed_=true;return {false,false,true};}
    decoder_->loop_count=1;
    gd_render_frame(decoder_,decoder_->canvas);expand();
    const auto interval=delay();due_+=interval;
    // Absolute deadlines prevent decoding and rendering time accumulating
    // into every frame delay. Avoid monopolizing the UI with catch-up loops.
    if(int32_t(now-due_)>=0){++late_;due_=now+interval;}
    return {true,loop,false};
}
}
