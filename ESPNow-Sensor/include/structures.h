// include/structures.h
#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <Arduino.h>

// Unified Struktur v3.2: Exakt dieselbe Paketgröße für Ultraschall und Radar!
typedef struct __attribute__((__packed__)) struct_sensor_payload {
    float pv1; float pv2; float pv3; float pv4; float pv5; // (20 Bytes)
    
    uint32_t sleep_seconds;   // 4 Bytes (Ganzzahl)
    int32_t last_rssi;        // 4 Bytes (Ganzzahl)
    float battery_voltage;    // 4 Bytes (Gleitkomma)
    
    uint8_t ok;               // 1 Byte
    uint8_t jumper;           // 1 Byte
    uint8_t ota_state;        // 1 Byte} struct_sensor_payload;
} struct_sensor_payload;

// Universelle ESP-NOW Nachrichtenstruktur
typedef struct __attribute__((__packed__)) struct_universal_message {
    uint8_t sensor_type; 
    uint8_t firmware_ver; 
    uint8_t payload[sizeof(struct_sensor_payload)]; 
} struct_universal_message;

// Union für die Modbus-Float-Konvertierung (bleibt für den Radar-Part aktiv)
union RegisterFloat { 
    uint16_t registers[2]; 
    float value; 
};

#endif
