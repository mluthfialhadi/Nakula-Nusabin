#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h> 
#include <Adafruit_AHTX0.h>
#include "DFRobot_ADXL345.h"
#include <DFRobot_ITG3200.h>
#include <DFRobot_QMC5883.h>
#include <HardwareSerial.h>
#include <esp_sleep.h> 
#include <esp_bt.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <koneksi.h>
#include <certificates.h>
// =======================================================
// A. KREDENSIAL & KONFIGURASI GLOBAL
// =======================================================

// Kredensial AWS (Pastikan variabel ini ada di file lain atau definisikan di sini jika perlu)
extern const char* rootCA;
extern const char* certificate_pem_crt;
extern const char* private_pem_key;
extern const char* MQTT_ENDPOINT;
extern const char* MQTT_PUBLISH_TOPIC;
extern const char* MQTT_CLIENT_ID;

// Konfigurasi WiFi (Hardcode untuk tes)
const char* WIFI_SSID = "IT THC 2";
const char* WIFI_PASS = "SmFtZ2FkYW5nMDEy";
// const char* WIFI_PASS = "SmFtZ2FkYW5nMDEy";

// Setting Interval Deep Sleep (10 Menit)
const int DEEP_SLEEP_MINUTES = 1; 
const uint64_t DEEP_SLEEP_US = (uint64_t)DEEP_SLEEP_MINUTES * 60 * 1000000ULL;

// Variabel Global Mode (Diubah via BLE)
// false = GSM (Default), true = WiFi
bool useWiFiMode = true; 

// Pinout
#define GSM_TX_PIN 17          
#define GSM_RX_PIN 18          
#define A9G_PWR_KEY_PIN 5      
const char* GSM_APN = "internet"; 

#define BLUETOOTH_SWITCH_PIN 27 
#define trig1 7
#define echo1 8

// BLE UUID
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" 
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" 

// Global Objects
Adafruit_AHTX0 aht;
BLEServer* pServer = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;
BLECharacteristic* pRxCharacteristic = nullptr; // <--- TAMBAHKAN BARIS INI
bool deviceConnected = false;
const unsigned long BLUETOOTH_TIMEOUT_MS = 5 * 60 * 1000; 

// =======================================================
// B. CLASS GsmA9g (Menangani Hardware & AT Command)
// =======================================================
class GsmA9g {
private:
    HardwareSerial* gsmSerial = &Serial1;

    // Helper: Kirim AT Command dan tunggu respon
    bool sendCommand(const char* cmd, const char* expected_resp, unsigned long timeout = 5000) {
        gsmSerial->println(cmd);
        unsigned long start = millis();
        while (millis() - start < timeout) {
            if (gsmSerial->available()) {
                String response = gsmSerial->readStringUntil('\n');
                if (response.indexOf(expected_resp) != -1) {
                    return true;
                }
            }
        }
        return false;
    }

public:
    GsmA9g() {
        pinMode(A9G_PWR_KEY_PIN, OUTPUT);
        digitalWrite(A9G_PWR_KEY_PIN, HIGH); 
        gsmSerial->begin(115200, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
    }
    
    void powerOn() {
        digitalWrite(A9G_PWR_KEY_PIN, LOW); delay(3000); 
        digitalWrite(A9G_PWR_KEY_PIN, HIGH); delay(8000); 
        sendCommand("AT", "OK", 5000);
    }
    
    void powerOff() {
        if (!sendCommand("AT+CPOWD=1", "POWER DOWN", 10000)) {
            digitalWrite(A9G_PWR_KEY_PIN, LOW); delay(3000);
            digitalWrite(A9G_PWR_KEY_PIN, HIGH);
        }
    }
    
    bool activateGPRS(const char* apn) {
        if (!sendCommand("AT+CGATT=1", "OK", 30000)) return false; 
        if (!sendCommand(("AT+CSTT=\"" + String(apn) + "\",\"\",\"\"").c_str(), "OK", 5000)) return false; 
        if (!sendCommand("AT+CIICR", "OK", 15000)) return false; 
        return true; 
    }

    // --- LOGIKA MQTT GSM PINDAH KE SINI (Enkapsulasi yang Benar) ---
    
    bool connectMQTT(const char* endpoint, const char* clientId) {
        Serial.println("GSM: Setup TCP & MQTT...");
        // Setup TCP
        String setupCmd = "AT+MTCPIPSETUP=0,\"" + String(endpoint) + "\",8883";
        if (!sendCommand(setupCmd.c_str(), "+MTCPIPSETUP: 0,1", 30000)) return false;

        // Connect MQTT
        String connCmd = "AT+MQTTCONN=0,\"" + String(clientId) + "\"";
        if (!sendCommand(connCmd.c_str(), "+MQTTCONN: 0,1", 30000)) {
            sendCommand("AT+MTCPIPCLOSE=0", "+MTCPIPCLOSE: 0,1");
            return false;
        }
        return true;
    }

    bool publishMQTT(const char* topic, const String& payload) {
        Serial.println("GSM: Publishing...");
        String pubCmd = "AT+MQTTPUB=0,\"" + String(topic) + "\",\"" + payload + "\"";
        return sendCommand(pubCmd.c_str(), "+MQTTPUB: 0,1", 20000);
    }

    void disconnectMQTT() {
        sendCommand("AT+MQTTDISC=0", "+MQTTDISC: 0,1", 5000); 
        sendCommand("AT+MTCPIPCLOSE=0", "+MTCPIPCLOSE: 0,1", 5000);
    }
};

// =======================================================
// C. CLASS MqttKu (Manager Pengiriman Data)
// =======================================================
class MqttKu {
private:
    WiFiClientSecure wifiClient;
    PubSubClient mqttClient;
    
