import machine
import network
import ntptime
import random
from umqtt.simple import MQTTClient

import sys
import time

from lora_mini_lib import LoRaMiniLib
import config_gateway as config_ga
import gateway_config_portal
import gateway_settings
import node_registry

from utils import RingBuffer, LoRaTxQueue, lora_tx_pump




# Инициализация периферии
led = machine.Pin(config_ga.PIN_LED, machine.Pin.OUT, value=1)  # выключен
button = machine.Pin(config_ga.PIN_BUTTON, machine.Pin.IN, machine.Pin.PULL_UP)

settings = gateway_settings.load_settings(config_ga.TENANT, config_ga.WIFI_SSID, config_ga.WIFI_PASS)
active_settings = gateway_settings.apply_settings(config_ga, settings)
print("[CONFIG] Tenant:", active_settings["tenant"])
print("[CONFIG] Wi-Fi SSID:", active_settings["wifi_ssid"], active_settings["wifi_pass"])

def config_portal_requested(
    window_ms=config_ga.CONFIG_PORTAL_BOOT_WINDOW_MS,
    hold_ms=config_ga.CONFIG_PORTAL_BOOT_HOLD_MS,
):
    print("[CONFIG] Hold BOOT now to open config portal...")
    led.value(0) # вкл светодиод — показать, что шлюз ждёт зажатие кнопку
    start = time.ticks_ms()
    held_since = None

    while time.ticks_diff(time.ticks_ms(), start) < window_ms:
        if button.value() == 0:
            if held_since is None:
                held_since = time.ticks_ms()
            led.value(0)
            time.sleep_ms(70)
            led.value(1)
            time.sleep_ms(70)
            if time.ticks_diff(time.ticks_ms(), held_since) >= hold_ms:
                return True
        else:
            held_since = None
            time.sleep_ms(50)
    
    led.value(1) # выкл светодиод

    return False


if not gateway_settings.wifi_is_configured(active_settings):
    print("[CONFIG] Wi-Fi is not configured; opening config portal")
    gateway_config_portal.run_config_portal(active_settings)

if config_portal_requested():
    gateway_config_portal.run_config_portal(active_settings)

# Глобальный клиент
mqtt_client = None
# Глобальная lora
lora = None

seq_num = 0
history = RingBuffer(max_size=config_ga.HISTORY_SIZE)

# TX queue: ограничиваем RAM
tx_queue = LoRaTxQueue(
    max_items=config_ga.LORA_TX_QUEUE_SIZE,
    max_msg_len=config_ga.LORA_TX_MAX_LEN,
)

# --- 1. Работа с временем (NTP) ---
def sync_time():
    """Синхронизация времени с интернетом, чтобы timestamp был верным"""
    try:
        print("[TIME] Syncing NTP...")
        ntptime.settime() # Стандартный сервер pool.ntp.org
        print(f"[TIME] Current time: {time.localtime()}")
    except Exception as e:
        print(f"[TIME] Sync failed: {e}")


def get_iso_timestamp():
    """
    Возвращает время в формате ISO 8601: YYYY-MM-DDTHH:MM:SSZ
    """
    t = time.gmtime()
    return "{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}Z".format(
        t[0], t[1], t[2], t[3], t[4], t[5]
    )

