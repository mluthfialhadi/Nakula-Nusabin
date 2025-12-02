#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_sleep.h> // Library untuk Deep Sleep
#include <Adafruit_AHTX0.h>

#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include <certificates.h> // Sertifikat root CA, client certificate, dan private key

#define SSID "IT THC 2"
#define PASS "taharica2024"

#define trig1 7
#define echo1 8

#define GSM_TX_PIN 17          // ESP32-S3 Pin TX (ke A9G RX)
#define GSM_RX_PIN 18          // ESP32-S3 Pin RX (ke A9G TX)
#define A9G_PWR_KEY_PIN 5      // Pin untuk pulsa Power On/Off
#define GSM_APN "internet"     // GANTI dengan APN operator Anda (misal: "3gprs", "indosatgprs")


// const String apithings = "http://119.18.158.14:8080/api/v1/";
// const String tokenthings = "KJBFkxVBdrKBCjlkKXdV";
// HTTPClient http;

Adafruit_AHTX0 aht;
GsmA9g gsmA9g;

const int DEEP_SLEEP_MINUTES = 10; 
const int DEEP_SLEEP_SEC = DEEP_SLEEP_MINUTES * 60;
const uint64_t DEEP_SLEEP_US = (uint64_t)DEEP_SLEEP_SEC * 1000000ULL;

void setup() {
  Serial.begin(115200);
  if (aht.begin()) {
    Serial.println("Found AHT10");
  } else {
    Serial.println("Didn't find AHT10");
  }

  pinMode(trig1, OUTPUT);
  pinMode(echo1, INPUT);
}
unsigned long told;
const long selang = 10*1000;
void loop() {
  // put your main code here, to run repeatedly:
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);// populate temp and humidity objects with fresh data
  Serial.print("Suhu: "+String(temp.temperature)+" °C ");
  Serial.println("Kelembaban: "+String(humidity.relative_humidity)+" %");

  /*
  if(millis()-told<selang){
    if(WiFi.status()==WL_CONNECTED){
      http.begin(apithings+tokenthings+"/telemetry");
      http.addHeader("Content-Type","application/json");
      String payload = "{";
            payload += "\"suhu\": "; payload += String(temp.temperature)+",";
            payload += "\"kelembaban\": "; payload += String(humidity.relative_humidity);
            payload += "}";
      int httpRespon = http.POST(payload);
      if(httpRespon==200){
        String respon = http.getString();
        Serial.println("Response: "+respon);
      }else {
        Serial.println("Error on sending POST: "+String(httpRespon));
      }
      http.end();
    }
  told = millis();
  }
  */
  
  delay(10);
}

float ultraSonic(void){
  digitalWrite(trig1, LOW);
  delayMicroseconds(2);
  digitalWrite(trig1, HIGH);
  delayMicroseconds(10);
  unsigned long traveltime = pulseIn(echo1, HIGH);
  float jarak = ((float(traveltime)/2.0)/29.1);
  return jarak;
}


class WiFiManager{
  private:
    const char* ssid;
    const char* password;
    int maxRetries = 20;
  public:
    WiFiManager(){
      ssid = SSID;
      password = PASS;
      maxRetries = 20;
    }
    WiFiManager(const char* ssid, const char* password, int maxRetries): ssid(ssid), password(password), maxRetries(maxRetries){}
    bool koneksi(){
      if(WiFi.status() == WL_CONNECTED){
        return true;
      }
      else{
        WiFi.begin(ssid, password);
        int counter = 0;
        Serial.print("Connecting to WiFi");
        while(WiFi.status() != WL_CONNECTED && counter < maxRetries){
          Serial.print(".");
          delay(500);
          counter++;
        }
        if(WiFi.status() == WL_CONNECTED){
          Serial.println("\nConnected to :"+String(ssid));
          Serial.println("IP address: "+String(WiFi.localIP()));
          Serial.println("MAC address: "+String(WiFi.macAddress()));
          return true;
        }else{
          Serial.println("\nFailed to connect to :"+String(ssid));
          return false;
        }
      }
    }
    bool shutdownWiFi(){
      if(WiFi.status() == WL_CONNECTED || WiFi.getMode() != WIFI_OFF){
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        return true;
      }
      else{
        return true;
      }
    }
    
};


class MqttKu {
private:
    WiFiClientSecure wifiClient;
    PubSubClient mqttClient;
    
    const char* endpoint;
    const char* publishTopic;
    const char* clientId;
    unsigned long lastPublish;

