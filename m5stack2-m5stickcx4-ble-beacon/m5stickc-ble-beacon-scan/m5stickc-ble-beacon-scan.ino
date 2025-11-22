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

#include <M5StickC.h>
#include <WiFi.h>
#include <esp_now.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// ========== 設定 ==========

// このアンカーのID（1〜4でユニークにする）
#define ANCHOR_ID 1

// 表示器(M5Stack2/Core2) の Wi-Fi MAC アドレス（6バイト）
// → 表示器側で Serial.println(WiFi.macAddress()); して書き写す
uint8_t DISPLAY_WIFI_MAC[6] = { 0x84, 0xCC, 0xA8, 0x60, 0x7A, 0x04 };

// 最大保持デバイス数
const int MAX_DEVICES = 20;

// スキャン時間（秒）
const uint32_t SCAN_SECONDS = 5;

// ========== データ構造 ==========

typedef struct {
    uint8_t mac[6];
    int8_t  rssi;
} DeviceEntry;

DeviceEntry g_devices[MAX_DEVICES];
uint8_t     g_deviceCount = 0;

// ========== ESP-NOW 送信用パケット ==========
// 先頭2バイト: [0] AnchorID, [1] count
// 続けて DeviceEntry x count

// ========== BLE スキャンコールバック ==========

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        BLEAddress addr = advertisedDevice.getAddress();  // ★ここだけはコンストラクタで初期化必須
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

        // g_devices に登録/更新
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


// ========== ESP-NOW コールバック（任意） ==========

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    Serial.print("ESP-NOW send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// ========== ユーティリティ ==========

void clearDeviceList() {
    g_deviceCount = 0;
}

void dumpDeviceListToSerial() {
    Serial.println("---- Device List ----");
    for (int i = 0; i < g_deviceCount; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf),
                 "%02X:%02X:%02X:%02X:%02X:%02X RSSI=%d",
                 g_devices[i].mac[0], g_devices[i].mac[1], g_devices[i].mac[2],
                 g_devices[i].mac[3], g_devices[i].mac[4], g_devices[i].mac[5],
                 g_devices[i].rssi);
        Serial.println(buf);
    }
    Serial.println("---------------------");
}

// ========== ESP-NOW 送信 ==========

void sendResultsByEspNow() {
    if (g_deviceCount == 0) {
        Serial.println("No devices to send");
        return;
    }

    // パケットを一時バッファに構築
    uint8_t buffer[2 + MAX_DEVICES * sizeof(DeviceEntry)];
    buffer[0] = (uint8_t)ANCHOR_ID;
    buffer[1] = g_deviceCount;

    // DeviceEntry をコピー
    for (int i = 0; i < g_deviceCount; i++) {
        memcpy(&buffer[2 + i * sizeof(DeviceEntry)],
               &g_devices[i],
               sizeof(DeviceEntry));
    }

    int size = 2 + g_deviceCount * sizeof(DeviceEntry);

    esp_err_t result = esp_now_send(DISPLAY_WIFI_MAC, buffer, size);
    if (result == ESP_OK) {
        Serial.println("ESP-NOW send OK");
    } else {
        Serial.print("ESP-NOW send failed, err=");
        Serial.println(result);
    }
}

// ========== セットアップ ==========

void setup() {
    M5.begin();
    Serial.begin(115200);

    M5.Lcd.setRotation(3);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("BLE Anchor");
    M5.Lcd.printf("ID = %d\n", ANCHOR_ID);

    // Wi-Fi/ESP-NOW 初期化
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_send_cb(onDataSent);

    // 表示器をpeerとして登録（送信先）
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, DISPLAY_WIFI_MAC, 6);
    peerInfo.channel = 0;      // 同一チャンネルなら0でOK
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
    }

    // BLE 初期化
    BLEDevice::init("");
    BLEScan *pScan = BLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), false);
    pScan->setActiveScan(true);  // アクティブスキャン有効
    pScan->setInterval(100);
    pScan->setWindow(80);
}

// ========== ループ ==========

void loop() {
    BLEScan *pScan = BLEDevice::getScan();

    Serial.println("Start BLE scan");
    clearDeviceList();

    // 5秒間スキャン（ブロッキング）
    BLEScanResults foundDevices = pScan->start(SCAN_SECONDS, false);
    Serial.printf("Scan done. Found %d devices (raw)\n", foundDevices.getCount());

    // 結果ダンプ
    dumpDeviceListToSerial();

    // 結果をESP-NOWで送信
    sendResultsByEspNow();

    // 少し待ってから次のサイクルへ
    delay(500);
}
