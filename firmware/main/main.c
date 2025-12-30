#include <string.h>

#include "esp_camera.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

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

static esp_err_t stream_handler(httpd_req_t *req) {
#define STREAM_PART_BOUNDARY "123456789000000000000987654321"
  static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" STREAM_PART_BOUNDARY;
  static const char *STREAM_BOUNDARY = "\r\n--" STREAM_PART_BOUNDARY "\r\n";
  static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n";

  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);

  if (res != ESP_OK) {
    return res;
  }

  char hdr[64];

  while (1) {
    camera_fb_t *fb = esp_camera_fb_get();

    if (!fb) {
      return ESP_FAIL;
    }

    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));

    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      return res;
    }

    int hlen = snprintf(hdr, sizeof(hdr), STREAM_PART, fb->len);

    if (hlen <= 0 || hlen >= (int)sizeof(hdr)) {
      esp_camera_fb_return(fb);
      return ESP_FAIL;
    }

    res = httpd_resp_send_chunk(req, hdr, hlen);

    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      return res;
    }

    res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);

    if (res != ESP_OK) {
      return res;
    }
  }
}

static httpd_handle_t start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.stack_size = 8192;

  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) != ESP_OK) {
    return NULL;
  }

  httpd_uri_t uri = {
      .uri = "/stream",
      .method = HTTP_GET,
      .handler = stream_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(server, &uri);

  ESP_LOGI("HTTP", "server: http://" IPSTR "/stream", IP2STR(&ipaddr));
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

  if (err != ESP_OK)
    ESP_LOGE("CAM", "esp_camera_init failed: 0x%x", err);
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
  ESP_ERROR_CHECK(init_camera());

  if (!start_webserver()) {
    ESP_LOGE("APP", "Failed to start web server");
  }
}