    void setupCertificates() {
        // Konfigurasi sertifikat pada klien WiFi aman
        wifiClient.setCACert(rootCA);
        wifiClient.setCertificate(certificate_pem_crt);
        wifiClient.setPrivateKey(private_pem_key);
    }

public:
    // Konstruktor
    MqttKu(const char* endpoint, const char* publishTopic, const char* clientId)
        : endpoint(endpoint), publishTopic(publishTopic), clientId(clientId), lastPublish(0) {
        
        // Atur sertifikat sebelum mengatur klien MQTT
        setupCertificates(); 
        
        mqttClient.setClient(wifiClient);
        // Port default untuk MQTTS (MQTT over TLS/SSL) adalah 8883
        mqttClient.setServer(endpoint, 8883); 
    }

    // Fungsi untuk memastikan koneksi MQTT tetap terjaga
    void checkConnection() {
        if (!mqttClient.connected()) {
            Serial.print("PubSubClient connecting to: "); 
            Serial.print(endpoint);
            // Coba koneksi dengan loop
            while (!mqttClient.connected()) {
                // Perlu ada delay agar loop tidak terlalu cepat dan membebani sistem
                delay(100); 
                Serial.print(".");
                // Parameter 1: Client ID, parameter 2 & 3 (username/password) opsional (tidak digunakan AWS IoT)
                if (!mqttClient.connect(clientId)) {
                     // Jika gagal, berikan waktu tunggu yang lebih lama sebelum mencoba lagi
                    delay(2000); 
                }
            }
            Serial.println(" connected");
        }
        // Harus dipanggil secara reguler di loop untuk menjaga koneksi dan memproses pesan masuk/keluar
        mqttClient.loop();
    }

    // Fungsi untuk mengirim data
    bool publish(const String& message) {
        // Panggil checkConnection() untuk memastikan kita terhubung sebelum publish
        checkConnection(); 

        boolean rc = mqttClient.publish(publishTopic, message.c_str());
        
        Serial.print("Published, rc="); 
        Serial.print(rc ? "OK: " : "FAILED: ");
        Serial.println(message);
        
        if (rc) {
            lastPublish = millis();
        }
        return rc;
    }

    // Fungsi untuk memeriksa apakah sudah waktunya publish
    bool shouldPublish(unsigned long interval = 30000) { // Default 30 detik (30 * 1000)
        return (millis() - lastPublish > interval);
    }
};

class GsmA9g {
private:
    HardwareSerial* gsmSerial = &Serial2;

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
    
    // Power On modul A9G
    void powerOn() {
        Serial.println("A9G Powering ON...");
        digitalWrite(A9G_PWR_KEY_PIN, LOW);
        delay(3000); 
        digitalWrite(A9G_PWR_KEY_PIN, HIGH);
        delay(8000); // Tunggu booting

        if (!sendCommand("AT", "OK", 5000)) {
            Serial.println("A9G Gagal merespon AT.");
        } else {
            Serial.println("A9G ON & Siap. Cek Sinyal: AT+CSQ");
            sendCommand("AT+CSQ", "+CSQ:", 2000); // Cek kualitas sinyal
        }
    }
    
    // Power Off modul A9G (Wajib untuk hemat daya)
    void powerOff() {
        Serial.println("A9G Powering OFF...");
        if (!sendCommand("AT+CPOWD=1", "POWER DOWN", 10000)) {
            Serial.println("Gagal CPOWD, mematikan via PWRKEY...");
            digitalWrite(A9G_PWR_KEY_PIN, LOW);
            delay(3000);
            digitalWrite(A9G_PWR_KEY_PIN, HIGH);
        }
    }
    
    // Fungsi utama pengiriman data
    bool kirimData(const String& payload, const char* apn, const char* server) {
        powerOn();
        bool success = false;
        
        Serial.println("Mencoba koneksi GPRS...");
        if (!sendCommand("AT+CGATT=1", "OK", 30000)) { Serial.println("Gagal GPRS Attach."); } 
        else if (!sendCommand(("AT+CSTT=\"" + String(apn) + "\",\"\",\"\"").c_str(), "OK", 5000)) { Serial.println("Gagal set APN."); } 
        else if (!sendCommand("AT+CIICR", "OK", 15000)) { Serial.println("Gagal Aktifkan GPRS."); } 
        else {
            Serial.println("GPRS Aktif. Mencoba koneksi ke server...");
            
            // --- Contoh Koneksi TCP/IP (Sesuaikan dengan protokol Anda) ---
            // Ganti ini dengan AT Command yang sesuai (e.g., AT+CIPSTART untuk TCP, atau AT+CMQTT untuk MQTT)
            // Protokol AWS MQTT melalui GPRS AT Command A9G sangat kompleks dan membutuhkan
            // banyak langkah, termasuk konfigurasi SSL/TLS AT commands (AT+SSLxxx).
            // Anggap saja berhasil setelah implementasi Anda:
            success = true; 
            // -------------------------------------------------------------
            
            sendCommand("AT+CIPSHUT", "SHUT OK"); 
        }
        
        powerOff(); // MATIKAN GSM SETELAH SELESAI
        return success;
    }
};
// Instance Global
GsmA9g gsmA9g;