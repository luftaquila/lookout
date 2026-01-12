#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_camera.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_http_server.h"
#include "nvs_flash.h"

#define START_HOUR 7
#define END_HOUR 19
#define BIT_WORKING_HOURS (1 << 0)
#define BIT_CAPTURE_BUSY (1 << 1)

static esp_ip4_addr_t ipaddr;
static httpd_handle_t s_httpd = NULL;

static uint8_t *s_latest_jpg   = NULL;
static size_t s_latest_jpg_len = 0;

static SemaphoreHandle_t s_latest_mux = NULL;
static SemaphoreHandle_t s_cam_mux    = NULL;

static EventGroupHandle_t s_evt_group;

static void time_keeper_task(void *arg) {
  while (1) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    bool is_time_synced = (timeinfo.tm_year > (2000 - 1900));
    bool is_working     = false;

    if (is_time_synced) {
      if (timeinfo.tm_hour >= START_HOUR && timeinfo.tm_hour < END_HOUR) {
        is_working = true;
      }
    } else {
      is_working = true;
    }

    if (is_working) {
      if (!(xEventGroupGetBits(s_evt_group) & BIT_WORKING_HOURS)) {
        ESP_LOGI("TIME", "Working hours started (Bit SET)");
        xEventGroupSetBits(s_evt_group, BIT_WORKING_HOURS);
      }
    } else {
      if (xEventGroupGetBits(s_evt_group) & BIT_WORKING_HOURS) {
        ESP_LOGI("TIME", "Working hours ended (Bit CLEARED)");
        xEventGroupClearBits(s_evt_group, BIT_WORKING_HOURS);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(60 * 1000));
  }
}

static void httpd_wake(void *arg) {}

