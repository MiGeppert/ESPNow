// include/modbus_radar.h
#ifndef MODBUS_RADAR_H
#define MODBUS_RADAR_H

#include <Arduino.h>
#include <ModbusMaster.h>
#include "structures.h"

// Macht dem Compiler die Variablen aus der main.cpp bekannt
extern HardwareSerial SensorSerial;
extern ModbusMaster node;

bool readModbusSensor(float *pvs) {
    digitalWrite(4, HIGH); // Pin 4 = SEN_POWER_PIN
    delay(1500); 

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

    digitalWrite(4, LOW); 

    if (result == node.ku8MBSuccess) {
        Serial.println("[Modbus-Erfolg] Daten erfolgreich empfangen.");

        for (int i = 0; i < 5; i++) {
            uint16_t reg1 = node.getResponseBuffer(i * 2);
            uint16_t reg2 = node.getResponseBuffer((i * 2) + 1);

            RegisterFloat regFloat;
            regFloat.registers[0] = reg2;
            regFloat.registers[1] = reg1;
            
            pvs[i] = regFloat.value;

/*        for (int i = 0; i < 5; i++) {
            RegisterFloat regFloat;
            regFloat.registers[0] = node.getResponseBuffer(i * 2);
            regFloat.registers[1] = node.getResponseBuffer((i * 2) + 1);
            pvs[i] = regFloat.value;
*/
        }
        return true;
    } else {
        Serial.printf("[Modbus-HART-FEHLER] Alle Versuche fehlgeschlagen! Code: 0x%02X\n", result);
        return false;
    }
}

#endif
