#include "player.hpp"
#include "pcm_queue.hpp"
#include "video_pacing.hpp"
#include "avi.hpp"
#include "board.hpp"
#include "esp_jpeg_dec.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <new>

namespace player {
static constexpr uint32_t sample_rate=48000;
// Fixed ceiling independent of the user's 0-100% control.
static constexpr int output_divisor=2; // V2: half amplitude (-6.02 dB)
static constexpr int audio_done=1, video_done=2;
static constexpr uint32_t pcm_capacity=524288; // 1 MiB allocation; power of two for counter wrap
static constexpr uint32_t pcm_limit=480000; // at most ten seconds of mono PCM
static constexpr uint32_t pcm_prefill=240000; // five seconds before start/recovery
static SemaphoreHandle_t lock, frame_lock;
static EventGroupHandle_t completed;
static Status current;
static uint16_t *latest=nullptr;
static bool frame_new=false;
static std::atomic<int> level{default_volume_percent};
static std::atomic<bool> muted{false};
static std::atomic<uint32_t> requested{0};
static std::string requested_path;

struct Session {
    std::string path;
    uint32_t token;
    std::atomic<bool> cancel{false}, paused{false}, ready{false};
    std::atomic<uint32_t> sent{0}, base{0}, submitted{0}, samples{0}, drops{0}, peak{0};
    std::atomic<uint32_t> video_frames{0}, buffered_ms{0}, read_max_ms{0}, video_stage{0};
    std::string error;
    uint64_t clock_us() const {
        return uint64_t(std::min(base.load()+sent.load(),submitted.load()))*1000000/sample_rate;
    }
};
static Session *active=nullptr; // accessed only while lock held, or by coordinator
static void nap(int ms=2) { vTaskDelay(pdMS_TO_TICKS(ms)); }
static void fail(Session *s,const std::string &error) {
    xSemaphoreTake(lock,portMAX_DELAY);
    if(s->error.empty()) s->error=error;
    s->cancel=true;
    xSemaphoreGive(lock);
}
static bool audio_sent(i2s_chan_handle_t,i2s_event_data_t *event,void *ctx) {
    static_cast<Session *>(ctx)->sent.fetch_add(event->size/4,std::memory_order_relaxed);
    return false;
}
struct AudioFeed {
    Session *session;
    media::PcmReader reader;
    int16_t *storage=nullptr, *scratch=nullptr;
    PcmQueue *queue=nullptr;
    std::atomic<bool> stop{false}, done{false};
    bool started=false;
};
static void pcm_worker(void *ctx) {
    auto *f=static_cast<AudioFeed *>(ctx);
    uint32_t total=0;
    while(!f->stop && !f->session->cancel && total<f->reader.info().samples) {
        const uint32_t buffered=f->queue->available();
        f->session->buffered_ms=buffered/48;
        if(buffered>=pcm_limit) { nap(5); continue; }
        const int n=f->reader.read(f->scratch,std::min<uint32_t>(4096,pcm_limit-buffered));
        if(n<=0) { fail(f->session,"SD audio read failed. Check card and video.avi."); break; }
        if(!f->queue->push(f->scratch,n)) { fail(f->session,"Audio buffer error. Restart playback."); break; }
        total+=n;
    }
    f->done.store(true,std::memory_order_release);
    vTaskDelete(nullptr);
}
static void audio_run(Session *s) {
    AudioFeed feed{s};
    i2s_chan_handle_t tx=nullptr;
    bool enabled=false;
    auto finish=[&]() {
        board::amplifier(false);
        if(enabled) i2s_channel_disable(tx);
        if(tx) i2s_del_channel(tx);
        feed.stop=true;
        while(feed.started && !feed.done.load(std::memory_order_acquire))nap();
        s->buffered_ms=0;
        delete feed.queue;free(feed.storage);free(feed.scratch);
    };
    const bool test=s->path==":speaker-test:";
    uint32_t test_position=0;
    if(!test && !feed.reader.open(s->path)) { fail(s,feed.reader.error()); finish(); return; }
    s->samples=test?sample_rate:feed.reader.info().samples;
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,I2S_ROLE_MASTER);
    channel.dma_desc_num=12; channel.dma_frame_num=512;
    channel.auto_clear=true;
    esp_err_t err=i2s_new_channel(&channel,&tx,nullptr);
    if(err!=ESP_OK) { fail(s,"Cannot create I2S audio channel"); finish(); return; }
    i2s_std_config_t cfg{};
    cfg.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    cfg.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
    cfg.gpio_cfg.mclk=static_cast<gpio_num_t>(board::audio_mclk());
    cfg.gpio_cfg.bclk=static_cast<gpio_num_t>(board::audio_bclk);
    cfg.gpio_cfg.ws=static_cast<gpio_num_t>(board::audio_ws);
    cfg.gpio_cfg.dout=static_cast<gpio_num_t>(board::audio_dout);
    cfg.gpio_cfg.din=GPIO_NUM_NC;
    err=i2s_channel_init_std_mode(tx,&cfg);
    i2s_event_callbacks_t cb{}; cb.on_sent=audio_sent;
    if(err==ESP_OK) err=i2s_channel_register_event_callback(tx,&cb,s);
    if(err!=ESP_OK) { fail(s,"Cannot initialize I2S audio"); finish(); return; }
    if(board::audio_init(sample_rate)!=ESP_OK){fail(s,"Audio codec init failed; check the V2.1 board");finish();return;}
    // Buffers are internal RAM, not on a small task stack.
    auto *mono=static_cast<int16_t *>(heap_caps_malloc(6144*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    auto *stereo=static_cast<int16_t *>(heap_caps_malloc(6144*4,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if(!mono || !stereo) {
        free(mono); free(stereo); fail(s,"Not enough internal RAM for audio"); finish(); return;
    }
    if(!test) {
        feed.storage=static_cast<int16_t *>(heap_caps_malloc(pcm_capacity*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        feed.scratch=static_cast<int16_t *>(heap_caps_malloc(4096*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        if(feed.storage && feed.scratch)feed.queue=new(std::nothrow) PcmQueue(feed.storage,pcm_capacity);
        if(!feed.queue || xTaskCreatePinnedToCore(pcm_worker,"pcm-read",6144,&feed,5,nullptr,0)!=pdPASS) {
            fail(s,"Cannot allocate audio read-ahead buffer");
            free(mono);free(stereo);finish();return;
        }
        feed.started=true;
    }
    int gain_q16=0;
    auto fill=[&](int capacity) {
        int n;
        if(test){
            n=std::min<uint32_t>(capacity,sample_rate-test_position);
            for(int i=0;i<n;++i){
                const uint32_t pos=test_position++;
                const float edge=std::min(1.0f,float(std::min(pos,sample_rate-1-pos))/480.0f);
                mono[i]=int16_t(8192.0f*edge*sinf(6.28318530718f*440.0f*pos/sample_rate));
            }
        }else {
            n=feed.queue->pop(mono,capacity);
            s->buffered_ms=feed.queue->available()/48;
        }
        if(n<0) { fail(s,"Truncated PCM audio or SD read failure"); return -1; }
        uint32_t peak=0;
        for(int i=0;i<n;++i)peak=std::max(peak,uint32_t(std::abs(int(mono[i]))));
        s->peak=std::max(s->peak.load(),peak);
        int gain=muted.load()?0:level.load();
        for(int i=0;i<capacity;++i) {
            const int target=gain*65536/100;
            gain_q16+=std::clamp(target-gain_q16,-256,256); // about 5 ms ramp, no step clicks
            int16_t v=i<n ? int32_t(mono[i])*gain_q16/(65536*output_divisor) : 0;
            stereo[i*2]=stereo[i*2+1]=v;
        }
        return n;
    };
    bool eof=false;
    while(!s->cancel && !eof) {
        while(s->paused && !s->cancel) nap(10);
        if(s->cancel) break;
        // SD reads run independently of the DMA feeder. Start/resume/recovery
        // waits for five seconds (or all remaining audio at EOF), with cancellation.
        while(!test && !s->cancel && !feed.done.load(std::memory_order_acquire) &&
              feed.queue->available()<pcm_prefill)nap(5);
        if(s->cancel)break;
        if(s->paused)continue;
        // On first start/resume, preload all 12 DMA descriptors before starting the sample clock.
        s->base=s->submitted.load(); s->sent=0;
        int n=fill(6144);
        if(n<=0) break;
        size_t loaded=0;
        err=i2s_channel_preload_data(tx,stereo,6144*4,&loaded);
        if(err!=ESP_OK || loaded!=6144*4) { fail(s,"I2S preload failed"); break; }
        s->submitted.fetch_add(n);
        eof=n<6144 || s->submitted>=s->samples;
        err=i2s_channel_enable(tx);
        if(err!=ESP_OK) { fail(s,"I2S start failed"); break; }
        enabled=true; board::amplifier(true); s->ready=true;
        while(!s->cancel && !s->paused && !eof) {
            // Drain and rebuffer before the DMA queue empties. A slow card may
            // pause playback, but must not discard samples or end the episode.
            if(!test && !feed.done.load(std::memory_order_acquire) && feed.queue->available()<1024)break;
            // Also recover from an unusually long scheduling stall. The media
            // clock is clamped to submitted samples while DMA outputs silence.
            if(s->base+s->sent > s->submitted+512)break;
            n=fill(1024);
            if(n<0) break;
            if(!n) { eof=true; break; }
            s->submitted.fetch_add(n);
            size_t written=0;
            err=i2s_channel_write(tx,stereo,n*4,&written,250);
            if(err!=ESP_OK || written!=size_t(n*4)) { fail(s,"Audio write timeout"); break; }
            eof=s->submitted>=s->samples;
        }
        // Drain real samples. The clock is clamped at submitted to exclude trailing zero padding.
        while(!s->cancel && s->base+s->sent<s->submitted) nap();
        board::amplifier(false);
        i2s_channel_disable(tx); enabled=false;s->ready=eof;
        gain_q16=0;
        s->base=s->submitted.load(); s->sent=0;
    }
    free(mono); free(stereo); finish();
}
static void audio_worker(void *ctx) {
    audio_run(static_cast<Session *>(ctx)); // let FILE and C++ members destruct before deleting task
    xEventGroupSetBits(completed,audio_done);
    vTaskDelete(nullptr);
}

static void video_worker(void *ctx) {
    auto *s=static_cast<Session *>(ctx);
    {
        media::Avi avi;
        jpeg_dec_handle_t decoder=nullptr;
        auto *compressed=static_cast<uint8_t *>(heap_caps_aligned_alloc(16,media::max_jpeg,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        auto *decoded=static_cast<uint8_t *>(heap_caps_aligned_alloc(16,368*368*2+64,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!compressed || !decoded) fail(s,"Not enough PSRAM for video");
        if(!s->cancel && !avi.open(s->path)) fail(s,avi.error);
        jpeg_dec_config_t cfg=DEFAULT_JPEG_DEC_CONFIG();
        cfg.output_type=JPEG_PIXEL_FORMAT_RGB565_LE;
        if(!s->cancel && jpeg_dec_open(&cfg,&decoder)!=JPEG_ERR_OK) fail(s,"JPEG decoder initialization failed");
        uint32_t frame=0;
        VideoPacing pacing;
        auto read_time=[&](int64_t started) {
            const uint32_t elapsed=(esp_timer_get_time()-started)/1000;
            s->read_max_ms=std::max(s->read_max_ms.load(),elapsed);
        };
        media::Chunk chunk;
        while(!s->cancel) {
            s->video_stage=1;
            int64_t started=esp_timer_get_time();
            int r=avi.next(false,chunk);
            read_time(started);
            if(r<0) { fail(s,avi.error); break; }
            if(!r) {
                if(frame!=avi.info.frames) fail(s,"Video frame count mismatch: truncated AVI");
                break;
            }
            uint64_t pts=avi.info.frame_us(frame++);
            s->video_stage=0;
            while(!s->cancel && (!s->ready || s->clock_us()+25000<pts)) {
                // Prevent a malformed file with video far longer than audio from waiting forever.
                if((xEventGroupGetBits(completed)&audio_done) && s->ready) {
                    if(pts>s->clock_us()+100000) fail(s,"Video duration exceeds audio");
                    break;
                }
                nap();
            }
            if(s->cancel) break;
            if(pacing.drop(pts,s->clock_us())) { ++s->drops; nap(1); continue; }
            s->video_stage=1;
            started=esp_timer_get_time();
            const bool read_ok=avi.read(chunk.data,compressed,chunk.size);
            read_time(started);
            if(!read_ok) { fail(s,avi.error); break; }
            s->video_stage=2;
            jpeg_dec_io_t io{}; io.inbuf=compressed; io.inbuf_len=chunk.size; io.outbuf=decoded;
            jpeg_dec_header_info_t header{};
            if(jpeg_dec_parse_header(decoder,&io,&header)!=JPEG_ERR_OK ||
               header.width!=video_w || header.height!=video_h ||
               jpeg_dec_process(decoder,&io)!=JPEG_ERR_OK) {
                fail(s,"Invalid JPEG frame; run the supplied converter"); break;
            }
            s->video_stage=0;
            while(!s->cancel && s->clock_us()<pts && !(xEventGroupGetBits(completed)&audio_done)) nap();
            if(s->cancel) break;
            xSemaphoreTake(frame_lock,portMAX_DELAY);
            memcpy(latest,decoded,video_w*video_h*2); frame_new=true;
            xSemaphoreGive(frame_lock);
            pacing.displayed(s->clock_us());
            ++s->video_frames;
            s->video_stage=3;
        }
        if(decoder) jpeg_dec_close(decoder);
        free(compressed); free(decoded);
    } // close AVI before signalling done
    s->video_stage=4;
    xEventGroupSetBits(completed,video_done);
    vTaskDelete(nullptr);
}
static void coordinator(void *) {
    uint32_t handled=0;
    for(;;) {
        if(requested==handled) { nap(10); continue; }
        Session s;
        xSemaphoreTake(lock,portMAX_DELAY);
        handled=requested.load(); s.token=handled; s.path=requested_path;
        current={}; current.token=handled;
        current.state=s.path.empty()?State::idle:State::starting;
        active=s.path.empty()?nullptr:&s;
        xSemaphoreGive(lock);
        xSemaphoreTake(frame_lock,portMAX_DELAY); frame_new=false; xSemaphoreGive(frame_lock);
        if(s.path.empty()) continue;
        xEventGroupClearBits(completed,audio_done|video_done);
        if(xTaskCreatePinnedToCore(audio_worker,"audio",6144,&s,6,nullptr,0)!=pdPASS) {
            fail(&s,"Cannot create audio task"); xEventGroupSetBits(completed,audio_done);
        }
        if(s.path==":speaker-test:"){xEventGroupSetBits(completed,video_done);}
        else if(xTaskCreatePinnedToCore(video_worker,"jpeg",8192,&s,4,nullptr,1)!=pdPASS) {
            fail(&s,"Cannot create video task"); xEventGroupSetBits(completed,video_done);
        }
        while((xEventGroupGetBits(completed)&3)!=3) {
            if(requested!=handled) s.cancel=true;
            xSemaphoreTake(lock,portMAX_DELAY);
            current.elapsed_ms=s.clock_us()/1000;
            current.duration_ms=uint64_t(s.samples.load())*1000/sample_rate;
            current.dropped=s.drops; current.audio_peak=s.peak;
            current.video_frames=s.video_frames; current.buffered_ms=s.buffered_ms;
            current.read_max_ms=s.read_max_ms; current.video_stage=s.video_stage;
            if(s.ready) current.state=s.paused?State::paused:State::playing;
            xSemaphoreGive(lock);
            nap(5);
        }
        xSemaphoreTake(lock,portMAX_DELAY);
        current.elapsed_ms=s.clock_us()/1000;
        current.video_frames=s.video_frames; current.buffered_ms=s.buffered_ms;
        current.read_max_ms=s.read_max_ms; current.video_stage=s.video_stage;
        current.error=s.error;
        current.state=!s.error.empty()?State::failed:State::ended;
        active=nullptr;
        xSemaphoreGive(lock);
    }
}
void init() {
    lock=xSemaphoreCreateMutex(); frame_lock=xSemaphoreCreateMutex(); completed=xEventGroupCreate();
    latest=static_cast<uint16_t *>(heap_caps_malloc(video_w*video_h*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    configASSERT(lock && frame_lock && completed && latest);
    configASSERT(xTaskCreatePinnedToCore(coordinator,"player",8192,nullptr,3,nullptr,0)==pdPASS);
}
uint32_t play(const std::string &path) {
    xSemaphoreTake(lock,portMAX_DELAY);
    if(active) active->cancel=true;
    requested_path=path;
    uint32_t id=++requested;
    xSemaphoreGive(lock); return id;
}
uint32_t test_sound(){return play(":speaker-test:");}
void stop() { play(""); }
void pause(bool paused) { xSemaphoreTake(lock,portMAX_DELAY); if(active) active->paused=paused; xSemaphoreGive(lock); }
void volume(int percent) { level=std::clamp(percent,0,100); }
void mute(bool m) { muted=m; }
Status status() { xSemaphoreTake(lock,portMAX_DELAY); auto s=current; xSemaphoreGive(lock); return s; }
bool copy_frame(uint16_t *dst) {
    xSemaphoreTake(frame_lock,portMAX_DELAY);
    bool fresh=frame_new;
    if(fresh) { memcpy(dst,latest,video_w*video_h*2); frame_new=false; }
    xSemaphoreGive(frame_lock); return fresh;
}
}