# --- 2. Работа с Wi-Fi ---
def connect_wifi(max_wait_s=30):
    wlan = network.WLAN(network.STA_IF)
    wlan.active(False)
    time.sleep(1)
    wlan.active(True)
    time.sleep(1)
    
    if config_ga.WIFI_SSID == gateway_settings.NO_WIFI_SSID:
        print("[WIFI] SSID is not configured")
        return False

    if not wlan.isconnected():
        print(f"[WIFI] Connecting to '{config_ga.WIFI_SSID}'...")
        wlan.connect(config_ga.WIFI_SSID, config_ga.WIFI_PASS)

        status_codes = {
            4: "STAT_DISCONNECTED",
            5: "STAT_CONNECT_FAIL",
            201: "STAT_NO_AP_FOUND",
            202: "STAT_WRONG_PASSWORD",
            203: "STAT_ASSOC_FAIL",
            204: "STAT_HANDSHAKE_TIMEOUT",
            1001: "STAT_IDLE",
            1002: "STAT_CONNECTING",
            1010: "STAT_GOT_IP",
        }
        
        # connection - version 1
        # max_wait = 20
        # while max_wait > 0:
        #     status = wlan.status()
        #     print(f"    {status}")
        #         break
        #     max_wait -= 1
        #     time.sleep(1)

        # connection - version 2
        # last_time = time.time()
        # while not wlan.isconnected():
        #     if (time.time() - last_time) >= 1.0:
        #         status = wlan.status()
        #         print(f"WLAN.status: {status} - {status_codes[status]}")
        #         last_time = time.time()
        #     machine.idle()

        # connection - version 3
        for _ in range(max_wait_s):
            status = wlan.status()
            print("[WIFI]", status_codes.get(status, status))
            if wlan.isconnected():
                break
            time.sleep(1)

            
    if wlan.isconnected():
        print(f"[WIFI] Connected! IP: {wlan.ifconfig()[0]}")
        return True
    else:
        print("[WIFI] Connection failed")
        return False



def _enqueue_payload(payload: str) -> bool:
    """
    Добавляет сообщение в TX очередь.
    Возвращает False если очередь заполнена/сообщение слишком длинное.
    """
    return tx_queue.add(payload.encode("utf-8"))

def send_command(cmd, *params, device_id=None, timestamp=None) -> bool:
    if not device_id:
        # кому отправлять-то?
        return False
    
    if not timestamp:
        timestamp = get_iso_timestamp()
    msg_rnd_id = random.randint(0, 999_999)
    payload = f"{device_id};{timestamp};{msg_rnd_id};cmd;{cmd}"
    if params:
        payload += f",{','.join(map(str, params))}"

    return _enqueue_payload(payload)


def send_join_ack(mac, node_id):
    timestamp = get_iso_timestamp()
    msg_rnd_id = random.randint(0, 999_999)
    payload = f"0;{timestamp};{msg_rnd_id};join_ack;{mac},{node_id}"
    if _enqueue_payload(payload):
        print(f"[JOIN] ACK queued: mac={mac} node_id={node_id}")
        return True

    print("[JOIN] ACK queue full")
    return False


def handle_join_request(device_id, timestamp, msg_rnd_id, msg):
    if device_id != "0":
        print(f"[JOIN] Ignoring join from non-zero device_id={device_id}")
        return True

    mac = msg.strip()
    node_id = node_registry.assign_node_id(mac)
    if not node_id:
        print("[JOIN] Invalid MAC in join request")
        return True

    print(f"[JOIN] Request mac={mac} rnd={msg_rnd_id} -> node_id={node_id}")
    send_join_ack(mac, node_id)
    return True


# --- 3. Обработка входящих команд (MQTT -> Gateway -> LoRa) ---
def on_message_received(topic, msg):
    """
    Вызывается, когда Брокер присылает команду для устройства.
    """
    try:
        topic_str = topic.decode()
        msg_str = msg.decode()
        
        print(f"\n[MQTT RX] Topic: {topic_str}")
        print(f"[MQTT RX] Payload: {msg_str}")
        
        # 1. Разбираем топик: {env}/{tenant}/devices/{device_id}/command
        parts = topic_str.split('/')
        # Проверяем структуру
        if len(parts) == 5 and parts[-1] == 'command':
            target_device_id = parts[-2] # from topic
            
            # 2. Разбираем Payload CSV: device_id,timestamp,type,params
            cmd_parts = msg_str.split(',')
            if len(cmd_parts) >= 3:
                device_id = cmd_parts[0] # from msg
                timestamp = cmd_parts[1]
                cmd_type = cmd_parts[2]
                params = cmd_parts[3:]
                
                print(f"-"*30)
                print(f"!!! COMMAND RECEIVED !!!")
                print(f"Target Device (from topic): {target_device_id}")
                print(f"Target Device (from msg): {device_id}")
                print(f"Command Type:  {cmd_type}")
                print(f"Action: Sending via LoRa UART...") 

                send_command(cmd_type, *params, device_id=target_device_id, timestamp=timestamp)
                print(f"-"*30)
            else:
                print("[WARN] Invalid CSV format in command")
                
    except Exception as e:
        print(f"[ERR] Message processing error: {e}")

