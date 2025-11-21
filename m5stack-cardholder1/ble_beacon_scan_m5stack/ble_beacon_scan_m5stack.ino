// ===== M5Stack: CAD-825 x2 Scanner (MAC address based) =====
#include <M5Stack.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

// ★ここをあなたのCAD-825のMACアドレス（小文字）にしています
//   F9:5C:BE:E5:99:23 -> f9:5c:be:e5:99:23
//   C8:91:CC:41:C6:D6 -> c8:91:cc:41:c6:d6
static const char* ADDR_1 = "f9:5c:be:e5:99:23";  // CAD-825 #1
static const char* ADDR_2 = "cb:91:cc:41:c6:d6";  // CAD-825 #2

static const int SCAN_SECONDS = 2;
static const int SCAN_INTERVAL = 45;
static const int SCAN_WINDOW   = 45;

static const int NEAR_THRESH   = -75;     // これ以上で「NEAR」
static const uint32_t STALE_MS = 4000;    // この時間受信なしで無効化

struct BeaconState {
  int rssi;
  uint32_t lastSeen;
  bool valid;
};

BeaconState b1 = { -127, 0, false };
BeaconState b2 = { -127, 0, false };
int lastFoundCount = 0;

// --- RSSIバー（矩形）描画 ---
void drawRssiBar(int x, int y, int rssi) {
  int pct = map(rssi, -90, -40, 0, 100);
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 100) {
    pct = 100;
  }
  const int W = 120;
  const int H = 12;

  M5.Lcd.drawRect(x, y, W, H, TFT_DARKGREY);
  M5.Lcd.fillRect(x + 1, y + 1, W - 2, H - 2, TFT_BLACK);

  int fillW = (W - 2) * pct / 100;
  if (fillW > 0) {
    M5.Lcd.fillRect(x + 1, y + 1, fillW, H - 2, TFT_CYAN);
  }
}

void drawUI() {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextSize(2);

  uint32_t now = millis();
  if (b1.valid && now - b1.lastSeen > STALE_MS) {
    b1.valid = false;
  }
  if (b2.valid && now - b2.lastSeen > STALE_MS) {
    b2.valid = false;
  }

  // タイトル
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.print("BLE Scan (CAD-825 x2)");
  M5.Lcd.setCursor(10, 40);
  M5.Lcd.printf("Found devices: %d", lastFoundCount);

  // 行位置
  int y = 80;
  int labelX = 10;
  int valX   = 120;
  int barX   = 10;

  // ============ CAD-825 #1 ============
  M5.Lcd.fillRect(0, y - 4, 320, 28, TFT_BLACK); // 行クリア
  M5.Lcd.setCursor(labelX, y);
  M5.Lcd.print("CAD-825 #1:");
  if (b1.valid) {
    M5.Lcd.setCursor(valX, y);
    M5.Lcd.printf("%4ddBm", b1.rssi);

    int barY = y + 24;
    drawRssiBar(barX, barY, b1.rssi);

    if (b1.rssi >= NEAR_THRESH) {
      M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
      M5.Lcd.setCursor(barX + 130, barY - 2);
      M5.Lcd.print("NEAR");
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
      M5.Lcd.fillRect(barX + 130, barY - 2, 80, 20, TFT_BLACK);
    }
  } else {
    M5.Lcd.setCursor(valX, y);
    M5.Lcd.print("no signal");
    M5.Lcd.fillRect(barX, y + 24, 200, 14, TFT_BLACK);
  }

  // ============ CAD-825 #2 ============
  y += 60;
  M5.Lcd.fillRect(0, y - 4, 320, 28, TFT_BLACK);
  M5.Lcd.setCursor(labelX, y);
  M5.Lcd.print("CAD-825 #2:");
  if (b2.valid) {
    M5.Lcd.setCursor(valX, y);
    M5.Lcd.printf("%4ddBm", b2.rssi);

    int barY = y + 24;
    drawRssiBar(barX, barY, b2.rssi);

    if (b2.rssi >= NEAR_THRESH) {
      M5.Lcd.setTextColor(TFT_GREEN, TFT_BLACK);
      M5.Lcd.setCursor(barX + 130, barY - 2);
      M5.Lcd.print("NEAR");
      M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
      M5.Lcd.fillRect(barX + 130, barY - 2, 80, 20, TFT_BLACK);
    }
  } else {
    M5.Lcd.setCursor(valX, y);
    M5.Lcd.print("no signal");
    M5.Lcd.fillRect(barX, y + 24, 200, 14, TFT_BLACK);
  }

  // どっちが近い？
  y += 60;
  M5.Lcd.fillRect(0, y - 6, 320, 36, TFT_BLACK);
  M5.Lcd.setCursor(10, y);
  M5.Lcd.setTextSize(3);
  String verdict = "---";
  if (b1.valid && b2.valid) {
    if (b1.rssi > b2.rssi + 3) {
      verdict = "Closer: #1";
    } else if (b2.rssi > b1.rssi + 3) {
      verdict = "Closer: #2";
    } else {
      verdict = "Similar distance";
    }
  } else if (b1.valid) {
    verdict = "Only #1 seen";
  } else if (b2.valid) {
    verdict = "Only #2 seen";
  } else {
    verdict = "No beacons";
  }
  M5.Lcd.print(verdict);

  // 文字サイズ戻し
  M5.Lcd.setTextSize(2);
}

void setup() {
  M5.begin();
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.print("Initializing BLE...");

  Serial.begin(115200);

  BLEDevice::init("M5Stack Scanner");
  BLEDevice::setPower(ESP_PWR_LVL_P7);

  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);     // 名前取得しやすい(今回は使ってないが有効でOK)
  scan->setInterval(SCAN_INTERVAL);
  scan->setWindow(SCAN_WINDOW);

  delay(300);
  drawUI();
}

void loop() {
  BLEScan* scan = BLEDevice::getScan();
  BLEScanResults results = scan->start(SCAN_SECONDS, false);
  lastFoundCount = results.getCount();

  uint32_t now = millis();
  int best1 = -127;
  int best2 = -127;
  bool seen1 = false;
  bool seen2 = false;

  Serial.printf("scan start===\n");
  for (int i = 0; i < results.getCount(); ++i) {
    BLEAdvertisedDevice dev = results.getDevice(i);

    std::string addr = dev.getAddress().toString();
    int r = dev.getRSSI();

    // デバッグしたければコメントアウト解除
    Serial.printf("Addr=%s RSSI=%d\n", addr.c_str(), r);

    if (addr == ADDR_1) {
      if (r > best1) {
        best1 = r;
      }
      seen1 = true;
    } else if (addr == ADDR_2) {
      if (r > best2) {
        best2 = r;
      }
      seen2 = true;
    }
  }
  Serial.printf("scan end===\n");

  if (seen1) {
    b1.rssi = best1;
    b1.lastSeen = now;
    b1.valid = true;
  }
  if (seen2) {
    b2.rssi = best2;
    b2.lastSeen = now;
    b2.valid = true;
  }

  scan->clearResults();
  drawUI();
  delay(200);
}
