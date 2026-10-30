#include "board.hpp"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/sdmmc_host.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "vendor/esp_lcd_st77916.h"
#include "vendor/es8311.h"
#include "vendor/es8311_reg.h"
#include <algorithm>
#include <cstring>
extern "C" {
extern const st77916_lcd_init_cmd_t vendor_specific_init_new[];
extern const unsigned panel_variant_count;
}
namespace board {
static esp_lcd_panel_handle_t panel;
static SemaphoreHandle_t transferred;
static es8311_handle_t codec=nullptr;
static uint8_t outputs=0xff;
static void wait_ms(int n){vTaskDelay(pdMS_TO_TICKS(n));}
static esp_err_t reg_write(uint8_t addr,uint8_t reg,uint8_t val){
 uint8_t b[]={reg,val};return i2c_master_write_to_device(I2C_NUM_0,addr,b,2,pdMS_TO_TICKS(100));
}
static void exio(int bit,bool high){
 if(high)outputs|=1u<<bit;else outputs&=~(1u<<bit);
 ESP_ERROR_CHECK(reg_write(0x20,1,outputs));
}
static bool done(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t*,void*){
 BaseType_t wake=pdFALSE;xSemaphoreGiveFromISR(transferred,&wake);return wake==pdTRUE;
}
void backlight(bool on){gpio_set_level(GPIO_NUM_5,on);}
const char *revision(){return "1.85C V2.1 / ES8311";}
int audio_mclk(){return 2;}
void amplifier(bool on){gpio_set_level(GPIO_NUM_15,on);}
esp_err_t audio_init(uint32_t rate){
 if(!codec)codec=es8311_create(I2C_NUM_0,ES8311_ADDRESS_0);
 if(!codec)return ESP_ERR_NO_MEM;
 es8311_clock_config_t clk{};
 clk.mclk_from_mclk_pin=true;clk.mclk_frequency=rate*256;clk.sample_frequency=rate;
 esp_err_t e=es8311_init(codec,&clk,ES8311_RESOLUTION_16,ES8311_RESOLUTION_16);
 // DAC 0xBF is unity (0 dB); the vendor volume helper at 100 selects +32 dB.
 // Keep codec gain fixed; player.cpp applies UI volume and the V2 output cap.
 if(e==ESP_OK)e=reg_write(ES8311_ADDRESS_0,ES8311_DAC_REG32,0xbf);
 if(e==ESP_OK)e=es8311_microphone_config(codec,false);
 return e;
}
esp_lcd_panel_handle_t init_display(){
 gpio_config_t out{};out.mode=GPIO_MODE_OUTPUT;out.pin_bit_mask=(1ULL<<5)|(1ULL<<15);
 ESP_ERROR_CHECK(gpio_config(&out));backlight(false);amplifier(false);
 i2c_config_t i2c{};i2c.mode=I2C_MODE_MASTER;i2c.sda_io_num=11;i2c.scl_io_num=10;
 i2c.sda_pullup_en=GPIO_PULLUP_ENABLE;i2c.scl_pullup_en=GPIO_PULLUP_ENABLE;i2c.master.clk_speed=400000;
 ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0,&i2c));
 ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0,I2C_MODE_MASTER,0,0,0));
 ESP_ERROR_CHECK(reg_write(0x20,1,outputs));
 // Only touch reset P0 and LCD reset P1 are outputs; preserve other peripheral pins.
 ESP_ERROR_CHECK(reg_write(0x20,3,0xfc));
 exio(1,false);exio(0,false);wait_ms(20);exio(1,true);exio(0,true);wait_ms(120);
 reg_write(0x15,0xfe,1); // disable touch auto-sleep
 spi_bus_config_t bus{};bus.sclk_io_num=40;bus.data0_io_num=46;bus.data1_io_num=45;
 bus.data2_io_num=42;bus.data3_io_num=41;bus.data4_io_num=bus.data5_io_num=bus.data6_io_num=bus.data7_io_num=-1;
 bus.max_transfer_sz=width*40*2;bus.flags=SPICOMMON_BUSFLAG_MASTER;
 ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
 esp_lcd_panel_io_spi_config_t io{};io.cs_gpio_num=21;io.dc_gpio_num=-1;io.spi_mode=0;
 io.pclk_hz=3000000;io.trans_queue_depth=2;io.lcd_cmd_bits=32;io.lcd_param_bits=8;io.flags.quad_mode=1;
 esp_lcd_panel_io_handle_t handle;
 ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&io,&handle));
 uint8_t id[4]={};esp_err_t identified=esp_lcd_panel_io_rx_param(handle,(0x0b<<24)|(0x04<<8),id,4);
 ESP_ERROR_CHECK(esp_lcd_panel_io_del(handle));
 transferred=xSemaphoreCreateBinary();configASSERT(transferred);
 io.pclk_hz=80000000;io.on_color_trans_done=done;
 ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,&io,&handle));
 st77916_vendor_config_t vendor{};vendor.flags.use_qspi_interface=1;
 if(identified==ESP_OK && id[0]==0 && id[1]==2 && id[2]==0x7f && id[3]==0x7f){
  vendor.init_cmds=vendor_specific_init_new;vendor.init_cmds_size=panel_variant_count;
 }
 esp_lcd_panel_dev_config_t cfg{};cfg.reset_gpio_num=-1;cfg.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;
 cfg.bits_per_pixel=16;cfg.vendor_config=&vendor;
 ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(handle,&cfg,&panel));
 ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
 ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));
 return panel;
}
void draw(int x1,int y1,int x2,int y2,const void *pixels){
 ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,x1,y1,x2,y2,pixels));
 // Do not release LVGL's DMA buffer until QSPI has finished reading it.
 configASSERT(xSemaphoreTake(transferred,pdMS_TO_TICKS(500))==pdTRUE);
}
esp_err_t mount_sd(){
 sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.max_freq_khz=SDMMC_FREQ_DEFAULT;
 sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=1;
 slot.clk=GPIO_NUM_14;slot.cmd=GPIO_NUM_17;slot.d0=GPIO_NUM_16;
 slot.d1=slot.d2=slot.d3=GPIO_NUM_NC;slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
 esp_vfs_fat_sdmmc_mount_config_t cfg{};cfg.format_if_mount_failed=false;cfg.max_files=6;cfg.allocation_unit_size=16384;
 sdmmc_card_t *card=nullptr;
 return esp_vfs_fat_sdmmc_mount("/sdcard",&host,&slot,&cfg,&card);
}
bool touch(int &x,int &y){
 uint8_t reg=2,b[5]={};
 if(i2c_master_write_read_device(I2C_NUM_0,0x15,&reg,1,b,5,pdMS_TO_TICKS(10))!=ESP_OK || !(b[0]&0x0f))return false;
 x=((b[1]&0x0f)<<8)|b[2];y=((b[3]&0x0f)<<8)|b[4];
 return x<width && y<height;
}
}
