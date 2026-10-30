#include "pcm_queue.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <thread>
#include <atomic>
using player::PcmQueue;
static int16_t sample(uint32_t n){return int16_t((n*65521u)^(n>>16));}
int main(){
 const uint32_t cap=524288, total=48000*20*60+123;
 std::vector<int16_t> data(cap);PcmQueue q(data.data(),cap);
 int16_t in[4096],out[1024];uint32_t produced=0,consumed=0,recoveries=0;
 bool buffering=true;uint32_t started=0;
 // Deterministic 20-minute media simulation, not a hardware throughput test.
 // 21 ms playback steps; ordinary 200 ms SD stalls and a twelve-second stall
 // after five minutes must preserve every sample and reach a partial EOF.
 for(uint32_t tick=0;consumed<total && tick<100000;++tick){
  bool stall=(tick%700<10)||(tick>=14300 && tick<14872);
  if(!stall){
   for(int batch=0;batch<2 && produced<total && q.available()+4096<=480000;++batch){
    uint32_t n=std::min(4096u,total-produced);
    for(uint32_t i=0;i<n;++i)in[i]=sample(produced+i);
    assert(q.push(in,n));produced+=n;
   }
  }
  if(buffering){
   if(q.available()<240000 && produced<total)continue;
   buffering=false;++started;
  }
  if(q.available()<1024 && produced<total){buffering=true;++recoveries;continue;}
  uint32_t n=q.pop(out,1024);assert(n);
  for(uint32_t i=0;i<n;++i)assert(out[i]==sample(consumed+i));consumed+=n;
 }
 assert(produced==total && consumed==total && q.available()==0);
 assert(recoveries>=1 && started>=2);
 printf("PASS: 20-minute simulation, %u samples, %u rebuffer event(s); no sample loss/reordering\n",consumed,recoveries);
 // Real producer/consumer concurrency across thousands of queue wraps.
 const uint32_t stress=20000000;std::atomic<bool> done{false};
 std::thread producer([&]{uint32_t pos=0;int16_t b[4096];while(pos<stress){auto n=std::min(4096u,stress-pos);for(uint32_t i=0;i<n;++i)b[i]=sample(pos+i);if(q.push(b,n))pos+=n;else std::this_thread::yield();}done=true;});
 uint32_t pos=0;while(pos<stress){auto n=q.pop(out,1024);for(uint32_t i=0;i<n;++i)assert(out[i]==sample(pos+i));pos+=n;if(!n)std::this_thread::yield();}
 producer.join();assert(done && !q.available());
 puts("PASS: threaded producer/consumer preserves 20,000,000 samples");
}
