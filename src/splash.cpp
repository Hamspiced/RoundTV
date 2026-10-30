#include "splash.hpp"
#include <cstring>
bool validate_gif(const std::vector<uint8_t> &d,std::string &error) {
    auto bad=[&]() { error="Invalid splash GIF; use prepare_media.py splash"; return false; };
    if(d.size()<14 || d.size()>2*1024*1024 ||
       (memcmp(d.data(),"GIF89a",6) && memcmp(d.data(),"GIF87a",6))) return bad();
    auto word=[&](size_t p) { return unsigned(d[p]) | unsigned(d[p+1])<<8; };
    unsigned w=word(6),h=word(8);
    if(w<16 || h<16 || w>480 || h>480) return bad();
    size_t pos=13;
    if(d[10]&0x80) pos+=3*(1u<<((d[10]&7)+1));
    unsigned frames=0;
    auto subblocks=[&]() {
        while(pos<d.size()) {
            unsigned n=d[pos++]; if(!n) return true;
            if(n>d.size()-pos) return false;
            pos+=n;
        }
        return false;
    };
    while(pos<d.size()) {
        uint8_t kind=d[pos++];
        if(kind==0x3b) { if(frames>0 && pos==d.size()) return true; return bad(); }
        if(kind==0x21) {
            if(pos>=d.size()) return bad();
            ++pos; if(!subblocks()) return bad();
        } else if(kind==0x2c) {
            if(pos+9>d.size()) return bad();
            unsigned x=word(pos),y=word(pos+2),fw=word(pos+4),fh=word(pos+6);
            uint8_t flags=d[pos+8]; pos+=9;
            if(!fw || !fh || x+fw>w || y+fh>h || ++frames>2000) return bad();
            if(flags&0x80) pos+=3*(1u<<((flags&7)+1));
            if(pos>=d.size() || d[pos]<2 || d[pos]>8) return bad();
            ++pos; if(!subblocks()) return bad();
        } else return bad();
    }
    return bad();
}
