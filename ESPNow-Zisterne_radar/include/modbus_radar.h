// include/modbus_radar.h
#ifndef MODBUS_RADAR_H
#define MODBUS_RADAR_H

#include <Arduino.h>
#include <ModbusMaster.h>
#include "structures.h"

// Externe Objekte aus der main.cpp bekannt machen
extern HardwareSerial SensorSerial;
extern ModbusMaster node;

// Hardware-Pins für den Ultraschall-Modus
#define US_TRIG_PIN 12
#define US_ECHO_PIN 14

// Universelle Funktion zum Auslesen der Hardware
bool readSensorHardware(struct_sensor_payload &payload) {
    
    // =========================================================================
    // MODUS A: ULTRASCHALL-MESSUNG
    // =========================================================================
    #if defined(IS_ULTRASCHALL)
    Serial.println("[Hardware] Starte Ultraschall-Messung...");
    pinMode(US_TRIG_PIN, OUTPUT);
    pinMode(US_ECHO_PIN, INPUT);
    
    digitalWrite(US_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(US_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(US_TRIG_PIN, LOW);
    
    long duration = pulseIn(US_ECHO_PIN, HIGH, 30000); // 30ms Timeout
    
    if (duration > 0) {
        float distanceCm = (duration / 2.0f) * 0.03432f;
        
        // Ultraschall nutzt nur den ersten Prozesswert
        payload.pv1 = distanceCm;
        payload.pv2 = 0.0f;
        payload.pv3 = 0.0f;
        payload.pv4 = 0.0f;
        payload.pv5 = 0.0f;
        
        Serial.printf("[Hardware] Ultraschall-Erfolg! Distanz: %.2f cm\n", payload.pv1);
        return true;
    }
    Serial.println("[Hardware-FEHLER] Ultraschall-Timeout (Echo ausgeblieben)!");
    return false;

    // =========================================================================
    // MODUS B: MODBUS-RADAR-MESSUNG
    // =========================================================================
    #elif defined(IS_RADAR)
    // Pin 4 (SEN_POWER_PIN) einschalten
    digitalWrite(4, HIGH); 
    delay(1500); 

    uint8_t result = 0;
    const int maxModbusRetries = 3; 

    for (int attempt = 1; attempt <= maxModbusRetries; attempt++) {
        // Seriellen Puffer leeren
        while(SensorSerial.available()) SensorSerial.read();

        Serial.printf("[Modbus] Sende Request ab (Register=910)... (Versuch %d/%d)\n", attempt, maxModbusRetries);
        result = node.readHoldingRegisters(910, 10);

        if (result == node.ku8MBSuccess) break;
        Serial.printf("[Modbus-Warnung] Versuch %d fehlgeschlagen: 0x%02X\n", attempt, result);
        delay(200); 
    }

    // Pin 4 (SEN_POWER_PIN) ausschalten
    digitalWrite(4, LOW); 

    if (result == node.ku8MBSuccess) {
        Serial.println("[Modbus-Erfolg] Daten erfolgreich empfangen. Wandle Register...");
        
        RegisterFloat regFloat;
        
        // KORREKTUR: Indizes [0] und [1] sind jetzt absolut fehlerfrei zugewiesen!
        regFloat.registers[0] = node.getResponseBuffer(1); regFloat.registers[1] = node.getResponseBuffer(0);
        payload.pv1 = regFloat.value;
        
        regFloat.registers[0] = node.getResponseBuffer(3); regFloat.registers[1] = node.getResponseBuffer(2);
        payload.pv2 = regFloat.value;
        
        regFloat.registers[0] = node.getResponseBuffer(5); regFloat.registers[1] = node.getResponseBuffer(4);
        payload.pv3 = regFloat.value;
        
        regFloat.registers[0] = node.getResponseBuffer(7); regFloat.registers[1] = node.getResponseBuffer(6);
        payload.pv4 = regFloat.value;
        
        regFloat.registers[0] = node.getResponseBuffer(9); regFloat.registers[1] = node.getResponseBuffer(8);
        payload.pv5 = regFloat.value;
        
        return true;
    }
    Serial.printf("[Modbus-HART-FEHLER] Alle Versuche fehlgeschlagen! Code: 0x%02X\n", result);
    return false;
    
    #else
    #error "Kein Sensor-Typ in der platformio.ini definiert!"
    #endif
}

#endif
