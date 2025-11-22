// ===== M5Stack Core2: RSSI Viewer =====
//
// ・ESP-NOWで4つのアンカーから MAC + RSSI リストを受信
// ・あらかじめ登録した 2つのビーコンMAC に対して
//   (ビーコン2) x (アンカー4) のRSSI表を表示
//
// ※ ビーコンのBLE MACアドレスを BEACON_MACS に設定してください
// ※ Wi-Fi MACアドレス(ESP-NOW用) はアンカー側に設定します

#include <M5Core2.h>   // 無印M5Stackの場合は <M5Stack.h> に変更
#include <WiFi.h>
#include <esp_now.h>

// ========== 設定 ==========

// ビーコンの BLE MAC アドレス（2個分）
const uint8_t BEACON_MACS[2][6] = {
    // 例1
    { 0xAC, 0x23, 0x3F, 0xAC, 0x66, 0x39 },
    // 例2
    { 0xAC, 0x23, 0x3F, 0xAC, 0x6C, 0x9C }
};

// アンカーの数（ID=1〜4）
const int NUM_ANCHORS = 4;
const int NUM_BEACONS = 2;

// データ有効期限（ミリ秒）
// この時間データが来なければ "--" 表示にする
const uint32_t DATA_TIMEOUT_MS = 15000;  // 15秒

// ========== 受信データ構造 (アンカー側と合わせる) ==========

typedef struct {
    uint8_t mac[6];
    int8_t  rssi;
} DeviceEntry;

// ========== 表示用テーブル ==========
//
// rssiTable[b][a] : b=0..1 (ビーコン), a=0..3 (アンカーID-1)
// lastUpdate[a]   : アンカーごとの最終更新時刻

int16_t   rssiTable[NUM_BEACONS][NUM_ANCHORS];
uint32_t  lastUpdate[NUM_ANCHORS];

// 自分の Wi-Fi MAC アドレス（文字列）
String g_wifiMacStr;

// ========== ユーティリティ関数 ==========

bool macEquals(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 6; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

String macToString(const uint8_t *mac) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

void initTable() {
    for (int b = 0; b < NUM_BEACONS; b++) {
        for (int a = 0; a < NUM_ANCHORS; a++) {
            rssiTable[b][a] = -127;  // 無効値
        }
    }
    for (int a = 0; a < NUM_ANCHORS; a++) {
        lastUpdate[a] = 0;
    }
}

// ========== ESP-NOW 受信コールバック ==========

void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    if (len < 2) {
        return;
    }
    uint8_t anchorId = incomingData[0];
    uint8_t count    = incomingData[1];

    Serial.print("onDataRecv: anchorId=");
    Serial.print(anchorId);
    Serial.print(" len=");
    Serial.print(len);
    Serial.print(" count=");
    Serial.println(count);

    if (anchorId == 0 || anchorId > NUM_ANCHORS) {
        Serial.println("  -> invalid anchorId, ignored");
        return;
    }
    int anchorIndex = anchorId - 1;

    // DeviceEntry 配列へのポインタ
    const uint8_t *ptr = incomingData + 2;
    int remaining = len - 2;

    int possibleCount = remaining / sizeof(DeviceEntry);
    if (count > possibleCount) {
        count = possibleCount; // 安全側で調整
    }

    // 受信時刻更新（アンカーの最後のスナップショット時刻）
    lastUpdate[anchorIndex] = millis();

    // ★ポイント1:
    // このアンカーからの今回のスナップショットに合わせて、
    // まずこのアンカー列の全ビーコン分の値を一度クリアしておく。
    // → 今回の一覧に含まれないビーコンは "--" 表示になる。
    for (int b = 0; b < NUM_BEACONS; b++) {
        rssiTable[b][anchorIndex] = -127;   // 無効値にリセット
    }

    // 各デバイスをチェック（今回見えているものだけ上書き）
    for (int i = 0; i < count; i++) {
        const DeviceEntry *dev = (const DeviceEntry *)(ptr + i * sizeof(DeviceEntry));

        // 2つのビーコンMACと比較
        for (int b = 0; b < NUM_BEACONS; b++) {
            if (macEquals(dev->mac, BEACON_MACS[b])) {
                rssiTable[b][anchorIndex] = dev->rssi;
            }
        }
    }
}

// ========== 画面表示 ==========

void drawTable() {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, 0);

    M5.Lcd.println("RSSI Viewer");
    M5.Lcd.println("");

    // ヘッダ行
    M5.Lcd.print("Bea-Anc    ");
    for (int a = 0; a < NUM_ANCHORS; a++) {
        M5.Lcd.printf("A%d", a + 1);
        if (a != NUM_ANCHORS - 1) {
            M5.Lcd.print("  ");
        }
    }
    M5.Lcd.println("");
    M5.Lcd.println("-------------------------");

    uint32_t now = millis();

    for (int b = 0; b < NUM_BEACONS; b++) {
        // ビーコンMACの短縮表示
        String macStr = macToString(BEACON_MACS[b]);
        // 下8文字くらいだけ表示（例: "E5:99:23"）
        String macShort = macStr.substring(9);

        M5.Lcd.printf("%d-%s ", b + 1, macShort.c_str());

        for (int a = 0; a < NUM_ANCHORS; a++) {
            // データ有効性チェック（アンカー自体が生きているか）
            bool validAnchor = (lastUpdate[a] != 0) &&
                               (now - lastUpdate[a] <= DATA_TIMEOUT_MS);

            if (!validAnchor || rssiTable[b][a] <= -120) {
                M5.Lcd.print(" -- ");
            } else {
                M5.Lcd.printf("%3d ", rssiTable[b][a]);
            }
        }
        M5.Lcd.println("");
    }

    M5.Lcd.println("");
    M5.Lcd.println("Data: max RSSI per 5s");

    // ==== 画面下端に Wi-Fi MAC を小さく表示 ====
    M5.Lcd.setTextSize(1);
    int16_t y = 220;   // rotation(1) のときのだいたい下端付近
    M5.Lcd.setCursor(0, y);
    M5.Lcd.print("WiFi: ");
    M5.Lcd.print(g_wifiMacStr);
}

// ========== セットアップ ==========

void setup() {
    M5.begin();
    Serial.begin(115200);

    M5.Lcd.setRotation(1);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("RSSI Viewer");

    // Wi-Fi/ESP-NOW 初期化
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // 自分のWi-Fi MAC（アンカー側設定用）
    g_wifiMacStr = WiFi.macAddress();  // 文字列として保存
    Serial.print("WiFi MAC: ");
    Serial.println(g_wifiMacStr);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_recv_cb(onDataRecv);

    initTable();
}

// ========== ループ ==========

void loop() {
    static uint32_t lastDraw = 0;
    uint32_t now = millis();

    // 5秒に1回くらい画面更新
    if (now - lastDraw > 5000) {
        lastDraw = now;
        drawTable();
    }

    delay(100);
}
