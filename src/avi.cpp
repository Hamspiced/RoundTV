#include "avi.hpp"
#include <cstring>
#include <algorithm>

namespace media {
static uint32_t u32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24; }
static uint16_t u16(const uint8_t *p) { return p[0] | uint16_t(p[1])<<8; }
uint32_t fourcc(const char *s) { return u32(reinterpret_cast<const uint8_t *>(s)); }
Avi::~Avi() { if(f) fclose(f); }
bool Avi::read(uint32_t offset, void *dst, size_t size) {
    if (uint64_t(offset)+size > file_end ||
        (ftell(f)!=long(offset) && fseek(f, offset, SEEK_SET)) ||
        fread(dst,1,size,f)!=size) {
        error="Truncated media or SD read failure"; return false;
    }
    return true;
}
bool Avi::chunk_at(uint32_t pos, uint32_t limit, Chunk &c) {
    uint8_t b[8];
    if (uint64_t(pos)+8>limit || !read(pos,b,8)) { error="Truncated AVI chunk"; return false; }
    c.id=u32(b); c.size=u32(b+4); c.data=pos+8;
    uint64_t end=uint64_t(c.data)+c.size+(c.size&1);
    if(end>limit) { error="AVI chunk exceeds its parent"; return false; }
    c.end=uint32_t(end); return true;
}
bool Avi::stream_list(uint32_t pos,uint32_t end,int stream) {
    uint8_t sh[56]{}, sf[40]{}; bool have_sh=false, have_sf=false;
    while(pos<end) {
        Chunk c; if(!chunk_at(pos,end,c)) return false;
        if(c.id==fourcc("strh")) {
            if(c.size<56 || !read(c.data,sh,56)) { error="Bad AVI stream header"; return false; }
            have_sh=true;
        }
        if(c.id==fourcc("strf")) {
            if(c.size<16 || !read(c.data,sf,std::min<uint32_t>(40,c.size))) return false;
            have_sf=true;
        }
        pos=c.end;
    }
    if(!have_sh || !have_sf) { error="Missing AVI stream format"; return false; }
    if(stream==0 && u32(sh)==fourcc("vids") && u32(sf+16)==fourcc("MJPG")) {
        info.width=u32(sf+4); info.height=u32(sf+8);
        info.scale=u32(sh+20); info.rate=u32(sh+24); info.frames=u32(sh+32);
        if(!info.scale || !info.rate || !info.frames || u32(sh+28)!=0 ||
           info.width!=360 || info.height!=360 ||
           double(info.rate)/info.scale>30 || double(info.rate)/info.scale<1) {
            error="Use converter: expected 360x360 MJPEG, 1-30 fps, zero start"; return false;
        }
    } else if(stream==1 && u32(sh)==fourcc("auds") && u16(sf)==1 &&
              u16(sf+2)==1 && u32(sf+4)==48000 && u16(sf+12)==2 && u16(sf+14)==16 &&
              u32(sh+28)==0 && u32(sh+44)==2) {
        info.samples=u32(sh+32);
        if(!info.samples) { error="Empty audio stream"; return false; }
    } else { error="Use converter: stream 0 MJPEG, stream 1 PCM16 mono 48000Hz"; return false; }
    return true;
}
bool Avi::header_list(uint32_t pos,uint32_t end,unsigned depth,int &stream) {
    if(depth>4) { error="AVI header nesting too deep"; return false; }
    while(pos<end) {
        Chunk c; if(!chunk_at(pos,end,c)) return false;
        if(c.id==fourcc("LIST")) {
            uint8_t kind[4]; if(c.size<4 || !read(c.data,kind,4)) return false;
            if(u32(kind)==fourcc("strl")) {
                if(!stream_list(c.data+4,c.data+c.size,stream++)) return false;
            } else if(u32(kind)==fourcc("hdrl")) {
                if(!header_list(c.data+4,c.data+c.size,depth+1,stream)) return false;
            } else if(u32(kind)==fourcc("movi")) {
                info.movi=c.data+4; info.movi_end=c.data+c.size;
            }
        }
        pos=c.end;
    }
    return true;
}
bool Avi::open(const std::string &path) {
    if(f) fclose(f);
    f=fopen(path.c_str(),"rb"); error.clear(); info={}; list_ends.clear();
    if(!f) { error="Cannot open video.avi"; return false; }
    setvbuf(f,nullptr,_IOFBF,16384);
    if(fseek(f,0,SEEK_END)) return false;
    long size=ftell(f);
    if(size<12 || uint64_t(size)>0x7fffffff) { error="AVI must be smaller than 2 GiB"; return false; }
    file_end=uint32_t(size);
    uint8_t h[12]; if(!read(0,h,12)) return false;
    if(u32(h)!=fourcc("RIFF") || u32(h+8)!=fourcc("AVI ") || uint64_t(u32(h+4))+8!=file_end) {
        error="Expected single RIFF AVI (OpenDML unsupported)"; return false;
    }
    int streams=0;
    if(!header_list(12,file_end,0,streams)) return false;
    if(streams!=2 || !info.movi || !info.samples || !info.frames) {
        error="AVI missing video/audio/movi"; return false;
    }
    position=info.movi; list_ends.push_back(info.movi_end); return true;
}
int Avi::next(bool audio,Chunk &out) {
    while(!list_ends.empty()) {
        if(position==list_ends.back()) { list_ends.pop_back(); if(position&1) ++position; continue; }
        Chunk c; if(!chunk_at(position,list_ends.back(),c)) return -1;
        position=c.end;
        if(c.id==fourcc("LIST")) {
            uint8_t kind[4]; if(c.size<4 || !read(c.data,kind,4)) return -1;
            if(u32(kind)==fourcc("rec ")) {
                if(list_ends.size()>8) { error="AVI record nesting too deep"; return -1; }
                position=c.data+4; list_ends.push_back(c.data+c.size);
            }
            continue;
        }
        bool match=audio ? c.id==fourcc("01wb") : (c.id==fourcc("00dc") || c.id==fourcc("00db"));
        if(match) {
            if(!c.size) continue;
            if((audio && (c.size&1)) || (!audio && c.size>max_jpeg)) {
                error="Invalid PCM chunk or oversized JPEG frame"; return -1;
            }
            out=c; return 1;
        }
    }
    return 0;
}
int PcmReader::read(int16_t *out,unsigned count) {
    unsigned done=0;
    while(done<count && consumed<avi.info.samples) {
        if(used==chunk.size) {
            int next=avi.next(true,chunk);
            if(next<=0) return -1;
            used=0;
        }
        unsigned n=std::min<uint32_t>({count-done,(chunk.size-used)/2,avi.info.samples-consumed});
        if(!avi.read(chunk.data+used,out+done,n*2)) return -1;
        used+=n*2; done+=n; consumed+=n;
    }
    return done;
}
}
