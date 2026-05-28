import asyncio
import logging
import uuid
from datetime import datetime, timezone

import asyncpg
from asyncio_mqtt import Client, MqttError

from .services.config import settings
from .db import get_pool
from .repositories import devices as devrepo
from .repositories import telemetry as telrepo
from .repositories import commands as cmdrepo
from .instrumentation import (
    MQTT_MESSAGES_RECEIVED,
    MQTT_MESSAGES_ERRORS,
    MQTT_COMMANDS_PUBLISHED,
    MQTT_CONNECTED,
)

TOPIC_HUM = "+/+/sensors/+/humidity"
TOPIC_TMP = "+/+/sensors/+/temperature"
TOPIC_LOC = "+/+/sensors/+/location"
TOPIC_STATE = "+/+/sensors/+/state"
TOPIC_ACK = "+/+/devices/+/ack"
logger = logging.getLogger(__name__)
logging.getLogger().setLevel(logging.DEBUG)


def _iso(ts: str):
    return datetime.fromisoformat(ts.replace("Z", "+00:00"))


def _split_topic(topic: str):
    parts = str(topic).split("/")
    if len(parts) < 5:
        return None
    env = parts[0]
    tenant = parts[1]
    kind = parts[2]
    device = parts[3]
    leaf = parts[4]
    return env, tenant, kind, device, leaf


async def _set_rls(conn: asyncpg.Connection, tenant_name: str):
    try:
        await conn.execute("SET LOCAL app.tenant = $1", tenant_name)
    except Exception:
        pass


async def _device_belongs(conn: asyncpg.Connection, device_id: str, tenant_name: str) -> bool:
    return bool(await devrepo.is_belongs_to_tenant(conn, device_id, tenant_name))

async def _handle_humidity(line: str, conn: asyncpg.Connection, *, external_id: str, device_uuid: str):
    parts = line.split(",")
    if len(parts) < 3:
        raise ValueError(f"bad humidity payload: {line!r}")
    d, ts, h = parts[0], parts[1], parts[2]
    if d != external_id:
        return
    seq = int(parts[3]) if len(parts) > 3 and parts[3] else None
    await telrepo.insert_humidity(conn, device_id=device_uuid, ts=_iso(ts), humidity=float(h), seq=seq)


async def _handle_temperature(line: str, conn: asyncpg.Connection, *, external_id: str, device_uuid: str):
    parts = line.split(",")
    if len(parts) < 3:
        raise ValueError(f"bad temperature payload: {line!r}")
    d, ts, t = parts[0], parts[1], parts[2]
    if d != external_id:
        return
    seq = int(parts[3]) if len(parts) > 3 and parts[3] else None
    await telrepo.insert_temperature(conn, device_id=device_uuid, ts=_iso(ts), temperature=float(t), seq=seq)


async def _handle_location(line: str, conn: asyncpg.Connection, *, external_id: str, device_uuid: str):
    parts = line.split(",")
    if len(parts) < 4:
        raise ValueError(f"bad location payload: {line!r}")
    d, ts, lat, lon = parts[0], parts[1], parts[2], parts[3]
    if d != external_id:
        return
    await devrepo.upsert_location(conn, device_id=device_uuid, lat=float(lat), lon=float(lon), ts=_iso(ts))


async def _handle_state(line: str, conn: asyncpg.Connection, *, external_id: str, device_uuid: str):
    parts = line.split(",")
    if len(parts) < 6:
        raise ValueError(f"bad state payload: {line!r}")
    d, ts, rssi, snr, bat, online = parts[0], parts[1], parts[2], parts[3], parts[4], parts[5]
    if d != external_id:
        return
    await devrepo.upsert_state(
        conn,
        device_id=device_uuid,
        rssi=int(rssi),
        snr=float(snr),
        battery=float(bat),
        online=online,
        ts=_iso(ts),
    )


async def _handle_ack(line: str, conn: asyncpg.Connection):
    parts = line.split(",", 3)
    if not parts:
        raise ValueError(f"bad ack payload: {line!r}")
    cmd_id = parts[0]
    status = parts[2] if len(parts) > 2 else "unknown"
    details = parts[3] if len(parts) > 3 else None
    await cmdrepo.ack_command(conn, cmd_id=cmd_id, status=status, error=details)


