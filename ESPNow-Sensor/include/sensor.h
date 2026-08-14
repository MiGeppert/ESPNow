// include/sensor.h
#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include <ModbusMaster.h>
#include "structures.h"

// Externe Objekte und Hardware-Pins aus der main.cpp unfehlbar bekannt machen
extern HardwareSerial SensorSerial;
extern ModbusMaster node;

// Universelle Funktion zum Auslesen der Hardware (v3.2 Unified)
bool readSensorHardware(struct_sensor_payload &payload) {
    
    // =========================================================================
    // MODUS A: ULTRASCHALL-MESSUNG VIA UART-PROTOKOLL (A02YYUW / JSN-SR04T)
    // =========================================================================
    #if defined(IS_ULTRASCHALL)
    Serial.println("[Hardware] Starte Ultraschall-Messung via UART...");
    
    // Sensor-Strom einschalten und Puffer leeren
    digitalWrite(SEN_POWER_PIN, HIGH); 
    delay(500);
    while(SensorSerial.available()) SensorSerial.read();
    
    // Mess-Trigger-Byte abschicken
    SensorSerial.write(0x55);
    
    uint32_t start_time = millis(); 
    uint8_t buf[4] = {0}; 
    int bytesRead = 0;
    
    // Max. 150ms auf die 4 seriellen Antwortbytes warten
    while ((millis() - start_time < 150) && (bytesRead < 4)) {
        if (SensorSerial.available()) {
            buf[bytesRead++] = SensorSerial.read();
        }
    }
    
    // Strom sofort wieder ausschalten
    digitalWrite(SEN_POWER_PIN, LOW);
    
    // Plausibilitaets- und Checksummenpruefung
    if (bytesRead != 4 || buf[0] != 0xFF) {
        Serial.println("[Hardware-FEHLER] Ultraschall-Daten unvollstaendig oder Startbyte fehlt!");
        return false;
    }
    
    if (((buf[0] + buf[1] + buf[2]) & 0xFF) != buf[3]) {
        Serial.println("[Hardware-FEHLER] Ultraschall-Checksummenfehler (Daten korrupt)!");
        return false;
    }
    
    // Distanz in cm umrechnen (Rohwert ist in mm)
    float distanceCm = ((buf[1] << 8) | buf[2]) / 10.0f;
    
    // Unified-Payload befuellen (Ultraschall nutzt nur pv1)
    payload.pv1 = distanceCm;
    payload.pv2 = 0.0f;
    payload.pv3 = 0.0f;
    payload.pv4 = 0.0f;
    payload.pv5 = 0.0f;
    
    Serial.printf("[Hardware-Erfolg] Ultraschall-Distanz: %.1f cm\n", payload.pv1);
    return true;

    // =========================================================================
    // MODUS B: MODBUS-RADAR-MESSUNG
    // =========================================================================
    #elif defined(IS_RADAR)
    Serial.println("[Hardware] Starte Modbus-Radar-Messung...");
    
    // Sensor-Strom einschalten
    digitalWrite(SEN_POWER_PIN, HIGH); 
    delay(1500); 

    SensorSerial.end();
    SensorSerial.begin(115200, SERIAL_8N1, 16, 17);
    while(SensorSerial.available()) SensorSerial.read();


    uint8_t result = 0;
    const int maxModbusRetries = 3; 

    for (int attempt = 1; attempt <= maxModbusRetries; attempt++) {
        while(SensorSerial.available()) SensorSerial.read();

        Serial.printf("[Modbus] Sende Request ab (Register=910)... (Versuch %d/%d)\n", attempt, maxModbusRetries);
        result = node.readHoldingRegisters(910, 10);

        if (result == node.ku8MBSuccess) break;
        Serial.printf("[Modbus-Warnung] Versuch %d fehlgeschlagen: 0x%02X\n", attempt, result);
        delay(200); 
    }

    SensorSerial.flush();

    // Sensor-Strom wieder ausschalten
    digitalWrite(SEN_POWER_PIN, LOW); 

    if (result == node.ku8MBSuccess) {
        Serial.println("[Modbus-Erfolg] Daten erfolgreich empfangen. Wandle Register...");
        
        RegisterFloat regFloat;
        
        // Byte-Swapping für alle 5 Prozesswerte
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
    #error "Kein gueltiger Sensor-Typ in der platformio.ini selektiert!"
    #endif
}

#endif
