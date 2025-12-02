#ifndef KONEKSI_H
#define KONEKSI_H

const char* awsEndpoint = "ax2qsc1xu59f8-ats.iot.ap-southeast-1.amazonaws.com";
const char* deviceId = "NakulaV2-01";
const char* MQTT_ENDPOINT = "804b020558094dddb356ee537943e3d4.s1.eu.hivemq.cloud";
const char* MQTT_PUBLISH_TOPIC = "/Nakula/telemetry";
const char* MQTT_CLIENT_ID = "Nakula";

int distance = 0;
int volumeSensor = 0;
int fillPercentageSensor = 0;
float totalVolume = 65.892;
int binHeight = 60; // in cm
unsigned long duration = 0;

#endif