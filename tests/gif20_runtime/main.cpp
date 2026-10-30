#include "gif_player.hpp"
#include "lvgl.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <cassert>
#include <cstdio>
static std::vector<char> read(const char*p){std::ifstream f(p,std::ios::binary);assert(f);return {(std::istreambuf_iterator<char>(f)),{}};}
int main(int argc,char**argv){
 assert(argc==3);lv_init();auto bytes=read(argv[1]);splash_render::Player p;
 assert(p.open(bytes.data(),0));assert(p.pixels());
 unsigned frames=0,loops=0;
 for(uint32_t t=1;t<=13800;++t){auto u=p.tick(t);assert(!u.error);frames+=u.frame;loops+=u.loop;if(u.frame)assert(t%50==0);}
 assert(frames==276 && loops==2 && p.late_frames()==0);
 assert(p.restart(20000));assert(!p.tick(20049).frame);assert(p.tick(20050).frame);
 assert(p.open(bytes.data(),UINT32_MAX-24));assert(!p.tick(24).frame);assert(p.tick(25).frame);
 p.tick(200);assert(p.late_frames()>0);p.close();assert(!p.pixels());
 auto fixture=read(argv[2]);assert(p.open(fixture.data(),0));
 // Center crop of a 4x2 image: middle two columns are red/green on top,
 // blue/white on bottom. The outer magenta columns must be absent.
 auto out=p.pixels();assert(out[0]==lv_color_hex(0xff0000).full);
 assert(out[359]==lv_color_hex(0x00ff00).full);
 assert(out[359*360]==lv_color_hex(0x0000ff).full);
 assert(out[360*360-1]==lv_color_hex(0xffffff).full);
 auto u=p.tick(50);assert(u.frame&&!u.loop);
 assert(p.pixels()[0]==lv_color_hex(0x000000).full);
 assert(p.tick(100).loop);assert(p.pixels()[0]==lv_color_hex(0xff0000).full);
 printf("PASS: 276 scheduled frames / 13.8s = 20 fps; two loops; restart; clock wrap; late accounting; centered crop; colors\n");
}
