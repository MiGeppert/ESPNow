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
        logger.info("Starte ESPNow-MQTT-Gateway Bridge V5...")
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
            
            if mac in DEVICE_MAPPING:
                device_name, custom_topic = DEVICE_MAPPING[mac]
            else:
                device_name = f"Unbekannt {mac[:4]}"
                custom_topic = f"{BASE_TOPIC}/{mac}/state"

            # --- SENSOR PARSER ---
            if sensor_id == 1:
                # 'f' = Abstand (4B), 'B' = ok (1B), 'B' = jumper (1B), 'B' = ota (1B), 'f' = battery (4B) -> Gesamt 11 Bytes!
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

            if values:
                # 1. Sofortige Weiterleitung an Home Assistant
                for key in values.keys():
                    self.mqtt.publish_discovery(mac, device_name, custom_topic, key)
                
                self.mqtt.publish_state(mac, custom_topic, values)
                logger.info(f"Daten von [{device_name}] erfolgreich verarbeitet: {values}")
                
        elif pkt_type == "log":
            logger.info(f"[ESP32-Gateway-Log] {packet.get('level')}: {packet.get('msg')}")
            
        elif pkt_type == "ack":
            logger.info(f"Bestätigung von [Gateway] erhalten: {packet.get('cmd')} -> {packet.get('status')}")

    def handle_ha_command(self, mac, cmd_type, payload):
        # Bereinige die Payload von eventuellen Anführungszeichen aus HA
        payload = str(payload).strip().replace('"', '').replace("'", "")
        logger.info(f"Empfange HA-Befehl für {mac} [{cmd_type}]: {payload}")
        
        # Formatierung für den C++ Sensor anpassen
        if cmd_type == "sleep":
            payload = f"SLEEP={payload}"
            cmd_type = "cmd"
            
        self.serial.send_command(mac, cmd_type, payload)

if __name__ == "__main__":
    bridge = MainBridge()
    bridge.start()
