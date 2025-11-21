// ===== ESP32 DevKit + SSD1306 Aggregator =====
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// I2C(SSD1306) は SDA=GPIO21, SCL=GPIO22 を使用
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

// シリアル
// Serial1: RX=GPIO32 (M5 #1)
// Serial2: RX=GPIO25 (M5 #2)
#define RX1_PIN 32
#define TX1_PIN 33   // 未配線でOK（使わない）
#define RX2_PIN 25
#define TX2_PIN 26   // 未配線でOK（使わない）

// 判定用パラメータ
#define RSSI_INVALID   (-127)
#define NEAR_THRESHOLD (-75)
#define CLOSER_MARGIN  (3)
#define STALE_MS       (5000UL)  // この時間更新がなければ無効

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// M5Stack 1台分の状態
struct ReceiverState {
  int rssi1;
  int rssi2;
  uint32_t lastUpdate;
};

ReceiverState rx[2];

// シリアル受信用バッファ
char lineBuf[2][32];
uint8_t linePos[2] = {0, 0};

void handleLineFrom(int index, const char* line);
void readSerialPort(HardwareSerial &ser, int index);
void redrawDisplay(void);
void drawBeaconLine(int beaconId, int beaconIndex, int y);

// ----------------------------------------------------
void setup() {
  bool ok;

  Serial.begin(115200);
  delay(500);
  Serial.println("ESP32 Aggregator start");

  // レシーバ状態初期化
  rx[0].rssi1 = RSSI_INVALID;
  rx[0].rssi2 = RSSI_INVALID;
  rx[0].lastUpdate = 0;
  rx[1].rssi1 = RSSI_INVALID;
  rx[1].rssi2 = RSSI_INVALID;
  rx[1].lastUpdate = 0;

  // Serial1 : M5 #1
  Serial1.begin(115200, SERIAL_8N1, RX1_PIN, TX1_PIN);

  // Serial2 : M5 #2
  Serial2.begin(115200, SERIAL_8N1, RX2_PIN, TX2_PIN);

  // I2C (SSD1306)
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);

  ok = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!ok) {
    Serial.println("SSD1306 allocation failed");
    while (1) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ESP32 Aggregator");
  display.println("Waiting data...");
  display.display();
}

// ----------------------------------------------------
void loop() {
  static uint32_t lastDraw = 0;
  uint32_t now;

  // シリアルからの受信
  readSerialPort(Serial1, 0);  // M5 #1
  readSerialPort(Serial2, 1);  // M5 #2

  // 表示更新
  now = millis();
  if (now - lastDraw > 500) {
    redrawDisplay();
    lastDraw = now;
  }
}

// ----------------------------------------------------
// シリアルポートから 1 行読み込み
void readSerialPort(HardwareSerial &ser, int index) {
  char c;

  while (ser.available() > 0) {
    c = ser.read();
    if (c == '\n') {
      // 1行完了
      lineBuf[index][linePos[index]] = '\0';
      handleLineFrom(index, lineBuf[index]);
      linePos[index] = 0;
    } else if (c == '\r') {
      // 無視
    } else {
      if (linePos[index] < sizeof(lineBuf[index]) - 1) {
        lineBuf[index][linePos[index]] = c;
        linePos[index]++;
      }
    }
  }
}

// ----------------------------------------------------
// "ID,rssi1,rssi2" をパース
void handleLineFrom(int index, const char* line) {
  int id;
  int r1;
  int r2;
  int parsed;
  uint32_t now;

  parsed = sscanf(line, "%d,%d,%d", &id, &r1, &r2);
  if (parsed == 3) {
    rx[index].rssi1 = r1;
    rx[index].rssi2 = r2;
    now = millis();
    rx[index].lastUpdate = now;

    Serial.print("From RX");
    Serial.print(index + 1);
    Serial.print(" (ID=");
    Serial.print(id);
    Serial.print("): r1=");
    Serial.print(r1);
    Serial.print(" r2=");
    Serial.println(r2);
  } else {
    Serial.print("Parse error (idx=");
    Serial.print(index);
    Serial.print("): ");
    Serial.println(line);
  }
}

// ----------------------------------------------------
// 画面全体を描画
void redrawDisplay(void) {
  uint32_t now;

  now = millis();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("BLE Position Viewer");

  // Beacon #1, #2 の各行を描画
  drawBeaconLine(1, 0, 12);
  drawBeaconLine(2, 1, 32);

  // 下部に簡単なデバッグ情報（最終更新から何秒たったか）
  display.setCursor(0, 52);
  display.print("RX1:");
  display.print((now - rx[0].lastUpdate) / 1000);
  display.print("s RX2:");
  display.print((now - rx[1].lastUpdate) / 1000);
  display.print("s");

  display.display();
}

// ----------------------------------------------------
// Beacon 1 / 2 ごとの判定と表示
void drawBeaconLine(int beaconId, int beaconIndex, int y) {
  uint32_t now;
  int rA;
  int rB;
  bool validA;
  bool validB;
  bool nearA;
  bool nearB;
  char buf[32];

  now = millis();

  // beaconIndex 0 => Beacon1, 1 => Beacon2
  if (beaconIndex == 0) {
    rA = rx[0].rssi1;  // Receiver1 から見た B1
    rB = rx[1].rssi1;  // Receiver2 から見た B1
  } else {
    rA = rx[0].rssi2;  // Receiver1 から見た B2
    rB = rx[1].rssi2;  // Receiver2 から見た B2
  }

  validA = false;
  validB = false;

  if ((now - rx[0].lastUpdate) <= STALE_MS && rA > RSSI_INVALID) {
    validA = true;
  }
  if ((now - rx[1].lastUpdate) <= STALE_MS && rB > RSSI_INVALID) {
    validB = true;
  }

  nearA = false;
  nearB = false;

  if (validA && rA >= NEAR_THRESHOLD) {
    nearA = true;
  }
  if (validB && rB >= NEAR_THRESHOLD) {
    nearB = true;
  }

  display.setCursor(0, y);
  snprintf(buf, sizeof(buf), "B%d:", beaconId);
  display.print(buf);

  display.setCursor(28, y);

  if (!validA && !validB) {
    display.print("none");
  } else if (nearA && !nearB) {
    display.print("near R1");
  } else if (nearB && !nearA) {
    display.print("near R2");
  } else if (nearA && nearB) {
    if (rA > rB + CLOSER_MARGIN) {
      display.print("R1 closer");
    } else if (rB > rA + CLOSER_MARGIN) {
      display.print("R2 closer");
    } else {
      display.print("between R1/R2");
    }
  } else {
    // どちらからも見えているが「近く」はない場合
    if (validA && !validB) {
      display.print("far, R1 side");
    } else if (!validA && validB) {
      display.print("far, R2 side");
    } else {
      display.print("far from both");
    }
  }

  // 下の行に RSSI を小さく表示（デバッグ用）
  display.setCursor(0, y + 10);
  snprintf(buf, sizeof(buf), "R1:%3ddB  R2:%3ddB", rA, rB);
  display.print(buf);
}
