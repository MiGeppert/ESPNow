// include/structures.h
#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <Arduino.h>

// Gepackte Datenstruktur für Home Assistant
typedef struct __attribute__((__packed__)) struct_radar {
    float pv1; float pv2; float pv3; float pv4; float pv5;
    uint8_t ok; uint8_t jumper; uint8_t ota_state; float battery_voltage;
} struct_radar;

// Universelle ESP-NOW Nachrichtenstruktur
typedef struct __attribute__((__packed__)) struct_universal_message {
    uint8_t sensor_type; uint8_t firmware_ver; uint8_t payload[240]; 
} struct_universal_message;

// Union für die Modbus-Float-Konvertierung
union RegisterFloat { 
    uint16_t registers[2]; 
    float value; 
};

#endif
