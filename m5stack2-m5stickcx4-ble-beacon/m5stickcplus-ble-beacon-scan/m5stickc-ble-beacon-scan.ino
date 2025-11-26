// ===== M5StickC: BLE Anchor (Scanner + ESP-NOW Sender) =====
//
// 動作:
//  1) 5秒間BLEスキャン
//  2) 見つけたMACごとに最大RSSIを記録
//  3) 結果リストを表示器(M5Stack2/Core2)へESP-NOW送信
//  4) 繰り返し
//
// ※ ANCHOR_ID を 1〜4 に変えて4台分用意してください
// ※ DISPLAY_WIFI_MAC は表示器の Wi-Fi MAC アドレスに書き換えてください

#include <M5StickCPlus.h>
#include <WiFi.h>
#include <esp_now.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ========== 設定 ==========

// このアンカーのID（1〜4でユニークにする）
#define ANCHOR_ID 4

// 表示器(M5Stack2/Core2) の Wi-Fi MAC アドレス（6バイト）
// → 表示器側で Serial.println(WiFi.macAddress()); して書き写す
uint8_t DISPLAY_WIFI_MAC[6] = { 0x84, 0xCC, 0xA8, 0x60, 0x0B, 0x8C };

// 最大保持デバイス数
const int MAX_DEVICES = 20;

// スキャン時間（秒）
const uint32_t SCAN_SECONDS = 5;

// ヘッダ高さ（この Y 座標より下をステータス表示に使う）
const int HEADER_HEIGHT = 48;

// ========== データ構造 ==========

typedef struct {
    uint8_t mac[6];
    int8_t  rssi;
} DeviceEntry;

DeviceEntry g_devices[MAX_DEVICES];
uint8_t     g_deviceCount = 0;

// ========== ESP-NOW 送信状態表示用 ==========

char g_peerMacStr[32];

volatile uint32_t g_sendCount    = 0;
volatile uint32_t g_successCount = 0;
volatile uint32_t g_failCount    = 0;
volatile esp_now_send_status_t g_lastStatus = ESP_NOW_SEND_FAIL;
volatile bool g_statusDirty = false;

// ========== ユーティリティ（MAC文字列化） ==========

