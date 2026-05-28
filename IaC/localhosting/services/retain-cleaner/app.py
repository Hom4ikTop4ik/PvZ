import os
import sys
import time
import paho.mqtt.client as mqtt

HOST = os.environ.get("MQTT_HOST", "mosquitto")
PORT = int(os.environ.get("MQTT_PORT", "1883"))
CLIENT_ID = os.environ.get("MQTT_CLIENT_ID", "retain-cleaner")
USERNAME = os.environ.get("MQTT_USERNAME")
PASSWORD = os.environ.get("MQTT_PASSWORD")

SUB_FILTERS = [s.strip() for s in os.environ.get("SUB_FILTERS", "").splitlines() if s.strip()]
if not SUB_FILTERS:
    SUB_FILTERS = [
        "+/+/sensors/+/humidity",
        "+/+/sensors/+/temperature",
        "+/+/devices/+/command",
    ]

print(f"[retain-cleaner] connecting to {HOST}:{PORT}, filters={SUB_FILTERS}")

client = mqtt.Client(client_id=CLIENT_ID, clean_session=True, protocol=mqtt.MQTTv311)
if USERNAME:
    client.username_pw_set(USERNAME, PASSWORD or "")

def on_connect(c: mqtt.Client, userdata, flags, rc):
    print(f"[retain-cleaner] connected rc={rc}")
    for f in SUB_FILTERS:
        c.subscribe(f, qos=1)
    print("[retain-cleaner] subscribed")

def on_message(c: mqtt.Client, userdata, msg: mqtt.MQTTMessage):
    if msg.retain:
        print(f"[retain-cleaner] clearing retained on {msg.topic}")
        c.publish(msg.topic, payload=b"", qos=1, retain=True)

client.on_connect = on_connect
client.on_message = on_message

while True:
    try:
        client.connect(HOST, PORT, keepalive=30)
        client.loop_forever(retry_first_connection=True)
    except Exception as e:
        print(f"[retain-cleaner] error: {e}", file=sys.stderr)
        time.sleep(3)
