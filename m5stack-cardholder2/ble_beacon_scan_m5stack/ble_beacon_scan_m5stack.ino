// ===== M5Stack: CAD-825 x2 Scanner + UART out =====
#include <M5Stack.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>

// ★この M5Stack の ID（1台目:1, 2台目:2）
#define RECEIVER_ID 2   // ← 2台目は 2 に変更してビルド

// ★ここをあなたの Beacon の MAC アドレス（小文字）に合わせてください
//   例:
//   F9:5C:BE:E5:99:23 -> "f9:5c:be:e5:99:23"
//   C8:91:CC:41:C6:D6 -> "c8:91:cc:41:c6:d6"
static const char* ADDR_1 = "f9:5c:be:e5:99:23";  // Beacon #1
static const char* ADDR_2 = "cb:91:cc:41:c6:d6";  // Beacon #2

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
  int pct;
  const int W = 120;
  const int H = 12;
  int fillW;

  pct = map(rssi, -90, -40, 0, 100);
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 100) {
    pct = 100;
  }

  M5.Lcd.drawRect(x, y, W, H, TFT_DARKGREY);
  M5.Lcd.fillRect(x + 1, y + 1, W - 2, H - 2, TFT_BLACK);

  fillW = (W - 2) * pct / 100;
  if (fillW > 0) {
    M5.Lcd.fillRect(x + 1, y + 1, fillW, H - 2, TFT_CYAN);
  }
}

// --- UI描画 ---
void drawUI() {
  uint32_t now;
  int y;
  int labelX;
  int valX;
  int barX;
  int barY;
  String verdict;

  now = millis();

  if (b1.valid && now - b1.lastSeen > STALE_MS) {
    b1.valid = false;
  }
  if (b2.valid && now - b2.lastSeen > STALE_MS) {
    b2.valid = false;
  }

  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextSize(2);

  // タイトル
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.print("BLE Scan (CAD-825 x2)");
  M5.Lcd.setCursor(10, 40);
  M5.Lcd.printf("Found: %d (ID=%d)", lastFoundCount, RECEIVER_ID);

  // 行位置
  y = 80;
  labelX = 10;
  valX   = 120;
  barX   = 10;

  // ============ Beacon #1 ============
  M5.Lcd.fillRect(0, y - 4, 320, 28, TFT_BLACK); // 行クリア
  M5.Lcd.setCursor(labelX, y);
  M5.Lcd.print("Beacon #1:");
  if (b1.valid) {
    M5.Lcd.setCursor(valX, y);
    M5.Lcd.printf("%4ddBm", b1.rssi);

    barY = y + 24;
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

  // ============ Beacon #2 ============
  y += 60;
  M5.Lcd.fillRect(0, y - 4, 320, 28, TFT_BLACK);
  M5.Lcd.setCursor(labelX, y);
  M5.Lcd.print("Beacon #2:");
  if (b2.valid) {
    M5.Lcd.setCursor(valX, y);
    M5.Lcd.printf("%4ddBm", b2.rssi);

    barY = y + 24;
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
}

// --- Pico に RSSI を送信 ---
void sendToPico() {
  int r1;
  int r2;

  r1 = -127;
  r2 = -127;

  if (b1.valid) {
    r1 = b1.rssi;
  }
  if (b2.valid) {
    r2 = b2.rssi;
  }

  // フォーマット: ID,rssi1,rssi2\n
  Serial2.print(RECEIVER_ID);
  Serial2.print(",");
  Serial2.print(r1);
  Serial2.print(",");
  Serial2.println(r2);
}

void setup() {
  BLEScan* scan;

  M5.begin();
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.print("Initializing BLE...");

  Serial.begin(115200);   // デバッグ用
  // UART2: RX=16, TX=17（M5Stack Core の標準）
  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  BLEDevice::init("M5Stack Scanner");
  BLEDevice::setPower(ESP_PWR_LVL_P7);

  scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(SCAN_INTERVAL);
  scan->setWindow(SCAN_WINDOW);

  delay(300);
  drawUI();
}

void loop() {
  BLEScan* scan;
  BLEScanResults results;
  uint32_t now;
  int best1;
  int best2;
  bool seen1;
  bool seen2;
  int i;
  int r;
  std::string addr;

  scan = BLEDevice::getScan();
  results = scan->start(SCAN_SECONDS, false);
  lastFoundCount = results.getCount();

  now = millis();
  best1 = -127;
  best2 = -127;
  seen1 = false;
  seen2 = false;

  Serial.printf("scan start=== (ID=%d)\n", RECEIVER_ID);

  for (i = 0; i < results.getCount(); ++i) {
    BLEAdvertisedDevice dev = results.getDevice(i);

    addr = dev.getAddress().toString();
    r = dev.getRSSI();

    // デバッグ
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

  // Pico へ送信
  sendToPico();

  // ローカル表示更新
  drawUI();

  delay(200);
}