    const char* endpoint;
    const char* publishTopic;
    const char* clientId;
    
    void setupCertificates() {
        wifiClient.setCACert(rootCA);
        // wifiClient.setCertificate(certificate_pem_crt); // Uncomment jika perlu
        // wifiClient.setPrivateKey(private_pem_key);      // Uncomment jika perlu
    }

public:
    MqttKu(const char* endpoint, const char* publishTopic, const char* clientId)
        : endpoint(endpoint), publishTopic(publishTopic), clientId(clientId) {
        setupCertificates(); 
        mqttClient.setClient(wifiClient);
        mqttClient.setServer(endpoint, 8883); 
    }

    // Fungsi Publish Pintar
    // Menerima pointer ke GsmA9g. Jika pointer null, berarti mode WiFi.
    bool publish(const String& payload, GsmA9g* gsmObj = nullptr) {
        
        // --- MODE WIFI ---
        if (WiFi.status() == WL_CONNECTED && gsmObj == nullptr) {
            if (!mqttClient.connected()) {
                 Serial.print("WiFi MQTT Connecting...");
                 if (!mqttClient.connect(clientId)) { 
                     Serial.println("Fail"); return false; 
                 }
                 Serial.println("OK");
            }
            mqttClient.loop();
            bool rc = mqttClient.publish(publishTopic, payload.c_str());
            Serial.println(rc ? "WiFi Publish OK." : "WiFi Publish GAGAL.");
            return rc;
        } 
        
        // --- MODE GSM (Delegasi ke Class GsmA9g) ---
        else if (gsmObj != nullptr) {
            Serial.println("Delegasi ke GSM A9G...");
            
            // 1. Connect MQTT via GSM Class
            if (!gsmObj->connectMQTT(endpoint, clientId)) {
                Serial.println("GSM MQTT Connect Gagal.");
                return false;
            }
            
            // 2. Publish via GSM Class
            bool rc = gsmObj->publishMQTT(publishTopic, payload);
            Serial.println(rc ? "GSM Publish OK." : "GSM Publish GAGAL.");
            
            // 3. Disconnect
            gsmObj->disconnectMQTT();
            
            return rc;
        }
        
        return false;
    }
};

// Inisialisasi Objek Global
GsmA9g gsmA9g;
MqttKu mqttku(MQTT_ENDPOINT, MQTT_PUBLISH_TOPIC, MQTT_CLIENT_ID); 

// =======================================================
// D. FUNGSI SENSOR & LOGIKA UTAMA
// =======================================================

float ultraSonic(void){
    digitalWrite(trig1, LOW);
    delayMicroseconds(2);
    digitalWrite(trig1, HIGH);
    delayMicroseconds(10);
    digitalWrite(trig1, LOW);
    unsigned long traveltime = pulseIn(echo1, HIGH, 30000); // Timeout 30ms
    if(traveltime == 0) return 0.0;
    return ((float(traveltime)*0.0343)/2);
}

void perform_data_upload() {
    // 1. Ambil Data Sensor
    if (!aht.begin()) { Serial.println("Gagal AHT10."); }
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);

    float jarak_ultrasonik = ultraSonic();
    Serial.print("Jarak: "); Serial.println(jarak_ultrasonik);
    
    String payload = "{";
    payload += "\"suhu\": " + String(temp.temperature, 2) + ", ";
    payload += "\"kelembaban\": " + String(humidity.relative_humidity, 2) + ", ";
    payload += "\"jarak\": " + String(jarak_ultrasonik, 2);
    payload += "}";

