#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ← MUDE APENAS ESSA LINHA EM CADA ESP32
#define BEACON_NAME "BEACON_01"  // BEACON_02, BEACON_03, BEACON_04

void setup() {
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 0);
  display.println("BEACON");
  display.setCursor(35, 20);
  display.println("01");  // ← MUDE O NÚMERO
  display.setTextSize(1);
  display.drawLine(0, 40, 128, 40, SSD1306_WHITE);
  display.setCursor(0, 44);
  display.println("Transmitindo BLE...");
  display.setCursor(0, 54);
  display.println(BEACON_NAME);
  display.display();

  BLEDevice::init(BEACON_NAME);
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->start();
}

void loop() {
  delay(1000);
}