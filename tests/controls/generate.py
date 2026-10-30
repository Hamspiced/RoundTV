from pathlib import Path
root=Path(__file__).resolve().parents[2]
s=(root/'src/main.cpp').read_text()
controls=s[s.index('static void update_audio_status('):s.index('\nextern "C"')]
clear=s[s.index('static void clear_menu('):s.index('static void row(')]
enums=s[s.index('enum { BACK='):s.index('static void action(')]
out=Path(__file__).parent
(out/'lv_conf.h').write_text((root/'tests/gif20_runtime/lv_conf.h').read_text())
(out/'main.cpp').write_text('''
#include "lvgl.h"
#include "player.hpp"
#include "catalog.hpp"
#include <vector>
#include <string>
#include <cassert>
#include <cstdio>
static lv_obj_t *menu,*heading,*audio_status;
static bool testing_sound=false,paused=false,waiting=false;
static int volume_percent=50;
static std::string problem;
struct Selection {size_t season,episode;};
static std::vector<Selection> playlist={{0,0}};
static size_t playlist_index=0;
static std::vector<catalog::Season> seasons;
namespace board {const char *revision(){return "1.85C V2 / ES8311";}}
static player::Status diagnostics;
namespace player {Status status(){return diagnostics;}}
static void set_menu(bool){}
static void action(lv_event_t*){}
''' + enums + clear + controls + '''
static lv_color_t pixels[360*360],buffer[360*40];
static void flush(lv_disp_drv_t*d,const lv_area_t*a,lv_color_t*c){
 for(int y=a->y1;y<=a->y2;++y)for(int x=a->x1;x<=a->x2;++x)pixels[y*360+x]=*c++;
 lv_disp_flush_ready(d);
}
int main(int argc,char**argv){
 lv_init();static lv_disp_draw_buf_t b;lv_disp_draw_buf_init(&b,buffer,nullptr,360*40);
 static lv_disp_drv_t d;lv_disp_drv_init(&d);d.hor_res=360;d.ver_res=360;d.draw_buf=&b;d.flush_cb=flush;lv_disp_drv_register(&d);
 lv_obj_set_style_text_color(lv_scr_act(),lv_color_hex(0xeee5cd),0);
 lv_obj_set_style_bg_color(lv_scr_act(),lv_color_hex(0x12151d),0);
 heading=lv_label_create(lv_scr_act());lv_obj_set_pos(heading,50,38);lv_obj_set_width(heading,260);
 lv_obj_set_style_text_align(heading,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_font(heading,&lv_font_montserrat_20,0);
 menu=lv_obj_create(lv_scr_act());lv_obj_set_style_bg_color(menu,lv_color_hex(0x1e222c),0);lv_obj_set_style_border_width(menu,0,0);
 catalog::Season season;season.name="01";season.episodes.push_back({"01 - Episode title","test.avi"});seasons.push_back(season);
 if(argc>1)problem="SD audio read failed. Check card and video.avi.";
 show_controls();lv_obj_update_layout(menu);
 assert(!lv_obj_has_flag(menu,LV_OBJ_FLAG_SCROLLABLE));
 int buttons=0;
 for(uint32_t i=0;i<lv_obj_get_child_cnt(menu);++i){
  auto *c=lv_obj_get_child(menu,i);if(!lv_obj_check_type(c,&lv_btn_class))continue;
  ++buttons;lv_area_t a;lv_obj_get_coords(c,&a);
  for(int x:{int(a.x1),int(a.x2)})for(int y:{int(a.y1),int(a.y2)})assert((x-180)*(x-180)+(y-180)*(y-180)<180*180);
  auto *label=lv_obj_get_child(c,0);lv_area_t l;lv_obj_get_coords(label,&l);
  assert(l.x1>=a.x1 && l.x2<=a.x2 && l.y1>=a.y1 && l.y2<=a.y2);
 }
 if(!problem.empty()){
  auto *title=lv_obj_get_child(menu,0);assert(lv_label_get_long_mode(title)==LV_LABEL_LONG_WRAP);
  lv_point_t sz;lv_txt_get_size(&sz,lv_label_get_text(title),lv_obj_get_style_text_font(title,0),lv_obj_get_style_text_letter_space(title,0),lv_obj_get_style_text_line_space(title,0),258,LV_TEXT_FLAG_NONE);assert(sz.y<=60);
 }
 assert(buttons==9);
 for(const auto &test:std::vector<std::string>{"50% B9s V14400 READ","100% B10s V108000 SHOW","0% B0s V0 WAIT"}){
  lv_point_t sz;lv_txt_get_size(&sz,test.c_str(),lv_obj_get_style_text_font(audio_status,0),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
  assert(sz.x<=234 && sz.y<=20);
 }
 for(uint32_t stage=0;stage<6;++stage){
  diagnostics.video_stage=stage;diagnostics.video_frames=stage==3?108000:14400;diagnostics.buffered_ms=stage==3?10000:9000;
  volume_percent=stage==3?100:50;
  update_audio_status(diagnostics);lv_obj_update_layout(menu);
  lv_area_t a;lv_obj_get_coords(audio_status,&a);
  lv_point_t sz;lv_txt_get_size(&sz,lv_label_get_text(audio_status),lv_obj_get_style_text_font(audio_status,0),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
  assert(sz.x<=234 && sz.y<=20);
  for(int x:{180-int(sz.x)/2,180+(int(sz.x)+1)/2})for(int y:{int(a.y1),int(a.y2)})assert((x-180)*(x-180)+(y-180)*(y-180)<180*180);
 }
 diagnostics.video_frames=14400;diagnostics.buffered_ms=9000;diagnostics.video_stage=1;volume_percent=50;update_audio_status(diagnostics);
 assert(std::string(lv_label_get_text(audio_status))=="50% B9s V14400 READ");
 lv_refr_now(nullptr);
 FILE *f=fopen("controls.ppm","wb");assert(f);fprintf(f,"P6\\n360 360\\n255\\n");
 for(int y=0;y<360;++y)for(int x=0;x<360;++x){lv_color32_t c;c.full=lv_color_to32(pixels[y*360+x]);unsigned char rgb[]={c.ch.red,c.ch.green,c.ch.blue};if((x-180)*(x-180)+(y-180)*(y-180)>180*180)rgb[0]=rgb[1]=rgb[2]=0;fwrite(rgb,1,3,f);}fclose(f);
 puts("PASS: actual R7 controls: nine non-scrolling buttons, labels and diagnostic footer fit within circular screen");
}
''')
