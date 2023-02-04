#include "configs.h"

#include <WiFi.h>
#include <M5StickCPlus.h>
#include <ArduinoHttpClient.h>
#include <AsyncTCP_SSL.h>
#include <DivoomClient.h>

#include <ColorConverterLib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define LGFX_AUTODETECT
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <LGFX_AUTODETECT.hpp>

#define DIVOOM_PIXEL_SIZE 7
#define DIVOOM_PIXEL_SPACE 0

#define DIVOOM_PER_PAGE 5

// Wifi Credentials
WiFiClient wifi;

DivoomClient divoom_client(wifi, DIVOOM_EMAIL, DIVOOM_MD5_PASSWORD);

byte *tmp_frames_data;
byte rendering_frames_data[DIVOOM_ALL_FRAMES_SIZE];

int32_t center_x, center_y, start_x, start_y;
static LGFX lcd;

bool next_gif_ready = false;
bool request_next_gif = false;

DivoomFileInfoLite divoom_files_list[DIVOOM_PER_PAGE];
uint8_t divoom_files_list_count = 0;
uint8_t divoom_current_page = 1;
uint8_t divoom_current_gif_index = -1;
DivoomPixelBeanHeader divoom_pixel_bean_header;


uint16_t drawRGB24toRGB565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void OnParseSuccess(DivoomPixelBeanHeader header) {
  Serial.println("Success");
  divoom_pixel_bean_header = header;
  next_gif_ready = true;
}

void OnParseError(int8_t error) {
  Serial.println("ERROR");
  next_gif_ready = false;
  request_next_gif = true;
}

void renderFrames(void* param) {
  while (!next_gif_ready) {
    delay(1000);
  }

  uint8_t local_total_frames;
  int local_speed;

  uint8_t frame;
  uint32_t i;
  uint8_t x, y;

  uint8_t grid_x, grid_y;
  uint8_t draw_x, draw_y;

  uint8_t red, green, blue;
  double hue, saturation, lighting, value;
  uint16_t pixel_color;

  int max_times;
  int times;

  uint32_t start_pos;
  uint32_t real_i;

  while (true) {
    local_total_frames = divoom_pixel_bean_header.total_frames;
    local_speed = min(100, max(50, (int) divoom_pixel_bean_header.speed));

    memcpy(rendering_frames_data, tmp_frames_data, local_total_frames * DIVOOM_FRAME_SIZE);

    frame = 0;
    i = 0;
  
    max_times = (int) (5000 / local_speed / local_total_frames);
    max_times = max(3, max_times);
    times = 1;
    
    request_next_gif = true;
    while (true) {
      ++frame;
      if (frame > local_total_frames) {
        ++times;
        if (times >= max_times and next_gif_ready) {
          break;
        }
        frame = 1;
      }
  
      start_pos = (frame - 1) * DIVOOM_FRAME_SIZE;
  
      i = 0;
      x = 0;
      y = 0;
    
      grid_x = 0;
      grid_y = 0;
    
      while (i <= DIVOOM_FRAME_SIZE - 3) {
        real_i = start_pos + i;
        red = rendering_frames_data[real_i];
        green = rendering_frames_data[real_i + 1];
        blue = rendering_frames_data[real_i + 2];
        pixel_color = drawRGB24toRGB565(red, green, blue);
    
        draw_x = start_x + (grid_x * (DIVOOM_PIXEL_SIZE + DIVOOM_PIXEL_SPACE) * 16) + (x * (DIVOOM_PIXEL_SIZE + DIVOOM_PIXEL_SPACE));
        draw_y = start_y + (grid_y * (DIVOOM_PIXEL_SIZE + DIVOOM_PIXEL_SPACE) * 16) + (y * (DIVOOM_PIXEL_SIZE + DIVOOM_PIXEL_SPACE));
    
        lcd.fillRect(draw_x, draw_y, DIVOOM_PIXEL_SIZE, DIVOOM_PIXEL_SIZE - 1, pixel_color);
  
        ColorConverter::RgbToHsl(red, green, blue, hue, saturation, lighting);
        lighting -= 0.05;
        lighting = max(0.0, lighting);
        ColorConverter::HslToRgb(hue, saturation, lighting, red, green, blue);
        pixel_color = drawRGB24toRGB565(red, green, blue);
        lcd.drawLine(draw_x, draw_y + DIVOOM_PIXEL_SIZE - 1, draw_x + DIVOOM_PIXEL_SIZE - 1, draw_y + DIVOOM_PIXEL_SIZE - 1, pixel_color);
        lcd.drawLine(draw_x + DIVOOM_PIXEL_SIZE - 1, draw_y, draw_x + DIVOOM_PIXEL_SIZE - 1, draw_y + DIVOOM_PIXEL_SIZE - 1, pixel_color);
        
        i += 3;
        ++x;
    
        if ((i / 3) % 16 == 0) {
          x = 0;
          ++y;
        }
        /*
        if ((i / 3) % 256 == 0) {
          x = 0;
          y = 0;
    
          ++grid_x;
    
          if (grid_x == 2) {
            grid_x = 0;
            ++grid_y;
          }
        }
        */
      }
  
      delay(local_speed);
    }
  }
}

void requestNextGif() {
  next_gif_ready = false;

  divoom_current_page = random(1, 20);
  ++divoom_current_gif_index;

  if (divoom_files_list_count == 0 || divoom_current_gif_index >= divoom_files_list_count) {
    int category_id = random(0, 19);
    divoom_client.GetCategoryFileList(divoom_files_list, &divoom_files_list_count, category_id, divoom_current_page, DIVOOM_PER_PAGE);
    divoom_current_gif_index = -1;
    request_next_gif = true;
    return;
  }

  divoom_client.ParseFile(divoom_files_list[divoom_current_gif_index].file_id, tmp_frames_data);
}

void setup() {
  M5.begin();

  lcd.init();
  lcd.setRotation(0);
  lcd.setColorDepth(16);

  center_x = lcd.width() / 2;
  center_y = lcd.height() / 2;
  start_x = floor(center_x - 16 * DIVOOM_PIXEL_SIZE / 2);
  start_y = floor(center_y - 16 * DIVOOM_PIXEL_SIZE / 2);

  Serial.begin(115200);

  // WiFi Init
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print(F("[WIFI] connecting to network "));
  Serial.print(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println(F(" [WIFI] connected"));

  xTaskCreate(renderFrames, "renderFrames", DIVOOM_ALL_FRAMES_SIZE + 1000, NULL, 1, NULL);

  randomSeed(analogRead(0));
  while (!divoom_client.LogIn()) {
    Serial.println(F("Logging in..."));
  }

  divoom_client.OnParseSuccess(OnParseSuccess);
  divoom_client.OnParseError(OnParseError);

  tmp_frames_data = (byte*) malloc(DIVOOM_ALL_FRAMES_SIZE);

  request_next_gif = true;
}

void loop() {
  M5.update();

  if (M5.BtnA.wasReleased()) {
    Serial.println(F("pressed A"));
    divoom_client.AbortDownload();
  }

  if (request_next_gif) {
    request_next_gif = false;
    requestNextGif();
  }
}
