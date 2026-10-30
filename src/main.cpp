#include "board.hpp"
#include "player.hpp"
#include "catalog.hpp"
#include "splash.hpp"
#include "lvgl.h"
#include "gif_player.hpp"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

static esp_lcd_panel_handle_t panel;
static lv_obj_t *gif=nullptr,*video=nullptr,*menu=nullptr,*heading=nullptr,*notice=nullptr;
static lv_img_dsc_t video_image{},gif_image{};
static lv_obj_t *audio_status=nullptr;
static std::vector<uint8_t> gif_bytes;
static splash_render::Player splash_player;
static uint32_t splash_generation=0,splash_flushed=0,splash_flushes=0,splash_steps=0;
static uint64_t splash_work_us=0,splash_stats_at=0;
static float splash_fps=0,splash_ms=0;
static bool splash_running=false;
static std::vector<catalog::Season> seasons;
struct Selection { size_t season,episode; };
static std::vector<Selection> playlist;
static size_t playlist_index=0,selected_season=0;
static uint32_t player_token=0;
static uint64_t intermission_at=0;
static bool waiting=false,loop_finished=false,menu_visible=true,screen_off=false;
static bool want_controls=false,paused=false,sd_ok=false;
static bool startup_waiting=true;
static bool testing_sound=false;
static int volume_percent=player::default_volume_percent;
static std::string problem;
static uint64_t ms() { return esp_timer_get_time()/1000; }

