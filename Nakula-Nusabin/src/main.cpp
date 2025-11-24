#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define SSID "IT THC 2"
#define PASS "taharica2024"

void setup() {
  Serial.begin(115200);
  WiFi.begin(SSID,PASS);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED){
    Serial.print(".");
  }
  Serial.println("\n Connected to :"+String(SSID));
  Serial.println("IP address: "+String(WiFi.localIP()));
  Serial.println("MAC address: "+String(WiFi.macAddress()));
}

void loop() {
  // put your main code here, to run repeatedly:
}
