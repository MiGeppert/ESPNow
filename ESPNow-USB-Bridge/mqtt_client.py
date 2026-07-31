# /opt/espnow-gateway/mqtt_client.py
import json
import paho.mqtt.client as mqtt
from config import MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASSWORD, DISCOVERY_PREFIX, BASE_TOPIC, DEVICE_MAPPING
import logging

logger = logging.getLogger("espnow_bridge")

class MQTTBridgeClient:
    def __init__(self, on_cmd_callback):
        self.client = mqtt.Client()
        self.client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
        self.client.on_connect = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message = self.on_message
        self.on_cmd_callback = on_cmd_callback
        self.known_entities = set()
        self.is_connected = False

    def connect(self):
        try:
            logger.info(f"Verbinde mit MQTT-Broker {MQTT_BROKER}...")
            self.client.connect(MQTT_BROKER, MQTT_PORT, 60)
            self.client.loop_start()
        except Exception as e:
            logger.error(f"MQTT Verbindungsversuch fehlgeschlagen: {e}")

    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            logger.info("Erfolgreich mit MQTT-Broker verbunden.")
            self.is_connected = True
            # Höre auf generische Befehle und gerätespezifische Topics
            self.client.subscribe(f"{BASE_TOPIC}/+/cmd/#")
            for mac, info in DEVICE_MAPPING.items():
                self.client.subscribe(f"{info[1]}/cmd/#")
        else:
            logger.error(f"MQTT Verbindungsfehler! Code (rc): {rc}")

    def on_disconnect(self, client, userdata, rc):
        logger.warning(f"MQTT-Verbindung verloren (rc: {rc})")
        self.is_connected = False

    def on_message(self, client, userdata, msg):
        try:
            parts = msg.topic.split("/")
            payload = msg.payload.decode("utf-8")

            # Prüfen, ob der Befehl über das schöne Topic reinkam
            mac_target = None
            cmd_type = "generic"

            for mac, info in DEVICE_MAPPING.items():
                if msg.topic.startswith(info[1]):
                    mac_target = mac
                    if len(parts) > 2: cmd_type = parts[2]
                    break

            if not mac_target and parts[0] == BASE_TOPIC:
                mac_target = parts[1]
                if len(parts) > 3: cmd_type = parts[3]

            if mac_target:
                self.on_cmd_callback(mac_target, cmd_type, payload)
        except Exception as e:
            logger.error(f"Fehler im Rückkanal: {e}")

    def publish_discovery(self, mac, device_name, custom_topic, value_key):
        entity_id = f"{mac}_{value_key}"
        if entity_id in self.known_entities:
            return

        if not self.is_connected:
            return

        # ALLER-WICHTIGSTER FIX: Alle Variablen sauber für Python vorinitialisieren
        ha_type = "sensor"
        device_class = None
        unit = None
        state_class = None  
        payload_on = None
        payload_off = None

        friendly_key_name = value_key.capitalize()

        if value_key == "distance":
            ha_type = "sensor"
            device_class, unit, friendly_key_name = "distance", "cm", "Füllstand"
            state_class = "measurement"

        elif value_key == "battery":
            ha_type = "sensor"
            device_class = "voltage"
            friendly_key_name = "Spannung"

        elif value_key == "ok":
            ha_type = "binary_sensor"
            device_class, unit, friendly_key_name = "connectivity", None, "Status"
            payload_on, payload_off = "1", "0"

        elif value_key == "jumper":
            ha_type = "binary_sensor"
            device_class, unit, friendly_key_name = None, None, "Service Jumper"
            payload_on, payload_off = "1", "0"

        elif value_key == "ota_mode":
            ha_type = "binary_sensor"
            device_class, unit, friendly_key_name = None, None, "OTA"
            payload_on, payload_off = "1", "0"

        elif value_key == "fw_version":
            ha_type = "sensor"
            device_class, unit, friendly_key_name = None, None, "Firmware Version"

        discovery_topic = f"{DISCOVERY_PREFIX}/{ha_type}/{mac}/{value_key}/config"
        state_topic = custom_topic if custom_topic else f"{BASE_TOPIC}/{mac}/state"

        payload = {
            "name": f"{device_name} {friendly_key_name}",
            "unique_id": f"espnow_{entity_id}",
            "state_topic": state_topic,
            "value_template": f"{{{{ value_json.{value_key} \n}}}}",
            "state_on": "1",
            "state_off": "0",
            "device": {
                "identifiers": [f"espnow_{mac}"],
                "name": device_name,
                "model": "ESPNow Node",
                "manufacturer": "MiGe"
            }
        }

        if device_class: payload["device_class"] = device_class
        if unit: payload["unit_of_measurement"] = unit
        if state_class: payload["state_class"] = state_class 
        if payload_on: payload["payload_on"] = payload_on
        if payload_off: payload["payload_off"] = payload_off

        self.client.publish(discovery_topic, json.dumps(payload), retain=True)
        self.known_entities.add(entity_id)
        logger.info(f"HA Discovery ({ha_type}) gesendet: {device_name} -> {friendly_key_name}")

    def publish_state(self, mac, custom_topic, data):
        if self.is_connected:
            state_topic = custom_topic if custom_topic else f"{BASE_TOPIC}/{mac}/state"
            self.client.publish(state_topic, json.dumps(data))
