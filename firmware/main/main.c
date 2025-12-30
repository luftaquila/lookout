#include <string.h>
#include <stdlib.h>

#include "esp_camera.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

static esp_ip4_addr_t ipaddr;

static httpd_handle_t s_httpd = NULL;

static uint8_t *s_latest_jpg = NULL;
static size_t s_latest_jpg_len = 0;

static SemaphoreHandle_t s_latest_mux = NULL;
static SemaphoreHandle_t s_cam_mux = NULL;

static void httpd_wake(void *arg) {
}

static esp_err_t update_latest_capture(void) {
  esp_err_t ret = ESP_FAIL;
  camera_fb_t *fb = NULL;

  xSemaphoreTake(s_cam_mux, portMAX_DELAY);

  sensor_t *s = esp_camera_sensor_get();

  if (s) {
    s->set_framesize(s, FRAMESIZE_QXGA);
  }

  fb = esp_camera_fb_get();
  if (fb) {
    esp_camera_fb_return(fb);
    fb = NULL;
  }

  for (int i = 0; i < 3; i++) {
    fb = esp_camera_fb_get();

    if (!fb) {
      break;
    }

    if (fb->format == PIXFORMAT_JPEG &&
        fb->len >= 2 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8 &&
        fb->width == 2048 && fb->height == 1536) {
      break;
    }

    esp_camera_fb_return(fb);
    fb = NULL;
  }

  if (s) {
    s->set_framesize(s, FRAMESIZE_SXGA);
  }

  xSemaphoreGive(s_cam_mux);

  if (!fb) {
    return ESP_FAIL;
  }

  uint8_t *new_buf = (uint8_t *)malloc(fb->len);
  if (!new_buf) {
    esp_camera_fb_return(fb);
    return ESP_ERR_NO_MEM;
  }

  memcpy(new_buf, fb->buf, fb->len);
  size_t new_len = fb->len;

  esp_camera_fb_return(fb);

  xSemaphoreTake(s_latest_mux, portMAX_DELAY);
  uint8_t *old = s_latest_jpg;
  s_latest_jpg = new_buf;
  s_latest_jpg_len = new_len;
  xSemaphoreGive(s_latest_mux);

  if (old) {
    free(old);
  }

  ret = ESP_OK;
  return ret;
}

static void capture_refresh_task(void *arg) {
  const TickType_t period = pdMS_TO_TICKS(5 * 60 * 1000);

  for (int i = 0; i < 10; i++) {
    if (update_latest_capture() == ESP_OK) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  while (1) {
    update_latest_capture();
    vTaskDelay(period);
  }
}

static esp_err_t capture_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");

  xSemaphoreTake(s_latest_mux, portMAX_DELAY);
  size_t len = s_latest_jpg_len;

  if (!s_latest_jpg || len == 0) {
    xSemaphoreGive(s_latest_mux);
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_sendstr(req, "No capture yet");
  }

  uint8_t *tmp = (uint8_t *)malloc(len);
  if (!tmp) {
    xSemaphoreGive(s_latest_mux);
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_sendstr(req, "No mem");
  }

  memcpy(tmp, s_latest_jpg, len);
  xSemaphoreGive(s_latest_mux);

  esp_err_t r = httpd_resp_send(req, (const char *)tmp, len);
  free(tmp);
  return r;
}

static void stream_task(void *arg) {
#define STREAM_PART_BOUNDARY "123456789000000000000987654321"
  static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" STREAM_PART_BOUNDARY;
  static const char *STREAM_BOUNDARY = "\r\n--" STREAM_PART_BOUNDARY "\r\n";
  static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n";

  httpd_req_t *req = (httpd_req_t *)arg;

  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    httpd_req_async_handler_complete(req);
    if (s_httpd) httpd_queue_work(s_httpd, httpd_wake, NULL);
    vTaskDelete(NULL);
    return;
  }

  char hdr[64];

  while (1) {
    camera_fb_t *fb = NULL;

    xSemaphoreTake(s_cam_mux, portMAX_DELAY);
    fb = esp_camera_fb_get();
    xSemaphoreGive(s_cam_mux);

    if (!fb) {
      break;
    }

    if (fb->format != PIXFORMAT_JPEG || fb->len < 2 || fb->buf[0] != 0xFF || fb->buf[1] != 0xD8) {
      esp_camera_fb_return(fb);
      continue;
    }

    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));

    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    int hlen = snprintf(hdr, sizeof(hdr), STREAM_PART, fb->len);

    if (hlen <= 0 || hlen >= (int)sizeof(hdr)) {
      esp_camera_fb_return(fb);
      break;
    }

    res = httpd_resp_send_chunk(req, hdr, hlen);

    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);

    if (res != ESP_OK) {
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }

  httpd_resp_send_chunk(req, NULL, 0);
  httpd_req_async_handler_complete(req);
  if (s_httpd) httpd_queue_work(s_httpd, httpd_wake, NULL);
  vTaskDelete(NULL);
}

