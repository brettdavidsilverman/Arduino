#include <bitset>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <Adafruit_BME280.h>
#include <Adafruit_MCP23008.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "battery.h"
#include "esp_camera.h"
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "camera_pins.h"

#define SEALEVELPRESSURE_HPA (1024)

#define OLED_SCREEN_WIDTH 128 // OLED display width, in pixels
#define OLED_SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define OLED_SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

Adafruit_SSD1306 oledDisplay(OLED_SCREEN_WIDTH, OLED_SCREEN_HEIGHT, &Wire, OLED_RESET);

Adafruit_BME280 bme1; // I2C
Adafruit_BME280 bme2; // I2C

const char *ssid     = "MyAndroid";
const char *password = "feebeegeeb3";

void startCameraServer();
bool initializeLED();
bool initializeMultiplexer();
bool initializeWeather();
bool initializeOLEDDisplay();
bool initialiezCamera();
bool initializeWiFi();
void displayTemperature();
void displayError(const char* error);

bool status = false;
void printWeather(Adafruit_BME280& bme);

Adafruit_MCP23008 mcp;

void setup() {
    Serial.begin(115200);
    
    status = true;

    status &= initializeLED();
    status &= initializeMultiplexer();
    status &= initializeWeather();
    status &= initializeCamera();
    status &= initializeOLEDDisplay();
    status &= initializeWiFi();

    startCameraServer();

    Serial.print("Camera Ready! Use 'http://");
    Serial.print(WiFi.localIP());
    Serial.println("' to connect");
    
}

bool initializeLED() {
    Serial.setDebugOutput(true);
    Serial.println();
    pinMode(2, OUTPUT);
    digitalWrite(2, HIGH);
    Serial.println("LED initializaed");
    return true;
}

bool initializeWeather() {
 
  unsigned status1, status2;
    
  // default settings
  status1 = bme1.begin(0x76);
  status2 = bme2.begin(0x77);

  // You can also pass in a Wire library object like &Wire2
  // status = bme.begin(0x76, &Wire2)
  if (!status1 || !status2) {
      std::stringstream stream;
      stream << "Sensor ID 1 was: 0x" << std::hex << bme1.sensorID() << std::endl
             << "Sensor ID 2 was: 0x" << std::hex << bme2.sensorID() << std::endl;
      displayError(stream.str().c_str());
      return false;
  }

  Serial.println("Weather sensors initialized");
  return true;

}

void printWeather(Adafruit_BME280& bme) {

    Serial.print("Temp ");
    Serial.print(bme.readTemperature());
    Serial.print(" °C");

    Serial.print("\t");

    Serial.print("Press ");
    Serial.print(bme.readPressure() / 100.0F);
    Serial.print(" hPa");

    Serial.print("\t");

    Serial.print("Hum ");
    Serial.print(bme.readHumidity());
    Serial.print(" %");

    Serial.print("\t");

    Serial.print("Alt ");
    Serial.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
    Serial.print(" m");

    Serial.println();
}


bool initializeMultiplexer() {
    mcp.begin();      // use default address 0
    
    mcp.pinMode(0, OUTPUT);
    mcp.pinMode(1, OUTPUT);

    mcp.digitalWrite(0, HIGH);
    mcp.digitalWrite(1, LOW);
  
    Serial.println("Multiplexer initialized");
    return true;
}


bool initializeCamera() {
 //    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  // disable   detector
//    bat_init();
//    bat_hold_output();

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size   = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count     = 2;

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        displayError("Camera initialization failed");
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();
    // initial sensors are flipped vertically and colors are a bit saturated
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the blightness just a bit
    s->set_saturation(s, -2);  // lower the saturation

    // drop down frame size for higher initial frame rate
    s->set_framesize(s, FRAMESIZE_QVGA);

    Serial.println("Camera initializaed");

    return true;

}

bool initializeOLEDDisplay() {

  if(!oledDisplay.begin(SSD1306_SWITCHCAPVCC, OLED_SCREEN_ADDRESS)) {
    displayError("SSD1306 allocation failed");
    return false;
  }

  return true;

}

bool initializeWiFi() {
   Serial.printf("Connect to %s, %s\r\n", ssid, password);

    // If you want to use AP mode, you can use the following code
    // WiFi.softAP(ssid, password);
    // IPAddress IP = WiFi.softAPIP();
    // Serial.print("AP IP address: ");
    // Serial.println(IP);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");

    return true;
   
}

void displayError(const char* error) {
    while (1) {
        Serial.println(error);
        delay(1000);
    }
}

void loop() {
    // put your main code here, to run repeatedly:
    delay(100);
    digitalWrite(2, HIGH);
    delay(100);
    digitalWrite(2, LOW);

    Serial.println(WiFi.localIP());

    Serial.print("Weather 1\t");
    printWeather(bme1);
    Serial.print("Weather 2\t");
    printWeather(bme2);

    displayTemperature();

    Serial.println();

    delay(1000);
}


void displayTemperature() {

    std::stringstream stream;
    stream << std::setfill('0') << std::setw(4) << std::fixed << std::setprecision(2)
           << bme1.readTemperature()
           << "C";

    std::string temperature = stream.str();

    oledDisplay.clearDisplay();
    oledDisplay.setTextSize(2); // Draw 2X-scale text
    oledDisplay.setTextColor(SSD1306_WHITE);
    oledDisplay.setCursor(10, 10);
    oledDisplay.println(temperature.c_str());
    oledDisplay.display();

}