void formatMac(const uint8_t *mac, char *buf, size_t len) {
    snprintf(buf, len,
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ========== BLE スキャンコールバック ==========

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        BLEAddress addr = advertisedDevice.getAddress();  // コンストラクタで初期化
        uint8_t (*native)[6];
        const uint8_t *addrBytes;
        int rssi;
        int index;
        int i;
        int j;
        bool match;

        native    = addr.getNative();
        addrBytes = *native;                // getNative() の戻り値対策
        rssi      = advertisedDevice.getRSSI();

        index = -1;
        for (i = 0; i < g_deviceCount; i++) {
            match = true;
            for (j = 0; j < 6; j++) {
                if (g_devices[i].mac[j] != addrBytes[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                index = i;
                break;
            }
        }

        if (index == -1) {
            if (g_deviceCount >= MAX_DEVICES) {
                return;
            }
            index = g_deviceCount;
            g_deviceCount++;

            for (j = 0; j < 6; j++) {
                g_devices[index].mac[j] = addrBytes[j];
            }
            g_devices[index].rssi = (int8_t)rssi;
        } else {
            if (rssi > g_devices[index].rssi) {
                g_devices[index].rssi = (int8_t)rssi;
            }
        }
    }
};

// ========== ESP-NOW コールバック ==========

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    g_sendCount++;
    if (status == ESP_NOW_SEND_SUCCESS) {
        g_successCount++;
    } else {
        g_failCount++;
    }
    g_lastStatus  = status;
    g_statusDirty = true;

    Serial.print("ESP-NOW send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// ========== ユーティリティ ==========

void clearDeviceList() {
    g_deviceCount = 0;
}

void dumpDeviceListToSerial() {
    int i;
    char buf[48];   // ← () を付けずにこれでOK

    Serial.println("---- Device List ----");
    for (i = 0; i < g_deviceCount; i++) {
        snprintf(buf, sizeof(buf),
                 "%02X:%02X:%02X:%02X:%02X:%02X RSSI=%d",
                 g_devices[i].mac[0], g_devices[i].mac[1], g_devices[i].mac[2],
                 g_devices[i].mac[3], g_devices[i].mac[4], g_devices[i].mac[5],
                 g_devices[i].rssi);
        Serial.println(buf);
    }
    Serial.println("---------------------");
}

// ESP-NOW 送信
void sendResultsByEspNow() {
    uint8_t buffer[2 + MAX_DEVICES * sizeof(DeviceEntry)];
    int i;
    int size;
    esp_err_t result;

    if (g_deviceCount == 0) {
        Serial.println("No devices to send");
        return;
    }

    buffer[0] = (uint8_t)ANCHOR_ID;
    buffer[1] = g_deviceCount;

    for (i = 0; i < g_deviceCount; i++) {
        memcpy(&buffer[2 + i * sizeof(DeviceEntry)],
               &g_devices[i],
               sizeof(DeviceEntry));
    }

    size = 2 + g_deviceCount * sizeof(DeviceEntry);

    result = esp_now_send(DISPLAY_WIFI_MAC, buffer, size);
    if (result == ESP_OK) {
        Serial.println("ESP-NOW send OK");
    } else {
        Serial.print("ESP-NOW send failed, err=");
        Serial.println(result);
    }
}

// 画面に MAC と送信状態を表示
void updateStatusDisplay() {
    uint32_t sendCount;
    uint32_t successCount;
    uint32_t failCount;
    esp_now_send_status_t lastStatus;
    float successRate;
    char line[32];

    sendCount    = g_sendCount;
    successCount = g_successCount;
    failCount    = g_failCount;
    lastStatus   = g_lastStatus;

    if (sendCount == 0) {
        successRate = 0.0f;
    } else {
        successRate = (float)successCount * 100.0f / (float)sendCount;
    }

    // ヘッダの下から下部までをクリアして描画
    M5.Lcd.fillRect(0, HEADER_HEIGHT, 160, 80, BLACK);
    M5.Lcd.setTextSize(1);

    // 1行目: Peer
    M5.Lcd.setCursor(0, HEADER_HEIGHT);
    M5.Lcd.print("Peer:");
    M5.Lcd.println(g_peerMacStr);

    // 2行目: Send
    M5.Lcd.setCursor(0, HEADER_HEIGHT + 12);
    snprintf(line, sizeof(line), "Send:%lu ", (unsigned long)sendCount);
    M5.Lcd.print(line);

    // 3行目: OK / NG
    snprintf(line, sizeof(line), "OK:%lu NG:%lu",
             (unsigned long)successCount,
             (unsigned long)failCount);
    M5.Lcd.println(line);

    // 4行目: Rate
    M5.Lcd.setCursor(0, HEADER_HEIGHT + 24);
    snprintf(line, sizeof(line), "Rate:%.1f%% ", successRate);
    M5.Lcd.print(line);

    // 5行目: Last
    snprintf(line, sizeof(line), "Last:%s",
             (lastStatus == ESP_NOW_SEND_SUCCESS) ? "OK" : "FAIL");
    M5.Lcd.println(line);
}

// ========== セットアップ ==========

void setup() {
    BLEScan *pScan;
    esp_now_peer_info_t peerInfo;

    M5.begin();
    Serial.begin(115200);

    M5.Lcd.setRotation(3);
    M5.Lcd.fillScreen(BLACK);

    // ヘッダ（タイトル＋ID）だけ大きな文字で描画
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("BLE Anchor");
    M5.Lcd.printf("ID = %d\n", ANCHOR_ID);

    // 送信先 MAC を文字列化しておく
    formatMac(DISPLAY_WIFI_MAC, g_peerMacStr, sizeof(g_peerMacStr));
    Serial.print("ESP-NOW Peer MAC: ");
    Serial.println(g_peerMacStr);

    // Wi-Fi/ESP-NOW 初期化
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_send_cb(onDataSent);

    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, DISPLAY_WIFI_MAC, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
    }

    // BLE 初期化
    BLEDevice::init("");
    pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), false);
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(80);

    // 初回のステータス表示
    updateStatusDisplay();
}

// ========== ループ ==========

void loop() {
    BLEScan *pScan;
    BLEScanResults *foundDevices;   // ライブラリの start() がポインタを返す前提
    int count;

    pScan = BLEDevice::getScan();

    Serial.println("Start BLE scan");
    clearDeviceList();

    // 戻り値をポインタとして扱う
    foundDevices = pScan->start(SCAN_SECONDS, false);
    count        = foundDevices->getCount();

    Serial.printf("Scan done. Found %d devices (raw)\n", count);

    dumpDeviceListToSerial();

    sendResultsByEspNow();

    if (g_statusDirty) {
        g_statusDirty = false;
        updateStatusDisplay();
    }

    delay(500);
}