    // 2. Cek Mode Pengiriman (WiFi atau GSM)
    if (useWiFiMode) {
        Serial.println("Mode: WIFI Selected.");
        
        // Coba koneksi WiFi Sesaat
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        int tryConnect = 0;
        while (WiFi.status() != WL_CONNECTED && tryConnect < 20) {
            delay(500); Serial.print("."); tryConnect++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            // Panggil MqttKu tanpa parameter GSM (nullptr) -> Mode WiFi
            mqttku.publish(payload);
        } else {
            Serial.println("Gagal Konek WiFi.");
        }
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);

    } else {
        Serial.println("Mode: GSM Selected.");
        
        // Nyalakan GSM
        gsmA9g.powerOn();
        
        // Aktifkan GPRS
        if (gsmA9g.activateGPRS(GSM_APN)) {
            // Panggil MqttKu DENGAN parameter object gsmA9g -> Mode GSM
            // MqttKu akan memanggil fungsi connectMQTT/publishMQTT milik gsmA9g
            mqttku.publish(payload, &gsmA9g); 
        } else {
            Serial.println("Gagal GPRS.");
        }
        
        // Matikan GSM
        gsmA9g.powerOff();
    }
}

// =======================================================
// E. BLUETOOTH CALLBACKS & SETUP
// =======================================================

class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            String command = String(rxValue.c_str());
            Serial.println("BLE Cmd: " + command);
            
            // Ganti Mode via Bluetooth
            if (command.indexOf("SET_MODE=WIFI") != -1) {
                useWiFiMode = true;
                pTxCharacteristic->setValue("Mode set to WIFI");
                pTxCharacteristic->notify();
            } 
            else if (command.indexOf("SET_MODE=GSM") != -1) {
                useWiFiMode = false;
                pTxCharacteristic->setValue("Mode set to GSM");
                pTxCharacteristic->notify();
            }
            else if (command.indexOf("GET_STATUS") != -1) {
                String status = "Pengunaan Koneksi:" + String(useWiFiMode ? "WIFI" : "GSM");
                pTxCharacteristic->setValue(status.c_str());
                pTxCharacteristic->notify();
            }
        }
    }
};

class ServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; Serial.println("BLE Client Connected."); };
    void onDisconnect(BLEServer* pServer) { 
        deviceConnected = false; 
        Serial.println("BLE Client Disconnected.");
        pServer->getAdvertising()->start(); 
    }
};

void init_ble_nus() {
    Serial.println("Inisialisasi BLE NUS...");
    // PERBAIKAN: Hapus kode controller init manual yang berbahaya untuk S3
    //BLEDevice::init("ESP32S3_Remote");
    
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
    pRxCharacteristic->setCallbacks(new MyCallbacks());
    pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902()); 
    pService->start();
    pServer->getAdvertising()->start();
}

void manage_bluetooth_and_loop() {
    init_ble_nus();
    unsigned long startTime = millis();
    while (digitalRead(BLUETOOTH_SWITCH_PIN) == LOW && (millis() - startTime) < BLUETOOTH_TIMEOUT_MS) {
        delay(100); 
    }
    BLEDevice::deinit(); 
    Serial.println("BLE OFF. Kembali tidur.");
}

void go_to_deep_sleep() {
    Serial.println("*** Masuk Deep Sleep " + String(DEEP_SLEEP_MINUTES) + "m ***");
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_US); 
    esp_deep_sleep_start(); 
}

void go_to_light_sleep() {
    Serial.println("Masuk Light Sleep (Idle)...");
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_37, 0); 
    esp_light_sleep_start();
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        Serial.println("Bangun: Switch ON");
    }
}

// =======================================================
// F. MAIN SETUP & LOOP
// =======================================================

void setup() {
    Serial.begin(115200);
    delay(2000); 
    pinMode(BLUETOOTH_SWITCH_PIN, INPUT_PULLUP);
    pinMode(echo1, INPUT);
    pinMode(trig1, OUTPUT);
    
    Serial.println("\n=== ESP32-S3 Boot ===");
    WiFi.mode(WIFI_OFF);
    
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
        // Bangun rutin untuk kirim data
        perform_data_upload();
        go_to_deep_sleep();
    } else {
        // Bangun manual / Reset
        if (digitalRead(BLUETOOTH_SWITCH_PIN) == LOW) {
            manage_bluetooth_and_loop();
        }
        esp_sleep_enable_timer_wakeup(DEEP_SLEEP_US); 
        go_to_light_sleep();
    }
}

void loop() {
    // Loop hanya aktif setelah bangun dari Light Sleep via Switch
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        manage_bluetooth_and_loop();
    }
    go_to_light_sleep();
}