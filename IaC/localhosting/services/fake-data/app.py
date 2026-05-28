import os
import random
import string
import threading
import time
from datetime import datetime, timezone
from typing import List, Dict

import paho.mqtt.client as mqtt
from fastapi import FastAPI
import uvicorn

app = FastAPI(title="IoT fake data", version="0.2.0")


@app.on_event("startup")
def start_faker():
    threading.Thread(target=run_faker, daemon=True).start()
    print("[faker] background thread started")


@app.get("/")
async def root():
    return {"message": "IoT fake data is running"}


@app.get("/health")
async def health():
    return {"status": "ok"}


@app.get("/ready")
async def ready():
    return {"status": "ready"}


def now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def rand_id(prefix: str = "device", n: int = 4) -> str:
    return f"{prefix}-" + "".join(random.choices(string.ascii_lowercase + string.digits, k=n))


def clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


class FakeDevice:
    def __init__(
        self,
        device_id: str,
        broker_host: str,
        broker_port: int,
        env: str,
        tenant: str,
        client: mqtt.Client,
        humidity_base: float = 45.0,
        temperature_base: float = 22.0,
        periods: dict | None = None,
    ) -> None:
        self.id = device_id
        self.client = client
        self.env = env
        self.tenant = tenant
        self.broker_host = broker_host
        self.broker_port = broker_port

        self.seq = 0
        self.humidity = humidity_base + random.uniform(-3, 3)
        self.temperature = temperature_base + random.uniform(-2, 2)
        self.periods = dict(periods or {"hum": 1.0, "tmp": 1.0, "stt": 5.0, "geo": 10.0})
        self.rssi = -60 + random.randint(-15, 5)
        self.snr = 7.5 + random.uniform(-3, 3)
        self.battery = random.uniform(40.0, 100.0)
        self.lat = 54.842621 + random.uniform(-0.5, 0.5)
        self.lon = 83.087844 + random.uniform(-0.5, 0.5)
        self.online = "online"

        self.topic_cmd = f"{env}/{tenant}/devices/{self.id}/command"
        self.topic_ack = f"{env}/{tenant}/devices/{self.id}/ack"

    def publish_humidity(self, retain: bool = False) -> None:
        self.seq += 1
        self.humidity = clamp(self.humidity + random.uniform(-0.3, 0.3), 0.0, 100.0)
        payload = f"{self.id},{now_iso()},{self.humidity:.2f},{self.seq}"
        topic = f"{self.env}/{self.tenant}/sensors/{self.id}/humidity"
        info = self.client.publish(topic, payload=payload, qos=1, retain=retain)
        print(info)

    def publish_temperature(self, retain: bool = False) -> None:
        self.seq += 1
        self.temperature = clamp(self.temperature + random.uniform(-0.2, 0.2), -40.0, 60.0)
        payload = f"{self.id},{now_iso()},{self.temperature:.2f},{self.seq}"
        topic = f"{self.env}/{self.tenant}/sensors/{self.id}/temperature"
        info = self.client.publish(topic, payload=payload, qos=1, retain=retain)
        print(info)

    def publish_state(self, retain: bool = True) -> None:
        self.rssi += random.randint(-1, 1)
        self.rssi = clamp(self.rssi, -100, -40)
        self.snr += random.uniform(-0.2, 0.2)
        self.battery = clamp(self.battery - random.uniform(0.0, 0.05), 5.0, 100.0)
        payload = f"{self.id},{now_iso()},{self.rssi},{self.snr:.2f},{self.battery:.1f},{self.online}"
        topic = f"{self.env}/{self.tenant}/sensors/{self.id}/state"
        self.client.publish(topic, payload=payload, qos=1, retain=retain)

    def publish_location(self, retain: bool = True) -> None:
        self.lat += random.uniform(-0.0008, 0.0008)
        self.lon += random.uniform(-0.0008, 0.0008)
        payload = f"{self.id},{now_iso()},{self.lat:.6f},{self.lon:.6f}"
        topic = f"{self.env}/{self.tenant}/sensors/{self.id}/location"
        self.client.publish(topic, payload=payload, qos=1, retain=retain)

    def handle_command(self, payload: str) -> None:
        try:
            parts = payload.split(",", 3)
            command_id = parts[0]

            def ack():
                status = "ok"
                details = ""

                if len(parts) >= 3:
                    cmd_type = parts[2].strip().upper()
                else:
                    cmd_type = "UNKNOWN"

                cmd_params = parts[3].split(",") if len(parts) >= 4 else []

                if cmd_type == "SLEEP":
                    self.online = "sleep"
                elif cmd_type == "WAKE":
                    self.online = "online"
                elif cmd_type == "OFFLINE":
                    self.online = "offline"
                elif cmd_type == "FETCH":
                    targets = [p.strip().lower() for p in cmd_params if p.strip()]
                    if not targets:
                        targets = ["hum", "tmp", "stt", "geo"]
                    for target in targets:
                        if target == "hum":
                            self.publish_humidity(retain=False)
                        elif target == "tmp":
                            self.publish_temperature(retain=False)
                        elif target == "stt":
                            self.publish_state(retain=True)
                        elif target == "geo":
                            self.publish_location(retain=True)
                elif cmd_type == "INTERVAL":
                    try:
                        metric = cmd_params[0].strip().lower() if cmd_params else ""
                        seconds = float(cmd_params[1]) if len(cmd_params) > 1 else 0
                        if metric in self.periods and seconds > 0:
                            self.periods[metric] = seconds
                        else:
                            status = "error"
                            details = f"bad interval params: {cmd_params}"
                    except (ValueError, IndexError) as exc:
                        status = "error"
                        details = f"bad interval params: {exc}"

                ack_payload = f"{command_id},{now_iso()},{status},{details}"
                self.client.publish(self.topic_ack, payload=ack_payload, qos=1, retain=False)
                print(f"[faker:{self.id}] command {command_id} processed: {cmd_type} params={cmd_params}")

            threading.Timer(0.5 + random.random(), ack).start()
        except Exception as e:
            command_id = "unknown"
            ack_payload = f"{command_id},{now_iso()},error,{str(e)}"
            self.client.publish(self.topic_ack, payload=ack_payload, qos=1, retain=False)