# --- 4. Подключение к MQTT ---
def connect_mqtt():
    global mqtt_client
    print(f"[MQTT] Connecting to {config_ga.MQTT_SERVER}...")
    
    try:
        client = MQTTClient(
            config_ga.MQTT_CLIENT_ID, 
            config_ga.MQTT_SERVER, 
            port=config_ga.MQTT_PORT, 
            user=config_ga.MQTT_USER, 
            password=config_ga.MQTT_PASS,
            keepalive=60
        )
        client.set_callback(on_message_received)
        client.connect()
        print(f"[MQTT] Connected.")
        
        # Подписываемся на команды для ВСЕХ устройств
        client.subscribe(config_ga.TOPIC_SUB_COMMANDS)
        print(f"[MQTT] Subscribed to {config_ga.TOPIC_SUB_COMMANDS}")
        
        return client
    except Exception as e:
        print(f"[MQTT] Connection failed: {e}")
        return None

def ensure_mqtt():
    """Проверяет MQTT и переподключает если надо. Возвращает True если ОК"""
    global mqtt_client
    
    if mqtt_client:
        try:
            # Быстрая проверка
            mqtt_client.ping()
            return True
        except:
            print("[MQTT] Connection lost")
            mqtt_client = None
    
    # Переподключаемся
    # (цикл вечный, потому что без него нет смысла слушать LoRa и запоминать сообщения — RAM закончится)
    print("[MQTT] Reconnecting...")
    led.value(0)
    while True:
        mqtt_client = connect_mqtt()
        if mqtt_client:
            break
        time.sleep(2)
    led.value(1)
    
    return mqtt_client is not None



def do_payload(payload):
    global mqtt_client, history, seq_num, lora, led, button
    parts = payload.split(";", 4)
    if len(parts) != 5:
        print("[LoRa] Invalid payload:", payload)
        return

    device_id, timestamp, msg_rnd_id, msg_type, msg = parts

    if msg_type == "join":
        handle_join_request(device_id, timestamp, msg_rnd_id, msg)
        return

    if msg_type == "join_ack":
        print("[JOIN] Ignoring join_ack on gateway")
        return

    if device_id == "0":
        print("[LoRa] Ignoring unassigned device payload:", payload)
        return
    
    if (device_id, timestamp, msg_rnd_id) in history:
        print("History has this")
        return
    history.add((device_id, timestamp, msg_rnd_id))
    
    # Проверяем MQTT
    if not ensure_mqtt():
        print("[WARN] No MQTT: skip parsing LoRa msg  and  skip gateway->MQTT sending")
        return
    
    try:
        if msg_type == "hum":
            # --- 1. Влажность (Humidity) ---
            # Топик: dev/test/sensors/node-01/humidity
            # Топик: {env}}/{tenant}/sensors/{device_id}}/humidity
            topic_hum = f"{config_ga.TOPIC_PUB_PREFIX}/{device_id}/humidity"
            # Формат CSV: device_id,timestamp,humidity,sequence
            # 50.25%
            try:
                humid = float(msg)
            except Exception:
                print("Can't msg to float humid")
                return
            
            payload_hum = f"{device_id},{timestamp},{msg},{seq_num}"
            
            mqtt_client.publish(topic_hum, payload_hum)
            seq_num += 1
            print(f"[TX] {topic_hum} -> {payload_hum}")
        elif msg_type == "tmp":
            # --- 2. Температура (Temperature) ---
            topic_temp = f"{config_ga.TOPIC_PUB_PREFIX}/{device_id}/temperature"
            # Формат CSV: device_id,timestamp,temperature,sequence
            # 20 *С
            try:
                temp = float(msg)
            except Exception:
                print("Can't msg to float temp")
                return

            payload_temp = f"{device_id},{timestamp},{float(msg)},{seq_num}"
            
            mqtt_client.publish(topic_temp, payload_temp)
            seq_num += 1
            print(f"[TX] {topic_temp} -> {payload_temp}")
        elif msg_type == "gps":
            # --- 3. Геопозиция (Location) ---
            # Топик: dev/test/sensors/node-01/location
            topic_loc = f"{config_ga.TOPIC_PUB_PREFIX}/{device_id}/location"
            # Формат CSV: device_id,timestamp,lat,lon
            try:
                lat, lon = map(float, msg.split(","))
            except Exception:
                print("Can't msg to 2*float location")
            # msg = {lat},{lon}  # что прекрасно вставляется в сообщение
            payload_loc = f"{device_id},{timestamp},{msg},{seq_num}"
            
            mqtt_client.publish(topic_loc, payload_loc)
            seq_num += 1
            print(f"[TX] {topic_loc} -> {payload_loc}")
        elif msg_type == "stt":
            # --- 4. Состояние (State) ---
            # Топик: dev/test/sensors/node-01/state
            topic_state = f"{config_ga.TOPIC_PUB_PREFIX}/{device_id}/state"
            # Формат CSV: device_id,timestamp,rssi,snr,battery,online_status
            # rssi=-65, snr=8.5, bat=95.5, status=online
            
            # msg = {rssi},{snr},{bat},{online_status}  # что прекрасно вставляется в сообщение
            payload_state = f"{device_id},{timestamp},{msg},{seq_num}"
            
            mqtt_client.publish(topic_state, payload_state)
            seq_num += 1
            print(f"[TX] {topic_state} -> {payload_state}")

    except Exception as e:
        print(f"[MQTT SEND ERROR] {e}")
        mqtt_client = None  # Сбрасываем, чтобы переподключиться


        
