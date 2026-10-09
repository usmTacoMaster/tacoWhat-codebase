// ESP32-CAM for the Tacover rover.
//
// Joins the rover's own access point ("Rover-Planet") as a station at a FIXED
// address on the same subnet as the rover, so the browser page served by the
// rover (192.168.4.1) can load the feed without mDNS or discovery:
//
//   rover     192.168.4.1        (SoftAP + HTTP :80)
//   camera    192.168.4.50:81    (this sketch; also the page's default Camera address)
//   phone     DHCP, ~192.168.4.2 (first lease — deliberately not used by the CAM)
//
//   http://192.168.4.50:81/stream   multipart/x-mixed-replace MJPEG
//
// Port 81, not 80: the standard CameraWebServer convention, and the page
// derives the URL from the Camera address with a fixed :81.

#include "esp_camera.h"
#include <WiFi.h>

// The rover's access point (Arduino/roverDraft2-sketch/roverDraft2-sketch.ino).
const char *ssid = "Rover-Planet";
const char *password = "rover1234";

// Fixed, unchanging address on the rover's subnet. .1 is the rover itself and
// the phone's DHCP leases start at .2, so .50 cannot clash with either.
IPAddress local_IP(192, 168, 4, 50);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(192, 168, 4, 1);

// AI Thinker ESP32-CAM pins
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

WiFiServer server(81);

void setup() {
  Serial.begin(115200);
  Serial.println();
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;   // small = smooth. Try FRAMESIZE_VGA later
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  if (psramFound()) {
    config.fb_count = 2;
  } else {
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }

  // Fixed address must be applied BEFORE begin().
  if (!WiFi.config(local_IP, gateway, subnet, dns)) {
    Serial.println("WiFi.config failed; falling back to DHCP");
  }
  WiFi.setSleep(false);                 // keep the stream smooth
  WiFi.begin(ssid, password);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Camera ready! Open http://");
  Serial.println(WiFi.localIP());
  Serial.print("Stream: http://");
  Serial.print(WiFi.localIP());
  Serial.println(":81/stream");

  server.begin();
}

void loop() {
  WiFiClient client = server.available();
  if (!client) return;

  String reqLine = client.readStringUntil('\n');
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line.length() <= 1) break;
  }

  if (reqLine.indexOf("GET /stream") >= 0) {
    client.print("HTTP/1.1 200 OK\r\n"
                 "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
                 "Access-Control-Allow-Origin: *\r\n\r\n");
    while (client.connected()) {
      camera_fb_t *fb = esp_camera_fb_get();
      if (!fb) break;
      client.print("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ");
      client.print(fb->len);
      client.print("\r\n\r\n");
      size_t sent = client.write(fb->buf, fb->len);
      client.print("\r\n");
      esp_camera_fb_return(fb);
      if (sent == 0) break;
    }
  }
  else if (reqLine.indexOf("GET /flash/on") >= 0) {
    digitalWrite(4, HIGH);
    client.print("HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\n\r\nflash on");
  }
  else if (reqLine.indexOf("GET /flash/off") >= 0) {
    digitalWrite(4, LOW);
    client.print("HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\n\r\nflash off");
  }
  else if (reqLine.indexOf("GET /photo") >= 0) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
      client.print("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: ");
      client.print(fb->len);
      client.print("\r\nAccess-Control-Allow-Origin: *\r\n\r\n");
      client.write(fb->buf, fb->len);
      esp_camera_fb_return(fb);
    }
  }
  else {
    client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                 "<html><body style='margin:0;background:#000;color:#fff;text-align:center'>"
                 "<img src='/stream' style='width:100%'><br>"
                 "<button onclick=\"fetch('/flash/on')\" style='font-size:20px;margin:10px'>Light ON</button>"
                 "<button onclick=\"fetch('/flash/off')\" style='font-size:20px;margin:10px'>Light OFF</button>"
                 "<a href='/photo' style='color:#fff;font-size:20px;margin:10px'>Photo</a>"
                 "</body></html>");
  }
  client.stop();
}
