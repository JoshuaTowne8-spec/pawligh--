#include "pet_leds.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "pet_pins.h"

static const char *TAG = "pet_leds";
typedef enum { FX_SOLID, FX_BREATH, FX_WAVE, FX_CENTER_BREATH, FX_CHASE, FX_SPARKLE, FX_HEARTBEAT } effect_type_t;
typedef struct { effect_type_t type; uint8_t r1,g1,b1,r2,g2,b2,brightness,speed,sparkle; uint16_t period_ms; bool mirror; } effect_config_t;
static led_strip_handle_t s_strip_a;
#if !CONFIG_PET_LED_CHAINED
static led_strip_handle_t s_strip_b;
#endif
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static effect_config_t s_effect;
static int64_t s_overlay_until_us;
static bool s_overlay_fast;

static uint8_t clamp8(int value) { return value < 0 ? 0 : value > 255 ? 255 : (uint8_t)value; }
static uint8_t scale8(uint8_t value, uint16_t scale) { return (uint16_t)value * scale / 255; }
static uint8_t blend8(uint8_t a, uint8_t b, uint8_t amount) { return a + ((int)b - a) * amount / 255; }
static uint8_t triangle(uint16_t phase) { uint8_t p = phase & 255; return p < 128 ? p * 2 : (255 - p) * 2; }

static effect_type_t parse_type(const char *name)
{
    if (!name) return FX_BREATH;
    if (!strcmp(name,"solid")) return FX_SOLID;
    if (!strcmp(name,"wave")) return FX_WAVE;
    if (!strcmp(name,"center_breath")) return FX_CENTER_BREATH;
    if (!strcmp(name,"chase")) return FX_CHASE;
    if (!strcmp(name,"sparkle")) return FX_SPARKLE;
    if (!strcmp(name,"heartbeat")) return FX_HEARTBEAT;
    return FX_BREATH;
}

static bool parse_hex(const char *text, uint8_t *r, uint8_t *g, uint8_t *b)
{
    unsigned int value;
    if (!text || text[0] != '#' || strlen(text) != 7 || sscanf(text + 1, "%06x", &value) != 1) return false;
    *r=value>>16; *g=value>>8; *b=value; return true;
}

static effect_config_t default_effect(void)
{
    return (effect_config_t){FX_BREATH,242,160,123,255,215,181,64,30,0,2600,true};
}

static void set_builtin(const char *preset)
{
    effect_config_t next=default_effect();
    if (!strcmp(preset,"happy")) next=(effect_config_t){FX_SPARKLE,255,179,71,255,224,138,96,75,18,900,true};
    else if (!strcmp(preset,"calm")) next=(effect_config_t){FX_BREATH,255,228,181,217,242,230,52,18,0,4200,true};
    else if (!strcmp(preset,"miss")) next=(effect_config_t){FX_CENTER_BREATH,233,149,121,255,208,181,78,28,2,3000,true};
    else if (!strcmp(preset,"sad")) next=(effect_config_t){FX_WAVE,37,74,135,122,159,209,42,16,0,3600,true};
    else if (!strcmp(preset,"quick_touch")) next=(effect_config_t){FX_CHASE,255,209,102,255,140,66,120,100,4,1500,true};
    else if (!strcmp(preset,"slow_touch")) next=(effect_config_t){FX_BREATH,233,162,143,255,224,194,86,25,0,3000,true};
    portENTER_CRITICAL(&s_lock); s_effect=next; portEXIT_CRITICAL(&s_lock);
}

void pet_leds_set_emotion(const char *emotion) { set_builtin(emotion ? emotion : "calm"); ESP_LOGI(TAG,"Base preset: %s",emotion ? emotion : "calm"); }

void pet_leds_apply_config(const char *preset, const cJSON *config)
{
    if (!config) { pet_leds_set_emotion(preset); return; }
    effect_config_t next=default_effect(); const cJSON *item;
    item=cJSON_GetObjectItem(config,"type"); next.type=parse_type(cJSON_IsString(item)?item->valuestring:NULL);
    uint8_t *channels[]={&next.r1,&next.g1,&next.b1,&next.r2,&next.g2,&next.b2};
    for (int c=0;c<2;++c) { char key[7]; snprintf(key,sizeof(key),"color%d",c+1); item=cJSON_GetObjectItem(config,key); if (!parse_hex(cJSON_IsString(item)?item->valuestring:"#000000",channels[c*3],channels[c*3+1],channels[c*3+2])) return; }
    item=cJSON_GetObjectItem(config,"brightness"); if(cJSON_IsNumber(item)) next.brightness=clamp8(item->valueint);
    item=cJSON_GetObjectItem(config,"speed"); if(cJSON_IsNumber(item)) next.speed=clamp8(item->valueint);
    item=cJSON_GetObjectItem(config,"sparkle"); if(cJSON_IsNumber(item)) next.sparkle=clamp8(item->valueint);
    item=cJSON_GetObjectItem(config,"period_ms"); if(cJSON_IsNumber(item)) next.period_ms=item->valueint<200?200:item->valueint>20000?20000:item->valueint;
    item=cJSON_GetObjectItem(config,"mirror"); if(cJSON_IsBool(item)) next.mirror=cJSON_IsTrue(item);
    portENTER_CRITICAL(&s_lock); s_effect=next; portEXIT_CRITICAL(&s_lock); ESP_LOGI(TAG,"Custom preset applied: %s",preset?preset:"unknown");
}

