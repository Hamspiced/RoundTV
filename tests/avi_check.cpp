#include "avi.hpp"
#include <cassert>
#include <algorithm>
#include <cstdio>
int main(int argc,char **argv){
 assert(argc==2);media::Avi avi;assert(avi.open(argv[1]));
 assert(avi.info.width==360 && avi.info.height==360);
 media::Chunk c;int r;unsigned frames=0;
 while((r=avi.next(false,c))>0){assert(c.size<=media::max_jpeg);++frames;}
 assert(r==0 && frames==avi.info.frames);
 media::PcmReader pcm;assert(pcm.open(argv[1]));int16_t b[6144];unsigned samples=0;int peak=0;
 while((r=pcm.read(b,6144))>0){samples+=r;for(int i=0;i<r;++i)peak=std::max(peak,int(b[i]<0?-int(b[i]):b[i]));}
 assert(r==0 && samples==avi.info.samples && peak>0);
 assert(avi.info.frame_us(frames)<=uint64_t(samples)*1000000/48000+100000);
 printf("PASS: %u MJPEG frames; %u PCM samples; peak %d; firmware parser accepts all media\n",frames,samples,peak);
}