def _format_params(params) -> str:
    if params is None:
        return ""
    if isinstance(params, str):
        return params
    if isinstance(params, dict):
        values = params.values()
    elif isinstance(params, (list, tuple)):
        values = params
    else:
        return str(params)
    return ",".join("" if v is None else str(v) for v in values)




_INTERVAL_METRICS = {"hum", "tmp", "stt", "geo"}


def _validate_interval_params(params) -> tuple[str, int]:
    metric = None
    seconds = None

    if isinstance(params, dict):
        metric = params.get("metric") or params.get("type") or params.get("kind")
        seconds = params.get("seconds") or params.get("interval") or params.get("value")
    elif isinstance(params, (list, tuple)) and len(params) >= 2:
        metric, seconds = params[0], params[1]
    elif isinstance(params, str) and "," in params:
        m, s = params.split(",", 1)
        metric, seconds = m.strip(), s.strip()

    if metric not in _INTERVAL_METRICS:
        raise ValueError(
            f"interval requires metric in {sorted(_INTERVAL_METRICS)}, got: {metric!r}"
        )
    try:
        seconds_int = int(seconds)
    except (TypeError, ValueError):
        raise ValueError(f"interval requires numeric seconds, got: {seconds!r}")
    if seconds_int <= 0:
        raise ValueError(f"interval seconds must be positive, got {seconds_int}")
    return metric, seconds_int


async def publish_command(
    *,
    tenant_id: str,
    device_id: str,
    type_: str,
    params: dict | list | str | None = None,
    retain: bool = False,
) -> str:
    if type_ == "interval":
        metric, seconds = _validate_interval_params(params)
        params = [metric, seconds]

    pool = None
    try:
        pool = await get_pool()
    except Exception:
        logger.exception("DB is not available; will retry later")

    async with pool.acquire() as conn:
        tenant_name = await conn.fetchval(
            "SELECT tenant_name FROM iot.tenant WHERE tenant_id::text = $1 OR tenant_name = $1",
            tenant_id,
        )
        if not tenant_name:
            raise ValueError("Tenant not found")

        if not await _device_belongs(conn, device_id, tenant_name):
            raise ValueError("Device does not belong to tenant")
        await _set_rls(conn, tenant_name)

        node_id = await devrepo.get_node_id(conn, device_id)
        if not node_id:
            raise ValueError(f"Device NODE_ID not found for device_id={device_id}")

        cmd_id = str(uuid.uuid4())
        ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        params_str = _format_params(params)
        payload = f"{cmd_id},{ts},{type_}" + (f",{params_str}" if params_str else "")
        topic = f"{settings.app_env}/{tenant_name}/devices/{node_id}/command"

        if isinstance(params, dict):
            db_params = params
        elif isinstance(params, (list, tuple)):
            db_params = {"args": list(params)}
        elif params is None:
            db_params = {}
        else:
            db_params = {"args": [params]}

        await cmdrepo.create_command(
            conn,
            device_id=device_id,
            cmd_id=cmd_id,
            type_=type_,
            params=db_params,
            retain=retain,
        )
        logger.info(
            "Created command %s (type=%s) device=%s node_id=%s tenant=%s",
            cmd_id, type_, device_id, node_id, tenant_name,
        )

    async with Client(
        settings.mqtt_host,
        settings.mqtt_port,
        username=settings.mqtt_username or None,
        password=settings.mqtt_password or None,
        keepalive=30,
    ) as client:
        logger.info("Publishing command %s to topic %s payload=%r", cmd_id, topic, payload)
        MQTT_COMMANDS_PUBLISHED.labels(type=type_).inc()
        await client.publish(topic, payload.encode("utf-8"), qos=1, retain=retain)

    async with pool.acquire() as conn:
        await _set_rls(conn, tenant_name)
        await cmdrepo.mark_sent(conn, cmd_id=cmd_id)
    return cmd_id