void pet_leds_trigger_touch(bool fast) { portENTER_CRITICAL(&s_lock); s_overlay_fast=fast; s_overlay_until_us=esp_timer_get_time()+(fast?1500000:3000000); portEXIT_CRITICAL(&s_lock); }

static void set_pixel(int row,int index,uint8_t r,uint8_t g,uint8_t b)
{
    if(row==0){led_strip_set_pixel(s_strip_a,index,r,g,b);return;}
#if CONFIG_PET_LED_CHAINED
    led_strip_set_pixel(s_strip_a,CONFIG_PET_LED_COUNT_A+CONFIG_PET_LED_COUNT_B-1-index,r,g,b);
#else
    led_strip_set_pixel(s_strip_b,CONFIG_PET_LED_COUNT_B-1-index,r,g,b);
#endif
}

static void render_pixel(effect_config_t *fx,bool overlay,bool fast,int row,int index,int count,uint8_t phase)
{
    uint16_t period=fx->period_ms?fx->period_ms:1000; uint16_t now=(uint32_t)(esp_timer_get_time()/1000)*fx->speed/period;
    uint16_t pos=(uint32_t)index*255/(count>1?count-1:1); if(row==1&&fx->mirror)pos=255-pos; uint8_t wave=triangle(now*8+pos+phase),amount;
    if(overlay&&fast){amount=triangle(phase+pos*2);fx->r1=255;fx->g1=209;fx->b1=102;fx->r2=255;fx->g2=100;fx->b2=30;}
    else if(overlay){amount=80+triangle(phase/2)/3;fx->r1=233;fx->g1=162;fx->b1=143;fx->r2=255;fx->g2=224;fx->b2=194;}
    else if(fx->type==FX_SOLID)amount=255; else if(fx->type==FX_WAVE)amount=wave;
    else if(fx->type==FX_CENTER_BREATH)amount=(255-(uint8_t)abs(127-(int)pos)*2)*(80+wave/2)/255;
    else if(fx->type==FX_CHASE){int chase=255-abs((int)((now*3+pos)&255)-128)*2;amount=(uint8_t)(chase<0?0:chase);}
    else if(fx->type==FX_HEARTBEAT)amount=triangle(now*16); else amount=40+wave*215/255;
    if(fx->type==FX_SPARKLE&&(esp_random()%100)<fx->sparkle)amount=255;
    uint16_t brightness=(uint16_t)fx->brightness*CONFIG_PET_LED_BRIGHTNESS/128;
    set_pixel(row,index,scale8(blend8(fx->r1,fx->r2,amount),brightness),scale8(blend8(fx->g1,fx->g2,amount),brightness),scale8(blend8(fx->b1,fx->b2,amount),brightness));
}

static void led_task(void *arg)
{
    uint8_t phase=0; while(true){effect_config_t fx;bool fast;int64_t until;portENTER_CRITICAL(&s_lock);fx=s_effect;fast=s_overlay_fast;until=s_overlay_until_us;portEXIT_CRITICAL(&s_lock);bool overlay=esp_timer_get_time()<until;
        for(int i=0;i<CONFIG_PET_LED_COUNT_A;++i)render_pixel(&fx,overlay,fast,0,i,CONFIG_PET_LED_COUNT_A,phase);for(int i=0;i<CONFIG_PET_LED_COUNT_B;++i)render_pixel(&fx,overlay,fast,1,i,CONFIG_PET_LED_COUNT_B,phase);led_strip_refresh(s_strip_a);
#if !CONFIG_PET_LED_CHAINED
        led_strip_refresh(s_strip_b);
#endif
        phase+=overlay&&fast?10:3;vTaskDelay(pdMS_TO_TICKS(33));}
}

static esp_err_t create_strip(gpio_num_t gpio,int count,led_strip_handle_t *result)
{
    led_strip_config_t config={.strip_gpio_num=gpio,.max_leds=count,.led_model=LED_MODEL_WS2812,.color_component_format=LED_STRIP_COLOR_COMPONENT_FMT_GRB,.flags.invert_out=false};
    led_strip_rmt_config_t rmt={.clk_src=RMT_CLK_SRC_DEFAULT,.resolution_hz=10*1000*1000,.mem_block_symbols=64,.flags.with_dma=true};
    return led_strip_new_rmt_device(&config,&rmt,result);
}

esp_err_t pet_leds_start(void)
{
    set_builtin("calm");
#if CONFIG_PET_LED_CHAINED
    ESP_RETURN_ON_ERROR(create_strip(PET_LED_A_GPIO,CONFIG_PET_LED_COUNT_A+CONFIG_PET_LED_COUNT_B,&s_strip_a),TAG,"strip creation failed");
#else
    ESP_RETURN_ON_ERROR(create_strip(PET_LED_A_GPIO,CONFIG_PET_LED_COUNT_A,&s_strip_a),TAG,"strip A creation failed");
    ESP_RETURN_ON_ERROR(create_strip(PET_LED_B_GPIO,CONFIG_PET_LED_COUNT_B,&s_strip_b),TAG,"strip B creation failed");led_strip_clear(s_strip_b);
#endif
    led_strip_clear(s_strip_a); if(xTaskCreate(led_task,"pet_leds",4096,NULL,4,NULL)!=pdPASS)return ESP_ERR_NO_MEM; return ESP_OK;
}