static esp_err_t update_latest_capture(void) {
  camera_fb_t *fb = NULL;

  xEventGroupSetBits(s_evt_group, BIT_CAPTURE_BUSY);
  xSemaphoreTake(s_cam_mux, portMAX_DELAY);

  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QXGA);

  for (int i = 0; i < 3; i++) {
    fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  for (int i = 0; i < 20; i++) {
    fb = esp_camera_fb_get();

    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (fb->width != 2048 || fb->height != 1536 || fb->len < 2 || fb->buf[0] != 0xFF || fb->buf[1] != 0xD8) {
      esp_camera_fb_return(fb);
      fb = NULL;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    break;
  }

  if (!fb) {
    s->set_framesize(s, FRAMESIZE_SXGA);

    xSemaphoreGive(s_cam_mux);
    xEventGroupClearBits(s_evt_group, BIT_CAPTURE_BUSY);
    return ESP_FAIL;
  }

  xSemaphoreTake(s_latest_mux, portMAX_DELAY);

  memcpy(s_latest_jpg, fb->buf, fb->len);
  s_latest_jpg_len = fb->len;

  xSemaphoreGive(s_latest_mux);

  esp_camera_fb_return(fb);

  ESP_LOGI("CAM", "Capture Updated: %u bytes", s_latest_jpg_len);

  s->set_framesize(s, FRAMESIZE_SXGA);

  xSemaphoreGive(s_cam_mux);
  xEventGroupClearBits(s_evt_group, BIT_CAPTURE_BUSY);

  return ESP_OK;
}

static void capture_refresh_task(void *arg) {
  while (1) {
    EventBits_t bits = xEventGroupWaitBits(s_evt_group, BIT_WORKING_HOURS, pdFALSE, pdTRUE, portMAX_DELAY);

    if (bits & BIT_WORKING_HOURS) {
      update_latest_capture();
      vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    }
  }
}

static esp_err_t capture_handler(httpd_req_t *req) {
  EventBits_t bits = xEventGroupGetBits(s_evt_group);
  if (!(bits & BIT_WORKING_HOURS)) {
    httpd_resp_set_status(req, "403 Forbidden");
    return httpd_resp_sendstr(req, "Camera is sleeping (07:00 - 19:00 only)");
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");

  xSemaphoreTake(s_latest_mux, portMAX_DELAY);

  if (!s_latest_jpg) {
    xSemaphoreGive(s_latest_mux);
    httpd_resp_set_status(req, "503 Service Unavailable");
    return httpd_resp_sendstr(req, "Initializing...");
  }

  const size_t chunk_size = 16 * 1024;
  size_t remaining        = s_latest_jpg_len;
  uint8_t *ptr            = s_latest_jpg;

  while (remaining > 0) {
    size_t to_send = (remaining < chunk_size) ? remaining : chunk_size;

    esp_err_t res = httpd_resp_send_chunk(req, (const char *)ptr, to_send);

    if (res != ESP_OK) {
      xSemaphoreGive(s_latest_mux);
      return res;
    }

    ptr += to_send;
    remaining -= to_send;
  }

  httpd_resp_send_chunk(req, NULL, 0);

  xSemaphoreGive(s_latest_mux);
  return ESP_OK;
}

static void stream_task(void *arg) {
#define STREAM_PART_BOUNDARY "123456789000000000000987654321"
  static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" STREAM_PART_BOUNDARY;
  static const char *STREAM_BOUNDARY     = "\r\n--" STREAM_PART_BOUNDARY "\r\n";
  static const char *STREAM_PART         = "Content-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n";

  httpd_req_t *req = (httpd_req_t *)arg;

  if (!(xEventGroupGetBits(s_evt_group) & BIT_WORKING_HOURS)) {
    httpd_resp_set_status(req, "403 Forbidden");
    httpd_resp_sendstr(req, "Camera is sleeping (07:00 - 19:00 only)");
    httpd_req_async_handler_complete(req);
    vTaskDelete(NULL);
    return;
  }

  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);

  if (res != ESP_OK) {
    httpd_req_async_handler_complete(req);

    if (s_httpd) {
      httpd_queue_work(s_httpd, httpd_wake, NULL);
    }

    vTaskDelete(NULL);
    return;
  }

  char hdr[64];

  while (1) {
    if (!(xEventGroupGetBits(s_evt_group) & BIT_WORKING_HOURS)) {
      break;
    }

    if (xEventGroupGetBits(s_evt_group) & BIT_CAPTURE_BUSY) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    camera_fb_t *fb = NULL;

    if (xSemaphoreTake(s_cam_mux, portMAX_DELAY)) {
      fb = esp_camera_fb_get();
      xSemaphoreGive(s_cam_mux);
    } else {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
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

    vTaskDelay(pdMS_TO_TICKS(5));
  }

  httpd_resp_send_chunk(req, NULL, 0);
  httpd_req_async_handler_complete(req);

  if (s_httpd) {
    httpd_queue_work(s_httpd, httpd_wake, NULL);
  }

  vTaskDelete(NULL);
}

static esp_err_t stream_handler(httpd_req_t *req) {
  httpd_req_t *async_req = NULL;
  esp_err_t r            = httpd_req_async_handler_begin(req, &async_req);

  if (r != ESP_OK) {
    return r;
  }

  BaseType_t ok = xTaskCreate(stream_task, "stream", 8192, (void *)async_req, 5, NULL);
  if (ok != pdPASS) {
    httpd_req_async_handler_complete(async_req);

    if (s_httpd) {
      httpd_queue_work(s_httpd, httpd_wake, NULL);
    }

    return ESP_FAIL;
  }

  return ESP_OK;
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  const char *html =
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1' charset='UTF-8'>"
    "<style>"
    "body {height:100vh; margin:0; display:flex; flex-direction:column; align-items:center; justify-content:center;}"
    ".container {text-align:center;font-family:Arial;}"
    ".btns {display:flex; gap:15px; justify-content:center; margin-bottom:20px; }"
    "a {padding:15px 25px; text-decoration:none; background:#007bff; color:white; border-radius:5px;}"
    "a:hover {background:#0056b3;}"
    ".info {color:#666; line-height:1.6; }"
    "</style></head><body>"
    "<div class='container'>"
    "<h1 style='margin-bottom:2rem;'>RTst Parking Lot Camera</h1>"
    "<div class='btns'><a href='/stream'>Live Stream</a><a href='/capture'>Capture</a></div><div class='info'>"
    "<p><b>Working Time:</b> 07:00 ~ 19:00 KST</p>"
    "<p>Stream: 1280x1024 | Capture: 2048x1536</p>"
    "<p><i>* Capture image refreshes every 1 minute.</i></p>"
    "</div></div>"
    "</body></html>";

  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static httpd_handle_t start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port    = 80;
  config.stack_size     = 8192;

  httpd_handle_t server = NULL;

  if (httpd_start(&server, &config) != ESP_OK) {
    return NULL;
  }

  s_httpd = server;

  httpd_uri_t index_uri = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = index_handler,
    .user_ctx = NULL,
  };
  httpd_register_uri_handler(server, &index_uri);

  httpd_uri_t stream_uri = {
    .uri      = "/stream",
    .method   = HTTP_GET,
    .handler  = stream_handler,
    .user_ctx = NULL,
  };
  httpd_register_uri_handler(server, &stream_uri);

  httpd_uri_t cap_uri = {
    .uri      = "/capture",
    .method   = HTTP_GET,
    .handler  = capture_handler,
    .user_ctx = NULL,
  };
  httpd_register_uri_handler(server, &cap_uri);

  ESP_LOGI("HTTP", "stream:  http://" IPSTR "/stream", IP2STR(&ipaddr));
  ESP_LOGI("HTTP", "capture: http://" IPSTR "/capture", IP2STR(&ipaddr));
  return server;
}

static esp_err_t init_camera(void) {
  camera_config_t c = {
    .pin_pwdn     = 32,
    .pin_reset    = -1,
    .pin_xclk     = 0,
    .pin_sccb_sda = 26,
    .pin_sccb_scl = 27,

    .pin_d7    = 35,
    .pin_d6    = 34,
    .pin_d5    = 39,
    .pin_d4    = 36,
    .pin_d3    = 21,
    .pin_d2    = 19,
    .pin_d1    = 18,
    .pin_d0    = 5,
    .pin_vsync = 25,
    .pin_href  = 23,
    .pin_pclk  = 22,

    .xclk_freq_hz = 8000000,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size   = FRAMESIZE_SXGA,
    .jpeg_quality = 12,
    .fb_count     = 2,
    .grab_mode    = CAMERA_GRAB_LATEST,
  };

  esp_err_t err = esp_camera_init(&c);
  sensor_t *s   = esp_camera_sensor_get();
  s->set_vflip(s, 1);
  s->set_lenc(s, 1);
  s->set_dcw(s, 1);
  s->set_exposure_ctrl(s, 1);
  s->set_ae_level(s, -2);
  s->set_aec2(s, 1);
  s->set_aec_value(s, 1200);
  s->set_awb_gain(s, 0);
  s->set_brightness(s, 1);

  if (err != ESP_OK) {
    ESP_LOGE("CAM", "esp_camera_init failed: 0x%x", err);
  }

  return err;
}

static bool s_got_ip = false;

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data) {
  ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
  ipaddr               = e->ip_info.ip;
  s_got_ip             = true;
  ESP_LOGI("Wi-Fi", "Got IP: " IPSTR, IP2STR(&ipaddr));
}

