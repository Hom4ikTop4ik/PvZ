# Развёртывание PvZ-1 на Timeweb Cloud

## Что создаём

| Ресурс | Тип | Домен / адрес |
|---|---|---|
| Приватная сеть | VPC | `192.168.10.0/24` |
| MQTT-1, MQTT-2 | VPS × 2 | приватная сеть |
| Балансер | Load Balancer | публичный IP → `mqtt.nsupvz.ru` |
| База данных | Managed PostgreSQL | приватная сеть |
| Backend | App Platform | `api.nsupvz.ru` |
| Frontend | App Platform | `nsupvz.ru` |

---

## 1. Приватная сеть

**Панель → Сети → Создать VPC**

- Название: `pvz-private-net`
- Подсеть IPv4: `192.168.10.0/24`
- Зона доступности: `ru-1`

---

## 2. Два MQTT-сервера

**Панель → Облачные серверы → Создать сервер** — повторить дважды.

Настройки одинаковые для обоих:

- ОС: Ubuntu 22.04
- Конфигурация: 1 vCPU, 1 GB RAM, 15 GB NVMe
- Приватная сеть: `pvz-private-net`
- SSH-ключ: ваш

Имена: `pvz-mqtt-1` и `pvz-mqtt-2`.

> Запишите приватные IP обоих серверов.

---

### Настройка каждого MQTT-сервера

Подключитесь по SSH и выполните на каждом:

```bash
apt update && apt install -y mosquitto mosquitto-clients ufw certbot
```

**Сертификат** (выполнять пока Mosquitto не запущен):

```bash
certbot certonly --standalone -d mqtt.nsupvz.ru
mkdir -p /etc/mosquitto/certs
cp /etc/letsencrypt/live/mqtt.nsupvz.ru/fullchain.pem /etc/mosquitto/certs/
cp /etc/letsencrypt/live/mqtt.nsupvz.ru/privkey.pem   /etc/mosquitto/certs/
cp /etc/letsencrypt/live/mqtt.nsupvz.ru/chain.pem     /etc/mosquitto/certs/
chown -R mosquitto:mosquitto /etc/mosquitto/certs
```

**Пароль для backend-клиента:**

```bash
mosquitto_passwd -c /etc/mosquitto/passwd pvz-backend
```

**Конфиг** — создайте файл `/etc/mosquitto/conf.d/pvz.conf`:

```
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd

listener 8883
certfile /etc/mosquitto/certs/fullchain.pem
keyfile  /etc/mosquitto/certs/privkey.pem
cafile   /etc/mosquitto/certs/chain.pem
tls_version tlsv1.2
allow_anonymous false
password_file /etc/mosquitto/passwd

persistence true
persistence_location /var/lib/mosquitto/
log_dest file /var/log/mosquitto/mosquitto.log
log_type error
log_type warning
```

**Фаервол и запуск:**

```bash
ufw allow 22/tcp
ufw allow 1883/tcp
ufw allow 8883/tcp
ufw --force enable

systemctl enable --now mosquitto
```

**Проверка:**

```bash
systemctl status mosquitto
mosquitto_sub -h localhost -p 1883 -u pvz-backend -P <пароль> -t "test/#" -v &
mosquitto_pub -h localhost -p 1883 -u pvz-backend -P <пароль> -t "test/ping" -m "ok"
```

---

## 3. Балансер нагрузки

**Панель → Балансировщики → Создать балансировщик**

- Название: `pvz-mqtt-lb`
- Протокол: TCP
- Алгоритм: Round Robin
- Приватная сеть: `pvz-private-net`

**Бэкенды** — добавить оба сервера по приватному IP:

| Сервер | IP |
|---|---|
| pvz-mqtt-1 | `<приватный IP mqtt-1>` |
| pvz-mqtt-2 | `<приватный IP mqtt-2>` |

**Правила проброса портов:**

| Входящий порт | Порт бэкенда |
|---|---|
| 1883 | 1883 |
| 8883 | 8883 |

**Health check:** TCP, порт 1883.

> Запишите публичный IP балансера.

---

## 4. База данных PostgreSQL

**Панель → Базы данных → Создать кластер**

- Тип: PostgreSQL 16
- Название: `pvz-postgres`
- Приватная сеть: `pvz-private-net`

После создания кластера в разделе управления:

1. Создать базу данных: `pvzdb`
2. Создать пользователя: `pvz`, задать пароль

> Запишите приватный хост кластера (вида `192.168.10.x`).

**Применить схему** — запустите с любого сервера в той же VPC:

```bash
psql postgresql://pvz:<пароль>@<приватный-хост>:5432/pvzdb \
  -f IaC/localhosting/db/initdb/schema.sql
```

---

## 5. Backend

**Панель → App Platform → Создать приложение**

| Поле | Значение |
|---|---|
| Репозиторий | `PvZ-1` |
| Директория | `backend/` |
| Ветка | `main` |
| Тип сборки | Dockerfile |
| Порт | `8000` |
| Домен | `api.nsupvz.ru` |
| Приватная сеть | `pvz-private-net` |

**Переменные окружения:**

| Переменная | Значение |
|---|---|
| `APP_ENV` | `prod` |
| `APP_SECRET` | вывод `openssl rand -hex 32` |
| `APP_TENANT` | `fake` |
| `APP_JWT_EXPIRES_MIN` | `60` |
| `MQTT_HOST` | публичный IP балансера (шаг 3) |
| `MQTT_PORT` | `1883` |
| `MQTT_USERNAME` | `pvz-backend` |
| `MQTT_PASSWORD` | пароль из шага 2 |
| `DB_HOST` | приватный хост PostgreSQL (шаг 4) |
| `DB_PORT` | `5432` |
| `DB_USER` | `pvz` |
| `DB_PASSWORD` | пароль из шага 4 |
| `DB_NAME` | `pvzdb` |
| `ALLOW_ORIGIN` | `https://nsupvz.ru` |

---

## 6. Frontend

**Панель → App Platform → Создать приложение**

| Поле | Значение |
|---|---|
| Репозиторий | `PvZ-1` |
| Директория | `frontend-app/` |
| Ветка | `main` |
| Тип | Static Site |
| Build command | `npm run build` |
| Output directory | `dist` |
| Домен | `nsupvz.ru` |

**Переменные окружения:**

| Переменная | Значение |
|---|---|
| `VITE_API_BASE` | `https://api.nsupvz.ru` |

---

## 7. DNS

У вашего регистратора создайте A-записи:

| Запись | Значение |
|---|---|
| `nsupvz.ru` | IP App Platform frontend |
| `api.nsupvz.ru` | IP App Platform backend |
| `mqtt.nsupvz.ru` | публичный IP балансера |

---
