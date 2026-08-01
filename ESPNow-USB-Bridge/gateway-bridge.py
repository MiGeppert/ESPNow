# /opt/espnow-gateway/gateway-bridge.py
import struct
import json
from logger import setup_logger

logger = setup_logger()

from mqtt_client import MQTTBridgeClient
from serial_client import SerialGatewayClient
from config import DEVICE_MAPPING, BASE_TOPIC

class MainBridge:
    def __init__(self):
        self.mqtt = MQTTBridgeClient(on_cmd_callback=self.handle_ha_command)
        self.serial = SerialGatewayClient(on_data_callback=self.handle_gateway_data)

    def start(self):
        logger.info("Starte ESPNow-MQTT-Gateway Bridge V3.0...")
        self.mqtt.connect()
        self.serial.connect()
        self.serial.read_loop()

    def handle_gateway_data(self, packet):
        pkt_type = packet.get("type")
        mac = packet.get("mac", "").replace(":", "").lower()

        if pkt_type == "data":
            sensor_id = packet.get("sensor_id")
            fw_version = packet.get("fw")
            raw_bytes = bytes(packet.get("raw", []))
            
            values = {}
            
            # Gerätenamen und Topic aus dem Mapping auflösen
            if mac in DEVICE_MAPPING:
                device_name, custom_topic = DEVICE_MAPPING[mac]
            else:
                device_name = f"Unbekannt {mac[:4]}"
                custom_topic = f"{BASE_TOPIC}/{mac}/state"

            # =========================================================================
            # SENSOR ID 1: ULTRASCHALL SENSOR (ZISTERNE ALT)
            # =========================================================================
            if sensor_id == 1:
                if len(raw_bytes) >= 11:
                    distance, ok, jumper, ota_mode, battery = struct.unpack("<fBBBf", raw_bytes[:11])
                    values = {
                        "distance": round(distance, 2), 
                        "ok": ok, 
                        "jumper": jumper, 
                        "ota_mode": ota_mode, 
                        "battery": round(battery, 2),
                        "fw_version": fw_version
                    }

            # =========================================================================
            # SENSOR ID 2: RADAR MODBUS SENSOR (NEU IN V3.0)
            # =========================================================================
            elif sensor_id == 2:
                # 5x Float (20B) + ok (1B) + jumper (1B) + ota (1B) + 1x Float Batterie (4B) = 27 Bytes Gesamt!
                if len(raw_bytes) >= 27:
                    # <fffffBBBf steht für: 5 Floats, 3 unsigned Chars, 1 Float (Little Endian)
                    pv1, pv2, pv3, pv4, pv5, ok, jumper, ota_mode, battery = struct.unpack("<fffffBBBf", raw_bytes[:27])
                    values = {
                        "pv1": round(pv1, 4),
                        "pv2": round(pv2, 4),
                        "pv3": round(pv3, 4),
                        "pv4": round(pv4, 4),
                        "pv5": round(pv5, 4),
                        "ok": ok,
                        "jumper": jumper,
                        "ota_mode": ota_mode,
                        "battery": round(battery, 2),
                        "fw_version": fw_version
                    }

            # =========================================================================
            # WEITERLEITUNG AN HOME ASSISTANT VIA MQTT
            # =========================================================================
            if values:
                # Dynamisches Home Assistant Discovery für alle empfangenen Schlüssel triggern
                for key in values.keys():
                    self.mqtt.publish_discovery(mac, device_name, custom_topic, key)
                
                # Die echten Werte als JSON-Payload auf das State-Topic pushen
                self.mqtt.publish_state(mac, custom_topic, values)
                logger.info(f"Daten von [{device_name}] erfolgreich verarbeitet: {values}")
                
        elif pkt_type == "log":
            logger.info(f"[ESP32-Gateway-Log] {packet.get('level')}: {packet.get('msg')}")
            
        elif pkt_type == "ack":
            logger.info(f"Bestätigung von [Gateway] erhalten: {packet.get('cmd')} -> {packet.get('status')}")

    def handle_ha_command(self, mac, cmd_type, payload):
        payload = str(payload).strip().replace('"', '').replace("'", "")
        logger.info(f"Empfange HA-Befehl für {mac} [{cmd_type}]: {payload}")
        
        if cmd_type == "sleep":
            payload = f"SLEEP={payload}"
            cmd_type = "cmd"
            
        self.serial.send_command(mac, cmd_type, payload)

if __name__ == "__main__":
    bridge = MainBridge()
    bridge.start()
