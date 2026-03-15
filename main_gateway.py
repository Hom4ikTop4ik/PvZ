import machine
import network
import ntptime
import random
from umqtt.simple import MQTTClient

import sys
import time

from lora_mini_lib import LoRaMiniLib
import config_gateway as config_ga

from utils import RingBuffer, LoRaTxQueue, lora_tx_pump




# Инициализация периферии
led = machine.Pin(config_ga.PIN_LED, machine.Pin.OUT, value=1)  # выключен
button = machine.Pin(config_ga.PIN_BUTTON, machine.Pin.IN, machine.Pin.PULL_UP)

for i in range(20):
    if not button.value():
        # config_ga.WIFI_SSID = "BDD"
        # config_ga.WIFI_PASS = "aboba123"
        config_ga.WIFI_SSID = "B"
        config_ga.WIFI_PASS = "1111117a"
        break
    led.value(1)
    time.sleep(0.1)
    led.value(0)
    time.sleep(0.1)

# Глобальный клиент
mqtt_client = None

# Инициализация LoRa
print("Init LoRa...")
lora = LoRaMiniLib(config_ga)
print("LoRa OK")

# Таймер для антидребезга кнопки
last_button_press_time = 0
DEBOUNCE_MS = 300

# Тестовое сообщение
def create_test_message():
    message = 'a'*240    
    return message.encode('utf-8')

# Основной цикл
print("Start main loop (press BOOT to send)...")
print("-" * 50)

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
def connect_wifi():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(False)
    time.sleep(1)
    wlan.active(True)
    time.sleep(1)
    
    if not wlan.isconnected():
        print(f"[WIFI] Connecting to '{config_ga.WIFI_SSID}' '{config_ga.WIFI_PASS}'...")
        wlan.connect(config_ga.WIFI_SSID, config_ga.WIFI_PASS)

        status_codes = {
            4:    "STAT_DISCONNECTED - отключено",
            5:    "STAT_CONNECT_FAIL - ошибка соединения",
            200:  "STAT_BEACON_TIMEOUT - Потеря сигнала от точки доступа, таймаут маячка",
            201:  "STAT_NO_AP_FOUND - точка не найдена",
            202:  "STAT_WRONG_PASSWORD - неверный пароль!",
            203:  "STAT_ASSOC_FAIL - Ошибка ассоциации с точкой доступа",
            204:  "STAT_HANDSHAKE_TIMEOUT - таймаут рукопожатия",
            1000: "STAT_IDLE - интерфейс свободен",
            1001: "STAT_IDLE - нет соединения",
            1002: "STAT_CONNECTING - подключаемся",
            1010: "STAT_GOT_IP - получен IP!"
        }
        
        # max_wait = 20
        # while max_wait > 0:
            # status = wlan.status()
            # print(f"    {status}")
                # break
            # max_wait -= 1
        #     time.sleep(1)
        while not wlan.isconnected():
            machine.idle()
            
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
    # даже если params пусто, оставить пустым: 
    #   "{cmd}," чтобы единообразный формат был
    payload = f"{device_id};{timestamp};{msg_rnd_id};cmd;{cmd},{','.join(map(str, params))}"
    return _enqueue_payload(payload)


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
        # 1. Разбираем топик: {env}/{tenant}/sensors/{device_id}/command
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
                params = cmd_parts[3]
                
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
    global mqtt_client, history, seq_num, last_button_press_time, lora, led, button
    device_id, timestamp, msg_rnd_id, msg_type, msg = payload.split(";")
    
    if (device_id, timestamp, msg_rnd_id) in history:
        print("History has this")
        return
    history.add((device_id, timestamp, msg_rnd_id))
    
    # Проверяем MQTT
    if not ensure_mqtt():
        print("[WARN] No MQTT, skipping")
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
        elif msg_type == "geo":
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
    global mqtt_client, history, seq_num, last_button_press_time, lora, led, button

    while True:
        try:
            # Всегда читаем входящие данные
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
                
                try:
                    do_payload(payload)
                except Exception as e:
                    print(f"Error: {e}")
            

            result, msg = lora_tx_pump(lora, tx_queue, config_ga.LORA_TX_MIN_INTERVAL_MS)
            if result == "sent":
                print(f"sent: {msg}")

            
            # # Проверка кнопки с антидребезгом
            # if button.value() == 0:  # кнопка нажата
            #     current_time = time.ticks_ms()
            #     if time.ticks_diff(current_time, last_button_press_time) > DEBOUNCE_MS:
            #         last_button_press_time = current_time
                    
            #         # Мигаем светодиодом
            #         led.value(0)  # включить
                    
            #         # Создаем и отправляем сообщение
            #         device_id = config_ga.NODE_ID
            #         timestamp = get_iso_timestamp()
            #         msg_rnd_id = random.randint(0, 999_999)
            #         msg_type = "cmd"
            #         msg = "SLEEP"
            #         payload = f"{device_id};{timestamp};{msg_rnd_id};{msg_type};{msg}"
            #         print(f"\n[SENDING] {len(payload)} bytes...")
                    
            #         result = lora.send_bytes(payload)
                    
            #         if result == 0:
            #             print("[SEND OK]")
            #         else:
            #             print(f"[SEND ERROR] code={result}")
                    
            #         led.value(1)  # выключить
            
            time.sleep_ms(10)  # небольшая задержка
        
        except KeyboardInterrupt:
            print("\nStopped")
            sys.exit(0)
        except Exception as e:
            print(f"Error: {e}")
            time.sleep_ms(500)

import network

def scan_wifi():
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    
    print("Сканирование доступных сетей...")
    networks = wlan.scan()
    
    print("\nНайденные сети:")
    print("-" * 50)
    
    found = False
    for net in networks:
        ssid = net[0].decode('utf-8')  # Имя сети
        bssid = ':'.join('%02x' % b for b in net[1])  # MAC адрес
        channel = net[2]  # Канал
        rssi = net[3]  # Сила сигнала
        authmode = net[4]  # Тип шифрования
        
        print(f"SSID: {ssid}")
        print(f"  Канал: {channel}, Сигнал: {rssi} dBm")
        print(f"  BSSID: {bssid}")
        print(f"  Шифрование: {authmode}")
        print()
        
        if ssid == config_ga.WIFI_SSID:
            print(">>> ВАША СЕТЬ НАЙДЕНА! <<<")
            found = True
    
    if not found:
        print(f"Сеть '{config_ga.WIFI_SSID}' НЕ найдена!")
    
    return found

if __name__ == "__main__":
    led.value(0)

    while not connect_wifi():
        pass

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
