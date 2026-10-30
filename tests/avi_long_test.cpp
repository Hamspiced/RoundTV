#include "avi.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>

static uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;
}
int main(int argc,char **argv) {
    assert(argc==2);
    media::Avi avi;
    assert(avi.open(argv[1]));
    assert(avi.info.frames==24*20*60 && avi.info.samples==48000*20*60);
    media::Chunk chunk;
    uint32_t frames=0;
    int result;
    while((result=avi.next(false,chunk))>0) {
        uint8_t marker[9]{};
        assert(chunk.size==sizeof(marker) && avi.read(chunk.data,marker,sizeof(marker)));
        assert(u32(marker)==frames && u32(marker+4)==~frames && marker[8]==0xa5);
        // Re-read backward and then adjacent bytes using the real seek/read path.
        if(frames==24*10*60 || frames==24*20*60-1) {
            assert(avi.read(chunk.data,marker,4) && u32(marker)==frames);
            assert(avi.read(chunk.data+4,marker,4) && u32(marker)==~frames);
        }
        assert(avi.info.frame_us(frames)==uint64_t(frames)*1000000/24);
        ++frames;
    }
    assert(result==0 && frames==avi.info.frames);
    media::PcmReader pcm;
    assert(pcm.open(argv[1]));
    int16_t samples[6144];
    uint32_t total=0;
    while((result=pcm.read(samples,6144))>0) {
        for(int i=0;i<result;++i)assert(samples[i]==int16_t(total+i));
        total+=result;
    }
    assert(result==0 && total==avi.info.samples);
    uint8_t byte;
    assert(!avi.read(UINT32_MAX,&byte,1) && !avi.error.empty());
    printf("PASS: synthetic 20-minute AVI: %u markers, %u PCM samples, ten-minute boundary, nested records, odd padding, backward/adjacent reads and bounds\n",frames,total);
}
