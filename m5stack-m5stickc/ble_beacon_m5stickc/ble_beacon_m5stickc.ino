#include <M5StickC.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>

// ★1号機/2号機でここだけ変える
#define DEVICE_NAME "ESP32-2"   // ← 2号機は "ESP32-2"

void setup() {
  Serial.begin(115200);

  // 画面
  M5.begin();
  M5.Axp.ScreenBreath(10);
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(CYAN, BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 10); M5.Lcd.print(DEVICE_NAME);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 40); M5.Lcd.print("Advertising (Name only)");

  // BLE（名前だけを確実に流す：シンプル＆安定）
  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setPower(ESP_PWR_LVL_P7);

  BLEAdvertisementData advData;
  advData.setFlags(0x1A);        // LE General + BR/EDR Not Supported
  advData.setName(DEVICE_NAME);  // ★名前を広告に入れる

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->setAdvertisementData(advData);
  adv->setScanResponse(true);    // 名前はScan Responseでも返す（確実性UP）
  adv->setMinInterval(160);      // 100ms
  adv->setMaxInterval(160);
  adv->start();

  Serial.println("Name advertising started");
}

void loop() {
  delay(1000);
}