async def run_mqtt_forever():
    pool = await get_pool()
    print("MQTT: run_mqtt_forever entered", flush=True)
    while True:
        logger.info("Starting MQTT runtime...")
        try:
            logger.info(
                "Connecting to MQTT broker at %s:%d (env=%s)",
                settings.mqtt_host, settings.mqtt_port, settings.app_env
            )
            client_id = f"pvz-backend-{uuid.uuid4().hex[:8]}"
            async with Client(
                settings.mqtt_host,
                settings.mqtt_port,
                username=settings.mqtt_username or None,
                password=settings.mqtt_password or None,
                keepalive=30,
                client_id=client_id,
            ) as client:
                logger.info("Connected to MQTT broker client_id=%s", client_id)
                MQTT_CONNECTED.set(1)
                try:
                    async with client.messages() as messages:
                        async with asyncio.timeout(10):
                            await client.subscribe(TOPIC_HUM, qos=1)
                            await client.subscribe(TOPIC_TMP, qos=1)
                            await client.subscribe(TOPIC_LOC, qos=1)
                            await client.subscribe(TOPIC_STATE, qos=1)
                            await client.subscribe(TOPIC_ACK, qos=1)
                        logger.info("Subscribed to topics")
                        msg_iter = messages.__aiter__()
                        while True:
                            try:
                                msg = await asyncio.wait_for(anext(msg_iter), timeout=60)
                            except asyncio.TimeoutError:
                                logger.debug("No MQTT messages for 60s (still connected)")
                                continue
                            except StopAsyncIteration:
                                logger.info("MQTT messages stream ended. Reconnecting...")
                                break
                            try:
                                topic_str = str(msg.topic)
                                payload = msg.payload.decode("utf-8", errors="ignore")
                                logger.info("RAW MQTT: topic=%s payload=%r", topic_str, payload[:200])
                                meta = _split_topic(topic_str)
                                if not meta:
                                    continue
                                env, tenant_name, kind, device_id, leaf = meta
                                if env != settings.app_env:
                                    continue
                                async with pool.acquire() as conn:
                                    resolved = await devrepo.resolve_device_uuid(conn, device_id)
                                    if not resolved:
                                        resolved = await devrepo.get_or_create_by_external_id(
                                            conn,
                                            tenant_name=tenant_name,
                                            external_id=device_id,
                                            model="auto",
                                        )
                                        if resolved:
                                            logger.info("Auto-provisioned device external_id=%s tenant=%s -> %s", device_id, tenant_name, resolved)
                                        else:
                                            logger.info("Cannot provision device=%s: tenant=%s not found?", device_id, tenant_name)
                                            continue
                                    logger.info("Resolved %s -> UUID %s", device_id, resolved)
                                    belongs = await _device_belongs(conn, resolved, tenant_name)
                                    if not belongs:
                                        logger.info("Skipping: device %s not in tenant %s", resolved, tenant_name)
                                        continue
                                    if leaf == "humidity":
                                        await _handle_humidity(payload, conn, external_id=device_id, device_uuid=resolved)
                                    elif leaf == "temperature":
                                        await _handle_temperature(payload, conn, external_id=device_id, device_uuid=resolved)
                                    elif leaf == "location":
                                        await _handle_location(payload, conn, external_id=device_id, device_uuid=resolved)
                                    elif leaf == "state":
                                        await _handle_state(payload, conn, external_id=device_id, device_uuid=resolved)
                                    elif leaf == "ack":
                                        await _handle_ack(payload, conn)
                                    else:
                                        logger.info("Unknown leaf %s, ignoring", leaf)
                                    MQTT_MESSAGES_RECEIVED.labels(env=env, leaf=leaf).inc()
                                    logger.info("Stored: device=%s leaf=%s tenant=%s", resolved, leaf, tenant_name)
                            except Exception:
                                try:
                                    MQTT_MESSAGES_ERRORS.labels(env=env, leaf=leaf).inc()
                                except Exception:
                                    pass
                                logger.exception("Error processing MQTT message")
                except MqttError as e:
                    logger.exception("MQTT error: %r", e)
                    await asyncio.sleep(2)
                    continue
                except Exception:
                    logger.exception("MQTT unexpected crash")
                    await asyncio.sleep(2)
                    continue
        except TimeoutError:
            logger.exception("Timeout connecting/subscribing, retrying in 2s")
            await asyncio.sleep(2)
        except MqttError:
            MQTT_CONNECTED.set(0)
            logger.exception("MQTT connection error, retrying in 2s")
            await asyncio.sleep(2)
        except Exception:
            logger.exception("MQTT runtime crashed, retrying in 2s")
            await asyncio.sleep(2)