static esp_err_t stream_handler(httpd_req_t *req) {
  httpd_req_t *async_req = NULL;

  esp_err_t r = httpd_req_async_handler_begin(req, &async_req);

  if (r != ESP_OK) {
    return r;
  }

  BaseType_t ok = xTaskCreatePinnedToCore(stream_task, "stream", 8192, (void *)async_req, 5, NULL, 1);
  if (ok != pdPASS) {
    httpd_req_async_handler_complete(async_req);

    if (s_httpd) {
      httpd_queue_work(s_httpd, httpd_wake, NULL);
    }

    return ESP_FAIL;
  }

  return ESP_OK;
}

static httpd_handle_t start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.stack_size = 8192;

  httpd_handle_t server = NULL;

  if (httpd_start(&server, &config) != ESP_OK) {
    return NULL;
  }

  s_httpd = server;

  httpd_uri_t stream_uri = {
      .uri = "/stream",
      .method = HTTP_GET,
      .handler = stream_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(server, &stream_uri);

  httpd_uri_t cap_uri = {
      .uri = "/capture",
      .method = HTTP_GET,
      .handler = capture_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(server, &cap_uri);

  ESP_LOGI("HTTP", "stream:  http://" IPSTR "/stream", IP2STR(&ipaddr));
  ESP_LOGI("HTTP", "capture: http://" IPSTR "/capture", IP2STR(&ipaddr));
  return server;
}

static esp_err_t init_camera(void) {
  camera_config_t c = {
      .pin_pwdn = PWDN_GPIO_NUM,
      .pin_reset = RESET_GPIO_NUM,
      .pin_xclk = XCLK_GPIO_NUM,
      .pin_sscb_sda = SIOD_GPIO_NUM,
      .pin_sscb_scl = SIOC_GPIO_NUM,

      .pin_d7 = Y9_GPIO_NUM,
      .pin_d6 = Y8_GPIO_NUM,
      .pin_d5 = Y7_GPIO_NUM,
      .pin_d4 = Y6_GPIO_NUM,
      .pin_d3 = Y5_GPIO_NUM,
      .pin_d2 = Y4_GPIO_NUM,
      .pin_d1 = Y3_GPIO_NUM,
      .pin_d0 = Y2_GPIO_NUM,
      .pin_vsync = VSYNC_GPIO_NUM,
      .pin_href = HREF_GPIO_NUM,
      .pin_pclk = PCLK_GPIO_NUM,

      .xclk_freq_hz = 20000000,
      .ledc_timer = LEDC_TIMER_0,
      .ledc_channel = LEDC_CHANNEL_0,

      .pixel_format = PIXFORMAT_JPEG,
      .frame_size = FRAMESIZE_SXGA,
      .jpeg_quality = 12,
      .fb_count = 2,
      .grab_mode = CAMERA_GRAB_LATEST,
  };

  esp_err_t err = esp_camera_init(&c);

  if (err != ESP_OK) {
    ESP_LOGE("CAM", "esp_camera_init failed: 0x%x", err);
  }

  return err;
}

static bool s_got_ip = false;

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data) {
  ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
  ipaddr = e->ip_info.ip;
  s_got_ip = true;
  ESP_LOGI("Wi-Fi", "Got IP: " IPSTR, IP2STR(&ipaddr));
}

static esp_err_t wifi_connect_blocking(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));

  wifi_config_t wc = {0};
  strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid));
  strncpy((char *)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password));
  wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_connect());

  ESP_LOGI("Wi-Fi", "Connecting Wi-Fi: %s", WIFI_SSID);

  while (!s_got_ip) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  return ESP_OK;
}

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(wifi_connect_blocking());

  s_latest_mux = xSemaphoreCreateMutex();
  s_cam_mux = xSemaphoreCreateMutex();

  if (!s_latest_mux || !s_cam_mux) {
    ESP_LOGE("APP", "Failed to create mutex");
    return;
  }

  ESP_ERROR_CHECK(init_camera());

  xTaskCreatePinnedToCore(capture_refresh_task, "cap_refresh", 4096, NULL, 5, NULL, 1);

  if (!start_webserver()) {
    ESP_LOGE("APP", "Failed to start web server");
  }
}