static void flush(lv_disp_drv_t *drv,const lv_area_t *area,lv_color_t *color) {
    // ST77916 accepts high byte first; LVGL/JPEG work in native little endian.
    const size_t count=size_t(area->x2-area->x1+1)*(area->y2-area->y1+1);
    for(size_t i=0;i<count;++i) color[i].full=uint16_t((color[i].full<<8)|(color[i].full>>8));
    board::draw(area->x1,area->y1,area->x2+1,area->y2+1,color);
    if(lv_disp_flush_is_last(drv) && splash_flushed!=splash_generation){
        splash_flushed=splash_generation;++splash_flushes;
    }
    lv_disp_flush_ready(drv);
}
static void show_controls();
static void show_seasons();
static void show_episodes();
static void set_menu(bool visible) {
    menu_visible=visible;
    if(visible) { lv_obj_clear_flag(menu,LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(heading,LV_OBJ_FLAG_HIDDEN); }
    else { lv_obj_add_flag(menu,LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(heading,LV_OBJ_FLAG_HIDDEN); }
}
static void touch_read(lv_indev_drv_t *,lv_indev_data_t *data) {
    static bool waking=false;
    int x=0,y=0; bool down=board::touch(x,y);
    if(down && screen_off) {
        screen_off=false; board::backlight(true); player::mute(false); waking=true;
    }
    if(!down) waking=false;
    data->point.x=x; data->point.y=y;
    data->state=(down && !waking)?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
static void background_tap(lv_event_t *) {
    if(!menu_visible) want_controls=true;
}
static void splash_changed(){
    ++splash_generation;lv_img_cache_invalidate_src(&gif_image);lv_obj_invalidate(gif);
}
static void restart_gif() {
    if(!gif)return;
    if(splash_player.restart(uint32_t(ms()))){splash_running=true;splash_changed();}
    else {splash_running=false;problem="Splash decoder stopped";}
}
static void load_splash() {
    if(gif){lv_obj_del(gif);gif=nullptr;}
    lv_img_cache_invalidate_src(&gif_image);
    splash_player.close();splash_running=false;gif_bytes.clear();
    FILE *f=fopen("/sdcard/splash/splash.gif","rb");
    if(!f){problem="Missing /splash/splash.gif";return;}
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    if(size>2*1024*1024){fclose(f);problem="Splash exceeds 2 MiB; use the converter";return;}
    if(size>0){gif_bytes.resize(size);if(fread(gif_bytes.data(),1,size,f)!=size)gif_bytes.clear();}
    fclose(f);
    std::string err;
    if(!validate_gif(gif_bytes,err)){problem=err;gif_bytes.clear();return;}
    if(!splash_player.open(gif_bytes.data(),uint32_t(ms()))){
        problem="Splash decoder or frame allocation failed";gif_bytes.clear();return;
    }
    gif_image={};gif_image.header.cf=LV_IMG_CF_TRUE_COLOR;
    gif_image.header.w=360;gif_image.header.h=360;gif_image.data_size=360*360*2;
    gif_image.data=reinterpret_cast<const uint8_t*>(splash_player.pixels());
    gif=lv_img_create(lv_scr_act());lv_obj_remove_style_all(gif);
    lv_img_set_src(gif,&gif_image);lv_obj_set_pos(gif,0,0);lv_obj_move_background(gif);
    lv_obj_add_flag(gif,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(gif,background_tap,LV_EVENT_CLICKED,nullptr);
    splash_running=true;splash_changed();
    splash_flushes=splash_steps=0;splash_work_us=0;splash_stats_at=ms();
    splash_fps=splash_ms=0;loop_finished=false;
}
static void begin_selected() {
    if(playlist.empty()) return;
    testing_sound=false;
    auto choice=playlist[playlist_index];
    const auto &episode=seasons[choice.season].episodes[choice.episode];
    player_token=player::play(episode.path);
    memset(const_cast<uint8_t *>(video_image.data),0,video_image.data_size);
    lv_obj_clear_flag(video,LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(video);
    startup_waiting=false; splash_running=false;
    if(gif)lv_obj_add_flag(gif,LV_OBJ_FLAG_HIDDEN);
    waiting=false; paused=false; problem.clear();
    lv_label_set_text(notice,"");
}
static void intermission() {
    waiting=true; intermission_at=ms();
    lv_obj_add_flag(video,LV_OBJ_FLAG_HIDDEN);
    // Start a fresh splash loop; wait at least five seconds plus a loop boundary.
    if(gif){lv_obj_clear_flag(gif,LV_OBJ_FLAG_HIDDEN);restart_gif();}
    loop_finished=false;
}
static void shuffle_playlist() {
    for(size_t i=playlist.size();i>1;--i) std::swap(playlist[i-1],playlist[esp_random()%i]);
}
static void season_playlist(size_t start,bool shuffle) {
    playlist.clear();
    for(size_t i=0;i<seasons[selected_season].episodes.size();++i) playlist.push_back({selected_season,i});
    if(shuffle) shuffle_playlist();
    playlist_index=shuffle?0:start;
    set_menu(false); begin_selected();
}
enum { BACK=1, CLOSE, ORDERED, SHUFFLE_SEASON, SHUFFLE_ALL, REFRESH, PAUSE, NEXT, VOL_DOWN, VOL_UP, SCREEN_OFF, PREVIOUS, SOUND_TEST,
       SEASON_BASE=1000, EPISODE_BASE=2000 };
static void action(lv_event_t *event) {
    int code=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
    if(code>=EPISODE_BASE) { season_playlist(code-EPISODE_BASE,false); return; }
    if(code>=SEASON_BASE) { selected_season=code-SEASON_BASE; show_episodes(); return; }
    switch(code) {
    case BACK: show_seasons(); break;
    case CLOSE: set_menu(false); break;
    case ORDERED: season_playlist(0,false); break;
    case SHUFFLE_SEASON: season_playlist(0,true); break;
    case SHUFFLE_ALL:
        playlist.clear();
        for(size_t s=0;s<seasons.size();++s)
            for(size_t e=0;e<seasons[s].episodes.size();++e) playlist.push_back({s,e});
        shuffle_playlist(); playlist_index=0; set_menu(false); begin_selected(); break;
    case REFRESH:
        // Stop is asynchronous. Defer rescan until both media tasks have released their files.
        player::stop(); player_token=0; waiting=false; playlist.clear();
        lv_obj_add_flag(video,LV_OBJ_FLAG_HIDDEN); problem="Refreshing...";
        want_controls=false;
        break;
    case PAUSE:
        if(!playlist.empty() && (waiting || player::status().state==player::State::idle || player::status().state==player::State::ended || player::status().state==player::State::failed)) {
            testing_sound=false;set_menu(false);begin_selected();
        } else {paused=!paused;player::pause(paused);show_controls();}
        break;
    case SOUND_TEST:
        testing_sound=true;startup_waiting=false;waiting=false;paused=false;problem.clear();
        player::mute(false);player_token=player::test_sound();
        lv_obj_add_flag(video,LV_OBJ_FLAG_HIDDEN);
        if(gif){lv_obj_clear_flag(gif,LV_OBJ_FLAG_HIDDEN);restart_gif();}
        show_controls();break;
    case PREVIOUS:
    case NEXT:
        if(!playlist.empty()) {
            player::stop(); player_token=0;
            playlist_index=(playlist_index+playlist.size()+(code==NEXT?1:-1))%playlist.size();
            testing_sound=false;set_menu(false);begin_selected();
        }
        break;
    case VOL_DOWN: volume_percent=std::max(0,volume_percent-10); player::volume(volume_percent); show_controls(); break;
    case VOL_UP: volume_percent=std::min(100,volume_percent+10); player::volume(volume_percent); show_controls(); break;
    case SCREEN_OFF:
        set_menu(false); screen_off=true; player::mute(true); board::backlight(false); break;
    }
}
static void clear_menu(const std::string &title) {
    audio_status=nullptr;lv_obj_clean(menu);
    lv_obj_set_style_text_color(menu,lv_color_hex(0xeee5cd),0);
    lv_obj_set_size(menu,286,228);lv_obj_set_pos(menu,37,70);
    lv_obj_set_style_pad_all(menu,10,0);lv_obj_set_style_pad_row(menu,8,0);
    lv_obj_set_flex_flow(menu,LV_FLEX_FLOW_COLUMN);lv_obj_add_flag(menu,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(menu,LV_SCROLLBAR_MODE_AUTO);lv_obj_set_scroll_dir(menu,LV_DIR_VER);lv_obj_scroll_to_y(menu,0,LV_ANIM_OFF);
    lv_label_set_text(heading,title.c_str()); set_menu(true);
}
static void row(const std::string &text,int code) {
    auto *btn=lv_btn_create(menu);
    lv_obj_set_width(btn,LV_PCT(100)); lv_obj_set_height(btn,46);
    lv_obj_set_style_bg_color(btn,lv_color_hex(0x373b46),0);
    lv_obj_set_style_radius(btn,9,0);
    auto *label=lv_label_create(btn);
    lv_obj_set_width(label,LV_PCT(100)); lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
    lv_label_set_text(label,text.c_str()); lv_obj_center(label);
    lv_obj_add_event_cb(btn,action,LV_EVENT_CLICKED,reinterpret_cast<void *>(intptr_t(code)));
}
static void text_row(const std::string &text) {
    auto *label=lv_label_create(menu); lv_obj_set_width(label,LV_PCT(100));
    lv_label_set_text(label,text.c_str());
}
static void show_seasons() {
    clear_menu("SEASONS");
    if(!problem.empty()) text_row(problem);
    if(!sd_ok) text_row("SD card unavailable. Insert FAT32 card and restart.");
    else if(seasons.empty()) text_row("No episodes found. Use videos/seasons/01/01 - Name/video.avi");
    if(!seasons.empty()) row("Shuffle all seasons",SHUFFLE_ALL);
    for(size_t s=0;s<seasons.size();++s) row("Season "+seasons[s].name,SEASON_BASE+s);
    if(sd_ok) row("Refresh library",REFRESH);
    row("Return to screen",CLOSE);
}
static void show_episodes() {
    clear_menu("SEASON "+seasons[selected_season].name);
    row("Back to seasons",BACK); row("Play season in order",ORDERED); row("Shuffle season",SHUFFLE_SEASON);
    for(size_t e=0;e<seasons[selected_season].episodes.size();++e)
        row(seasons[selected_season].episodes[e].title,EPISODE_BASE+e);
}
static void update_audio_status(const player::Status &state) {
    static const char *stages[]={"WAIT","READ","DEC","SHOW","END"};
    const char *stage=state.video_stage<5?stages[state.video_stage]:"?";
    char text[80];snprintf(text,sizeof(text),"%d%% B%lus V%lu %s",volume_percent,
        static_cast<unsigned long>(state.buffered_ms/1000),
        static_cast<unsigned long>(state.video_frames),stage);
    lv_label_set_text(audio_status,text);
}
static void show_controls() {
    clear_menu("ROUND TV R7");
    lv_obj_set_size(menu,274,224);lv_obj_set_pos(menu,43,83);
    lv_obj_set_layout(menu,0);lv_obj_set_style_pad_all(menu,0,0);
    lv_obj_clear_flag(menu,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scrollbar_mode(menu,LV_SCROLLBAR_MODE_OFF);
    const auto state=player::status();
    const bool has_error=!problem.empty() && !testing_sound;
    auto *title=lv_label_create(menu);lv_obj_set_pos(title,8,0);lv_obj_set_size(title,258,has_error?60:20);
    lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);lv_label_set_long_mode(title,has_error?LV_LABEL_LONG_WRAP:LV_LABEL_LONG_DOT);
    std::string caption=testing_sound?"Speaker test":(!problem.empty()?problem:board::revision());
    if(!testing_sound && problem.empty() && !playlist.empty()) {
        auto choice=playlist[playlist_index];caption=seasons[choice.season].episodes[choice.episode].title;
    }
    lv_label_set_text(title,caption.c_str());
    auto button=[&](int col,int line,const char *text,int code,bool enabled=true){
        auto *btn=lv_btn_create(menu);lv_obj_set_pos(btn,col*94,(has_error?64:25)+line*(has_error?44:58));lv_obj_set_size(btn,86,has_error?40:50);
        lv_obj_set_style_pad_all(btn,2,0);lv_obj_set_style_radius(btn,9,0);
        lv_obj_set_style_bg_color(btn,lv_color_hex(code==CLOSE?0x28685c:0x373b46),0);
        auto *label=lv_label_create(btn);lv_label_set_text(label,text);lv_obj_center(label);
        lv_obj_add_event_cb(btn,action,LV_EVENT_CLICKED,reinterpret_cast<void *>(intptr_t(code)));
        if(!enabled)lv_obj_add_state(btn,LV_STATE_DISABLED);
    };
    const bool has_episode=!playlist.empty();
    const bool playing=state.state==player::State::playing || state.state==player::State::starting || state.state==player::State::paused;
    button(0,0,"Previous",PREVIOUS,has_episode);
    button(1,0,paused?"Resume":(playing&&!testing_sound?"Pause":"Play"),PAUSE,has_episode&&!testing_sound);
    button(2,0,"Next",NEXT,has_episode);
    button(0,1,"Vol -",VOL_DOWN);button(1,1,"Browse",BACK);button(2,1,"Vol +",VOL_UP);
    button(0,2,"Test tone",SOUND_TEST,!testing_sound);button(1,2,"Close",CLOSE);button(2,2,"Screen off",SCREEN_OFF);
    auto *status=lv_label_create(menu);audio_status=status;lv_obj_set_pos(status,20,199);lv_obj_set_size(status,234,20);
    lv_obj_set_style_text_align(status,LV_TEXT_ALIGN_CENTER,0);
    update_audio_status(state);
}

extern "C" void app_main() {
    esp_log_level_set("*",ESP_LOG_NONE); // keep JPEG's linked log ABI, but no runtime logging
    panel=board::init_display();
    sd_ok=board::mount_sd()==ESP_OK;
    player::init(); player::volume(volume_percent); lv_init();
    // Hot draw buffer in internal RAM; the large immutable image stays in PSRAM.
    auto *draw=static_cast<lv_color_t *>(heap_caps_malloc(360*40*sizeof(lv_color_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));

    configASSERT(draw);
    static lv_disp_draw_buf_t draw_buffer;
    lv_disp_draw_buf_init(&draw_buffer,draw,nullptr,360*40);
    static lv_disp_drv_t display;
    lv_disp_drv_init(&display); display.hor_res=360; display.ver_res=360;
    display.draw_buf=&draw_buffer; display.flush_cb=flush; lv_disp_drv_register(&display);
    static lv_indev_drv_t touch;
    lv_indev_drv_init(&touch); touch.type=LV_INDEV_TYPE_POINTER; touch.read_cb=touch_read;
    lv_indev_drv_register(&touch);
    auto *root=lv_scr_act();
    lv_obj_set_style_bg_color(root,lv_color_hex(0x12151d),0);
    lv_obj_set_style_text_color(root,lv_color_hex(0xeee5cd),0);
    lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(root,background_tap,LV_EVENT_CLICKED,nullptr);
    if(sd_ok) load_splash();

    // Media is center-cropped on the computer to native 360x360; no runtime scaling.
    video=lv_obj_create(root); lv_obj_remove_style_all(video);
    lv_obj_set_size(video,360,360); lv_obj_set_pos(video,0,0);
    lv_obj_set_style_bg_color(video,lv_color_black(),0); lv_obj_set_style_bg_opa(video,LV_OPA_COVER,0);
    lv_obj_add_flag(video,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(video,background_tap,LV_EVENT_CLICKED,nullptr);
    lv_obj_clear_flag(video,LV_OBJ_FLAG_SCROLLABLE);
    video_image.header.cf=LV_IMG_CF_TRUE_COLOR; video_image.header.w=player::video_w;
    video_image.header.h=player::video_h; video_image.data_size=360*360*2;
    video_image.data=static_cast<uint8_t *>(heap_caps_calloc(1,video_image.data_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    configASSERT(video_image.data);
    auto *image=lv_img_create(video); lv_img_set_src(image,&video_image); lv_obj_set_pos(image,0,0);
    lv_obj_add_flag(image,LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(video,LV_OBJ_FLAG_HIDDEN);
    heading=lv_label_create(root); lv_obj_set_width(heading,260);
    lv_obj_set_style_text_align(heading,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_font(heading,&lv_font_montserrat_20,0);
    lv_obj_set_pos(heading,50,38);
    menu=lv_obj_create(root); lv_obj_set_size(menu,286,228); lv_obj_set_pos(menu,37,70);
    lv_obj_set_style_bg_color(menu,lv_color_hex(0x1e222c),0);
    lv_obj_set_style_bg_opa(menu,LV_OPA_90,0); lv_obj_set_style_border_width(menu,0,0);
    lv_obj_set_style_pad_all(menu,10,0); lv_obj_set_style_pad_row(menu,8,0);
    lv_obj_set_flex_flow(menu,LV_FLEX_FLOW_COLUMN); lv_obj_set_scroll_dir(menu,LV_DIR_VER);
    notice=lv_label_create(root); lv_obj_set_width(notice,260); lv_obj_set_pos(notice,50,260);
    lv_obj_set_style_text_align(notice,LV_TEXT_ALIGN_CENTER,0); lv_label_set_text(notice,"");
    set_menu(false);
    // Render the first splash frame before scanning the media catalog.
    lv_timer_handler();
    if(sd_ok) seasons=catalog::scan("/sdcard/videos/seasons");
    // Catalog order is natural numeric order across seasons and episodes.
    for(size_t s=0;s<seasons.size();++s)
        for(size_t e=0;e<seasons[s].episodes.size();++e) playlist.push_back({s,e});
    playlist_index=0;
    if(!sd_ok)problem="SD card unavailable";
    if(!gif)lv_label_set_text(notice,(problem+"\nTap to start").c_str());
    board::backlight(true);
    uint64_t last_state=0;
    for(;;) {
        if(splash_running){
            uint64_t started=esp_timer_get_time();
            auto update=splash_player.tick(uint32_t(ms()));
            if(update.frame){
                splash_work_us+=esp_timer_get_time()-started;++splash_steps;splash_changed();
            }
            if(update.loop)loop_finished=true;
            if(update.error){splash_running=false;problem="Splash decoding failed";if(!startup_waiting)show_controls();else lv_label_set_text(notice,problem.c_str());}
        }
        const uint64_t stamp=ms();
        if(stamp-splash_stats_at>=1000){
            splash_fps=float(splash_flushes)*1000.0f/float(stamp-splash_stats_at);
            splash_ms=splash_steps?float(splash_work_us)/float(splash_steps)/1000.0f:0;
            splash_flushes=splash_steps=0;splash_work_us=0;splash_stats_at=stamp;
        }
        if(want_controls) {
            want_controls=false;
            if(startup_waiting){
                startup_waiting=false;
                if(!playlist.empty()){playlist_index=0;set_menu(false);begin_selected();}
                else show_seasons();
            }else show_controls();
        }
        if(problem=="Refreshing..." && player::status().state==player::State::idle) {
            problem.clear(); seasons=catalog::scan("/sdcard/videos/seasons"); load_splash(); show_seasons();
        }
        if(player::copy_frame(reinterpret_cast<uint16_t *>(const_cast<uint8_t *>(video_image.data)))) {
            lv_img_cache_invalidate_src(&video_image); lv_obj_invalidate(image);
        }
        if(ms()-last_state>=50) {
            last_state=ms(); auto state=player::status();
            if(audio_status && menu_visible){
                update_audio_status(state);
            }
            if(player_token && state.token==player_token) {
                if(state.state==player::State::ended && testing_sound) {
                    testing_sound=false;player_token=0;show_controls();
                } else if(state.state==player::State::ended) {
                    player_token=0;
                    playlist_index=(playlist_index+1)%playlist.size(); intermission();
                } else if(state.state==player::State::failed) {
                    player_token=0; waiting=false; testing_sound=false; problem=state.error;
                    lv_obj_add_flag(video,LV_OBJ_FLAG_HIDDEN); if(gif){lv_obj_clear_flag(gif,LV_OBJ_FLAG_HIDDEN);restart_gif();} show_controls();
                }
            }
        }
        if(loop_finished) {
            loop_finished=false;
            if(waiting && ms()-intermission_at>=5000 && !menu_visible) begin_selected();
        }
        // Between-episode fallback only; startup never enters waiting/intermission.
        if(waiting && !menu_visible && ms()-intermission_at>=(gif?15000:5000)) begin_selected();
        lv_timer_handler(); vTaskDelay(pdMS_TO_TICKS(5));
    }
}
