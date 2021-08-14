#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <endian.h>

#define wireClockSpeed 4000000

#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"

//extern bool frame2bmp(camera_fb_t * fb, uint8_t ** out, size_t * out_len);
//bool frame2bmp(camera_fb_t * fb, uint8_t ** out, size_t * out_len)

#include <RGBConverter.h>

static const uint8_t I2C_SDA = 15;
static const uint8_t I2C_SCL = 13;

#include "DHT.h"

#define DHTPIN 14//0 //4     // Digital pin connected to the DHT sensor
// Feather HUZZAH ESP8266 note: use pins 3, 4, 5, 12, 13 or 14 --
// Pin 15 can work but DHT must be disconnected during program upload.

// Uncomment whatever type you're using!
#define DHTTYPE DHT11   // DHT 11
DHT dht(DHTPIN, DHTTYPE);

#define POWER_LED 33
#define FLASH_LED 4

const char* ssid = "Bee";//"Telstra044F87";
const char* password = "feebeegeeb3";//"ugbs3e85p5";
const char* host_name = "esp32-take-picture";

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

static const char *TAG = "example:take_picture";

static camera_config_t camera_config = {
  
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

    //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, //PIXFORMAT_JPEG,  /*!< Format of the pixel data: PIXFORMAT_ + YUV422|GRAYSCALE|RGB565|JPEG  */
    .frame_size = FRAMESIZE_UXGA,    /*!< Size of the output image: FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA  */

    .jpeg_quality = 10, //0-63 lower number means higher quality
    .fb_count = 1,       //if more than one, i2s runs in continuous mode. Use only with JPEG
//    .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};

#define BYTES_PER_PIXEL 3

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* CONTENT_TYPE = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t init_camera()
{
    //initialize the camera

    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera Init Failed");
        return err;
    }
  
    ///sensor_t * s = esp_camera_sensor_get();
    //s->set_brightness(s, 100); // up the brightness just a bit
    //s->set_framesize(s, FRAMESIZE_SVGA);

    return ESP_OK;
}


httpd_handle_t camera_httpd = NULL;

typedef struct {
        size_t size; //number of values used for filtering
        size_t index; //current value index
        size_t count; //value count
        int sum;
        int * values; //array to be filled with values
} ra_filter_t;


byte checkEndTransmission(const char* message) {
  byte result = Wire.endTransmission();
  if (result == 0) {
     //Serial.println("Ok");
  }
  else {
    Serial.print(message);
    Serial.print(" error ");
    Serial.println(result);
  }

  return result;
}

static esp_err_t get_image_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    
    esp_err_t res = ESP_OK;
    char part_buf[64];

    static int64_t last_frame = 0;
    if(!last_frame) {
        last_frame = esp_timer_get_time();
    }

    sensor_t * s = esp_camera_sensor_get();
    
    s->set_quality(s, 10);
    s->set_framesize(s, FRAMESIZE_SXGA);

    //pinMode(FLASH_LED, OUTPUT);
    //digitalWrite(FLASH_LED, HIGH);

    fb = esp_camera_fb_get();
    
    //digitalWrite(FLASH_LED, LOW);

    if (!fb) {
        Serial.println("Camera capture failed");
        res = ESP_FAIL;
    }

     
    if(res == ESP_OK) {
      res = httpd_resp_set_status(req, "200");
      res = httpd_resp_set_type(req, "image/jpeg");
    }
    
    if(res == ESP_OK){
        snprintf((char *)part_buf, 64, "%u", fb->len);
        Serial.print("Content-Length: ");
        Serial.printf(part_buf);
        Serial.println("");
        res = httpd_resp_set_hdr(req, "Content-Type", "image/jpeg");
        res = httpd_resp_set_hdr(req, "Content-Length", part_buf);
    }

    if(res == ESP_OK){
        res = httpd_resp_send(req, (const char *)(fb->buf), fb->len);
    }

    if(fb){
        esp_camera_fb_return(fb);
        fb = NULL;
    }

    int64_t fr_end = esp_timer_get_time();

    int64_t frame_time = fr_end - last_frame;
    last_frame = fr_end;
    frame_time /= 1000;
    Serial.printf(
      "(%.1ffps)\n",
      1000.0 / (uint32_t)frame_time
    );

    return res;
}