static esp_err_t wifi_connect_blocking(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));

  wifi_config_t wc = { 0 };
  strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid));
  strncpy((char *)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password));
  wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
  ESP_ERROR_CHECK(esp_wifi_connect());

  ESP_LOGI("Wi-Fi", "Connecting Wi-Fi: %s", WIFI_SSID);

  while (!s_got_ip) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  return ESP_OK;
}

static void init_sntp(void) {
  ESP_LOGI("NTP", "Initializing SNTP");
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  setenv("TZ", "KST-9", 1);
  tzset();

  int retry             = 0;
  const int retry_count = 5;

  while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
    ESP_LOGI("NTP", "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }

  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  ESP_LOGI("NTP", "Current time: %s", asctime(&timeinfo));
}

void app_main(void) {
  ESP_ERROR_CHECK(nvs_flash_init());

  s_latest_jpg = (uint8_t *)heap_caps_malloc(256 * 1024, MALLOC_CAP_SPIRAM);

  if (!s_latest_jpg) {
    ESP_LOGE("APP", "Failed to allocate memory for latest jpg");
    return;
  }

  ESP_ERROR_CHECK(wifi_connect_blocking());
  init_sntp();

  s_evt_group  = xEventGroupCreate();
  s_latest_mux = xSemaphoreCreateMutex();
  s_cam_mux    = xSemaphoreCreateMutex();

  if (!s_latest_mux || !s_cam_mux) {
    ESP_LOGE("APP", "Failed to create mutex");
    return;
  }

  ESP_ERROR_CHECK(init_camera());

  xTaskCreate(time_keeper_task, "time_keeper", 4096, NULL, 3, NULL);
  xTaskCreate(capture_refresh_task, "cap_refresh", 4096, NULL, 5, NULL);

  if (!start_webserver()) {
    ESP_LOGE("APP", "Failed to start web server");
  }
}