def main():
    global mqtt_client, history, seq_num, lora, led, button

    # Основной цикл
    print("\n\nStart main loop...")
    print("-" * 50)

    while True:
        try:
            # 1. Проверяем входящие MQTT
            if mqtt_client:
                try:
                    mqtt_client.check_msg()
                except Exception as e:
                    mqtt_client = None # требуем переподключение
                    print(f"    [MQTT] check_msg error: {repr(e)}")
            
            # Переподключаемся, если требуется
            if mqtt_client is None:
                ensure_mqtt()  # Автовосстановление сразу при разрыве
                    
            # 2. Всегда читаем входящие данные
            received = lora.receive_bytes()

            if received:
                try:
                    # Пытаемся декодировать как UTF-8 для вывода
                    payload = received.decode('utf-8')                        

                    print(f"[SIZE={len(received)}], msg: '{payload}'")
                except Exception as e:
                    print(e)
                    # Если не получается - выводим как hex
                    print(f"[SIZX={len(received)}], hex: {received.hex()}")
                    continue
                
                try:
                    do_payload(payload)
                except Exception as e:
                    print(f"Error: {e}")
            
            # 3. Отправляем в LoRa эфир сообщения, если надо и есть возможность
            result, msg = lora_tx_pump(lora, tx_queue, config_ga.LORA_TX_MIN_INTERVAL_MS)
            if result == "sent":
                print(f"sent: {msg}")

            time.sleep_ms(10)  # небольшая задержка
        
        except KeyboardInterrupt:
            print("\nStopped")
            sys.exit(0)
        except Exception as e:
            print(f"Error: {e}")
            print(f"Error: {repr(e)}")
            time.sleep_ms(500)

if __name__ == "__main__":
    if not connect_wifi():
        print("[WIFI] Connection failed; opening config portal")
        gateway_config_portal.run_config_portal(active_settings)

    # Инициализация LoRa
    print("Init LoRa...")
    lora = LoRaMiniLib(config_ga)
    print("LoRa OK")

    for i in range(3):
        try:
            sync_time() # Сразу синхронизируем время
            break
        except:
            time.sleep(1)

    while True:
        mqtt_client = connect_mqtt()
        if mqtt_client:
            break
        time.sleep(2)

    led.value(1)
    main()