/*
struct fileheader {
    uint8_t signature[2];
    uint32_t filesize;
    uint32_t reserved;
    uint32_t fileoffset_to_pixelarray;
} ;

struct bitmapinfoheader{
    uint32_t dibheadersize;
    uint32_t width;
    uint32_t height;
    uint16_t planes;
    uint16_t bitsperpixel;
    uint32_t compression;
    uint32_t imagesize;
    uint32_t ypixelpermeter;
    uint32_t xpixelpermeter;
    uint32_t numcolorspallette;
    uint32_t mostimpcolor;
};

struct bitmap {
    struct fileheader _fileheader;
    struct bitmapinfoheader _bitmapinfoheader;
};

#define BITMAP_HEADER_SIZE 54

bool _frame2bmp(camera_fb_t * fb, uint8_t** bmp, size_t* bmpLength) {

  Serial.println("_frame2bmp");
  bitmap _bmp;
  memset(&_bmp, 0, sizeof(bitmap));
  
  _bmp._fileheader.signature[0] = 'B';
  _bmp._fileheader.signature[1] = 'M';

  Serial.println("Setting up bitmap header");
  Serial.print("Size of bitmap: ");
  Serial.println(sizeof(bitmap));

  uint32_t filesize = sizeof(bitmap) + fb->width * fb->height * 2;

  _bmp._fileheader.filesize = htole32(filesize);
  _bmp._bitmapinfoheader.width = htole32(fb->width);
  _bmp._bitmapinfoheader.height = htole32(fb->height);
  _bmp._bitmapinfoheader.bitsperpixel = htole16(16);
  _bmp._bitmapinfoheader.imagesize = htole32(fb->width * fb->height * 2);
  _bmp._fileheader.fileoffset_to_pixelarray = htole32(sizeof(bitmap));

  Serial.print("Allocating bitmap");

  *bmp = (uint8_t*)ps_malloc(filesize);

  Serial.println("Copying bitmap header");

  memcpy(*bmp, &_bmp, sizeof(bitmap));
  uint8_t* pixels = *bmp + sizeof(bitmap);

  Serial.println("Copying bitmap pixels");

  memcpy(pixels, fb->buf, fb->width * fb->height * 2);

  *bmpLength = filesize;

  Serial.println("_frame2bmp exit");
  if (((bitmap*)(*bmp))->_fileheader.filesize == htole32(*bmpLength))
    return true;
  return false;
  return frame2bmp(fb, bmp, bmpLength);

}
*/

bool capturing = false;

esp_err_t takePicture() {

    esp_err_t res = ESP_OK;

    camera_fb_t * fb = NULL;
    sensor_t * s = esp_camera_sensor_get();

    //s->reset(s);
    //s->set_pixformat(s, PIXFORMAT_JPEG);
    s->set_framesize(s, FRAMESIZE_QVGA);
    s->set_quality(s, 0);

    pinMode(FLASH_LED, OUTPUT);
    digitalWrite(FLASH_LED, HIGH);

    fb = esp_camera_fb_get();
    
    digitalWrite(FLASH_LED, LOW);

    if (!fb) {
        Serial.println("Camera capture failed");
        res = ESP_FAIL;
    }

    uint8_t* bmp = nullptr;
    size_t bmpLength = 0;

    bool converted = false;
    if (fb)
      converted = frame2bmp(fb, &bmp, &bmpLength);

    if(fb) {
        esp_camera_fb_return(fb);
        fb = NULL;
    }

    if (converted) {
      Serial.println("Convert frame buffer to bmp");
      Serial.print("Sending ");
      Serial.println(bmpLength);
      res = sendBitmap(bmp, bmpLength);

      free(bmp);
    }
    else
      res = 1;

    
  
    Serial.println("Took picture");

    return res;
    
}

const uint32_t chunkSize = 32;

int sendBitmap(uint8_t* bmp, size_t length) {
  int result = 0;

  uint32_t chunkCount = (uint32_t)ceil((float)length / (float)chunkSize);

  for (uint32_t chunk = 0; chunk  < chunkCount;  ++chunk) {

      Wire.beginTransmission(0x08);
        
        uint32_t chunkStart = chunk * chunkSize;
        uint32_t chunkEnd = chunkStart + chunkSize;
        if (chunkEnd > length)
            chunkEnd = length;
        uint32_t chunkLength = chunkEnd - chunkStart;
        Wire.write(bmp + chunkStart,  chunkLength);
      
      if ((result = checkEndTransmission("Data Chunk")) != 0) {
        Serial.print("Broke at chunk ");
        Serial.println(chunk);
        break;
      }
  }

  if (result == 0)
    Serial.println("Image sent");
  else
    Serial.println("Error sending image");

  return result;
}

int64_t frame_count = 0;
int64_t frame_time_sum = 0;