def run_faker() -> None:
    broker_host = os.getenv("MQTT_HOST", "5.129.250.254")
    broker_port = int(os.getenv("MQTT_PORT", "1883"))
    username = os.getenv("MQTT_USERNAME", "dynsec-admin")
    password = os.getenv("MQTT_PASSWORD", "change_me_admin")
    env = os.getenv("ENV", "dev")
    tenant = os.getenv("TENANT", "fake")

    devices_count = int(os.getenv("DEVICES", "5"))
    state_period = float(os.getenv("STATE_PERIOD", "5.0"))
    loc_period = float(os.getenv("LOCATION_PERIOD", "10.0"))
    humidity_period = float(os.getenv("HUMIDITY_PERIOD", "1.0"))
    temperature_period = float(os.getenv("TEMPERATURE_PERIOD", "1.0"))
    mqtt_connections = int(os.getenv("MQTT_CONNECTIONS", "1"))

    if mqtt_connections < 1:
        mqtt_connections = 1
    if mqtt_connections > devices_count:
        mqtt_connections = devices_count

    print(
        f"[faker] starting with host={broker_host}:{broker_port} env={env} tenant={tenant} "
        f"devices={devices_count} connections={mqtt_connections} "
        f"humidity_period={humidity_period}s temperature_period={temperature_period}s "
        f"state_period={state_period}s loc_period={loc_period}s"
    )
    clients: List[mqtt.Client] = []
    for i in range(mqtt_connections):
        client = mqtt.Client(
            client_id=f"faker-{i:03d}-{random.randint(1000, 9999)}",
            clean_session=True,
        )
        if username:
            client.username_pw_set(username, password)

        clients.append(client)

    devices_per_client: Dict[mqtt.Client, List[FakeDevice]] = {c: [] for c in clients}

    for i in range(devices_count):
        client = clients[i % mqtt_connections]
        dev = FakeDevice(
            device_id=f"dev-{i:04d}",
            broker_host=broker_host,
            broker_port=broker_port,
            env=env,
            tenant=tenant,
            client=client,
            humidity_base=random.uniform(40, 60),
            temperature_base=random.uniform(18, 26),
            periods={
                "hum": humidity_period,
                "tmp": temperature_period,
                "stt": state_period,
                "geo": loc_period,
            },
        )
        devices_per_client[client].append(dev)

    def make_on_connect(local_devices: List[FakeDevice]):
        def on_connect(c: mqtt.Client, userdata, flags, rc):
            print(f"[faker] client {c._client_id.decode()} connected rc={rc}, devices={len(local_devices)}")
            for d in local_devices:
                c.subscribe(d.topic_cmd, qos=1)
                d.publish_state(retain=True)
                d.publish_location(retain=True)

        return on_connect

    def make_on_message(local_devices: List[FakeDevice]):
        def on_message(c: mqtt.Client, userdata, msg: mqtt.MQTTMessage):
            for d in local_devices:
                if msg.topic == d.topic_cmd:
                    d.handle_command(msg.payload.decode("utf-8", errors="ignore"))
                    break

        return on_message

    for client, devs in devices_per_client.items():
        client.on_connect = make_on_connect(devs)
        client.on_message = make_on_message(devs)
        client.connect(broker_host, broker_port, keepalive=30)
        threading.Thread(
            target=lambda c=client: c.loop_forever(retry_first_connection=True),
            daemon=True,
        ).start()

    t0 = time.time()
    all_devices: List[FakeDevice] = [d for devs in devices_per_client.values() for d in devs]

    next_state = [t0 + random.uniform(0, state_period) for _ in all_devices]
    next_loc = [t0 + random.uniform(0, loc_period) for _ in all_devices]
    next_h = [t0 + random.uniform(0, humidity_period) for _ in all_devices]
    next_t = [t0 + random.uniform(0, temperature_period) for _ in all_devices]

    try:
        while True:
            now = time.time()
            for i, d in enumerate(all_devices):
                if now >= next_h[i] and d.online == "online":
                    d.publish_humidity(retain=False)
                    next_h[i] = now + d.periods["hum"]

                if now >= next_t[i] and d.online == "online":
                    d.publish_temperature(retain=False)
                    next_t[i] = now + d.periods["tmp"]

                if now >= next_state[i]:
                    d.publish_state(retain=True)
                    next_state[i] = now + d.periods["stt"]

                if now >= next_loc[i]:
                    d.publish_location(retain=True)
                    next_loc[i] = now + d.periods["geo"]

            time.sleep(0.01)
    except KeyboardInterrupt:
        print("[faker] stopping...")


if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000, reload=False)