static esp_err_t get_stream_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    char * part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    sensor_t * s = esp_camera_sensor_get();
    s->set_quality(s, 10);
    s->set_framesize(s, FRAMESIZE_XGA);

    frame_count = 0;
    frame_time_sum = 0;

    while(!capturing) {

      int64_t frame_start =  esp_timer_get_time();
  
      fb = esp_camera_fb_get();
      
      if (!fb) {
          Serial.println("Camera capture failed");
          res = ESP_FAIL;
      }


      //Serial.printf("Size(%u)", fb->len);

      if(res == ESP_OK){
          res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
      }
      if(res == ESP_OK){
          size_t hlen = snprintf((char *)part_buf, 64, CONTENT_TYPE, fb->len);
          res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
      }
      if(res == ESP_OK){
          res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
      }

      if(fb){
          esp_camera_fb_return(fb);
          fb = NULL;
      }
  
      if(res != ESP_OK){
          Serial.println("Client disconnected.");
          break;
      }

      int64_t frame_end = esp_timer_get_time();
  
      int64_t frame_time = frame_end - frame_start;
      frame_time_sum += frame_time;
      if (++frame_count == 10) {
        Serial.printf(
          "(%.1ffps)\n",
          (float)frame_count * ((float)frame_time_sum / 1000.0 / 1000.0)
        );
        frame_time_sum = 0;
        frame_count = 0;
      }

    }
    
    return res;
}

void logMemory() {
  log_d("Total heap: %d", ESP.getHeapSize());
  log_d("Free heap: %d", ESP.getFreeHeap());
  log_d("Total PSRAM: %d", ESP.getPsramSize());
  log_d("Free PSRAM: %d", ESP.getFreePsram());
}


void setup()
{

  Serial.begin(115200);
  Serial.println();


  if(ESP_OK != init_camera()) {
    Serial.println("Failed to initialize camera");
    while (1)
      ;
  }
  Serial.println("Camera initialized");
  
  pinMode(POWER_LED, OUTPUT);
  digitalWrite(POWER_LED, LOW);
  

  Serial.print("Connecting to Wifi ");
  Serial.print(ssid);

  Serial.print(" ");
  Serial.print(host_name);
  WiFi.setHostname(host_name);
  
  WiFi.begin(ssid, password);

  for (int i = 0; i < 40 && (WiFi.status() != WL_CONNECTED); ++i) {
    delay(250);
    digitalWrite(POWER_LED, LOW);
    delay(250);
    digitalWrite(POWER_LED, HIGH);

    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Restarting");
    ESP.restart();
  }
  
  //WiFi.softAP(ssid, password);

  //IPAddress IP = WiFi.softAPIP();
  //Serial.print("AP IP address: ");
  //Serial.println(IP);

  digitalWrite(POWER_LED, HIGH);
  pinMode(POWER_LED, INPUT);

  Serial.println("");
  Serial.println("WiFi connected");

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();


  httpd_uri_t get_image_uri = {
    .uri       = "/image",
    .method    = HTTP_GET,
    .handler   = get_image_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t get_stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = get_stream_handler,
    .user_ctx  = NULL
  };

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(wireClockSpeed);

  Serial.println("Wire started in master mode.");

  dht.begin();
  Serial.println("DHT11 Temperature sensor started.");

  config.server_port = 80;
  Serial.printf("Starting server on port: '%d'\n", config.server_port);
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &get_stream_uri);
    httpd_register_uri_handler(camera_httpd, &get_image_uri);
    Serial.println("Camera Ready! Use: ");
    Serial.print("http://");
    Serial.print(WiFi.localIP());
    //Serial.print(IP);
    Serial.print("/image");
    Serial.println("");
    Serial.print("http://");
    Serial.print(WiFi.localIP());
    //Serial.print(IP);
    Serial.print("/stream");
    Serial.println("");
  }
  else
    Serial.println("Error starting server");

}


enum Process {
  touchWait,
  touchDown,
  touchUp,
  start,
  header,
  pixels,
  footer
};


void loop() {


  // put your main code here, to run repeatedly:
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Resetting");
    ESP.restart();
  }

  if (!capturing) {
    Wire.requestFrom(0x08, 1);
    Process process = (Process)Wire.read();    // receive a byte as character
    //Serial.println(process);
    if (process == Process::start) {
      capturing = true;
      Serial.println("takePicture()");
      takePicture();
      capturing = false;
    }
  }

  if (false && millis() % 10000 == 0) {
    // Reading temperature or humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
    float h = dht.readHumidity();
    // Read temperature as Celsius (the default)
    float t = dht.readTemperature();
    // Read temperature as Fahrenheit (isFahrenheit = true)
    float f = dht.readTemperature(true);

    // Check if any reads failed and exit early (to try again).
    if (!isnan(h) && !isnan(t) && !isnan(f)) {

      // Compute heat index in Fahrenheit (the default)
      float hif = dht.computeHeatIndex(f, h);
      // Compute heat index in Celsius (isFahreheit = false)
      float hic = dht.computeHeatIndex(t, h, false);

      Serial.print(F("Humidity: "));
      Serial.print(h);
      Serial.print(F("%  Temperature: "));
      Serial.print(t);
      Serial.print(F("°C "));
      Serial.print(f);
      Serial.print(F("°F  Heat index: "));
      Serial.print(hic);
      Serial.print(F("°C "));
      Serial.print(hif);
      Serial.println(F("°F"));
    }

  }
  
}