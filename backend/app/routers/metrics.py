import time
from fastapi import APIRouter, Response
import asyncpg

from ..db import get_pool
from ..instrumentation import prometheus_text_response

router = APIRouter(tags=["metrics"])

_start_time = time.time()


async def _collect_iot(conn: asyncpg.Connection) -> str:
    blocks: list[list[str]] = []

    uptime = time.time() - _start_time
    blocks.append([
        "# HELP pvz_uptime_seconds Seconds since backend process started",
        "# TYPE pvz_uptime_seconds counter",
        f"pvz_uptime_seconds {uptime:.3f}",
    ])

    rows = await conn.fetch("""
        SELECT t.tenant_name, COUNT(d.device_id)::int AS cnt
        FROM iot.tenant t
        LEFT JOIN iot.devices d ON d.tenant_id = t.tenant_id
        GROUP BY t.tenant_name
    """)
    lines = [
        "# HELP pvz_devices_total Total registered devices per tenant",
        "# TYPE pvz_devices_total gauge",
    ]
    for r in rows:
        lines.append(f'pvz_devices_total{{tenant="{r["tenant_name"]}"}} {r["cnt"]}')
    blocks.append(lines)

    rows = await conn.fetch("""
        SELECT t.tenant_name,
               d.external_id,
               d.device_id::text,
               COALESCE(s.status, false) AS online,
               COALESCE(s.battery_level, -1) AS battery,
               COALESCE(s.rssi, 0) AS rssi,
               COALESCE(s.snr, 0) AS snr,
               EXTRACT(EPOCH FROM (now() - s.last_seen))::float AS seconds_since_seen
        FROM iot.devices d
        JOIN iot.tenant t ON t.tenant_id = d.tenant_id
        LEFT JOIN iot.state s ON s.device_id = d.device_id
    """)

    online_lines  = ["# HELP pvz_device_online 1 if device is online 0 otherwise",
                     "# TYPE pvz_device_online gauge"]
    battery_lines = ["# HELP pvz_device_battery_percent Battery level percent 0-100 or -1 if unknown",
                     "# TYPE pvz_device_battery_percent gauge"]
    rssi_lines    = ["# HELP pvz_device_rssi_dbm RSSI signal strength in dBm",
                     "# TYPE pvz_device_rssi_dbm gauge"]
    snr_lines     = ["# HELP pvz_device_snr_db SNR signal-to-noise ratio in dB",
                     "# TYPE pvz_device_snr_db gauge"]
    seen_lines    = ["# HELP pvz_device_seconds_since_seen Seconds since last state update",
                     "# TYPE pvz_device_seconds_since_seen gauge"]

    for r in rows:
        dev = r["external_id"] or r["device_id"]
        lbl = f'tenant="{r["tenant_name"]}",device="{dev}"'
        online_lines.append(f"pvz_device_online{{{lbl}}} {1 if r['online'] else 0}")
        battery_lines.append(f"pvz_device_battery_percent{{{lbl}}} {r['battery']:.1f}")
        rssi_lines.append(f"pvz_device_rssi_dbm{{{lbl}}} {r['rssi']}")
        snr_lines.append(f"pvz_device_snr_db{{{lbl}}} {r['snr']:.2f}")
        if r["seconds_since_seen"] is not None:
            seen_lines.append(f"pvz_device_seconds_since_seen{{{lbl}}} {r['seconds_since_seen']:.1f}")

    blocks.extend([online_lines, battery_lines, rssi_lines, snr_lines, seen_lines])

    rows = await conn.fetch("""
        WITH latest_temp AS (
            SELECT DISTINCT ON (device_id) device_id, temperature
            FROM iot.monitoring_raw WHERE temperature IS NOT NULL
            ORDER BY device_id, sent_ts DESC
        ),
        latest_hum AS (
            SELECT DISTINCT ON (device_id) device_id, humidity
            FROM iot.monitoring_raw WHERE humidity IS NOT NULL
            ORDER BY device_id, sent_ts DESC
        )
        SELECT t.tenant_name, d.external_id, d.device_id::text,
               lt.temperature, lh.humidity
        FROM iot.devices d
        JOIN iot.tenant t ON t.tenant_id = d.tenant_id
        LEFT JOIN latest_temp lt ON lt.device_id = d.device_id
        LEFT JOIN latest_hum  lh ON lh.device_id = d.device_id
        WHERE lt.temperature IS NOT NULL OR lh.humidity IS NOT NULL
    """)

    temp_lines = ["# HELP pvz_temperature_celsius Latest temperature reading in Celsius",
                  "# TYPE pvz_temperature_celsius gauge"]
    hum_lines  = ["# HELP pvz_humidity_percent Latest humidity reading in percent",
                  "# TYPE pvz_humidity_percent gauge"]
    for r in rows:
        dev = r["external_id"] or r["device_id"]
        lbl = f'tenant="{r["tenant_name"]}",device="{dev}"'
        if r["temperature"] is not None:
            temp_lines.append(f"pvz_temperature_celsius{{{lbl}}} {float(r['temperature']):.2f}")
        if r["humidity"] is not None:
            hum_lines.append(f"pvz_humidity_percent{{{lbl}}} {float(r['humidity']):.2f}")
    blocks.extend([temp_lines, hum_lines])

    rows = await conn.fetch("""
        SELECT t.tenant_name, d.external_id, d.device_id::text,
               COUNT(*) FILTER (WHERE m.temperature IS NOT NULL) AS temp_count,
               COUNT(*) FILTER (WHERE m.humidity IS NOT NULL)    AS hum_count
        FROM iot.monitoring_raw m
        JOIN iot.devices d ON d.device_id = m.device_id
        JOIN iot.tenant  t ON t.tenant_id = d.tenant_id
        WHERE m.sent_ts > now() - INTERVAL '24 hours'
        GROUP BY t.tenant_name, d.external_id, d.device_id
    """)

    tc_lines = ["# HELP pvz_telemetry_points_24h Telemetry points ingested in last 24h",
                "# TYPE pvz_telemetry_points_24h counter"]
    for r in rows:
        dev = r["external_id"] or r["device_id"]
        lbl_t = f'tenant="{r["tenant_name"]}",device="{dev}",metric="temperature"'
        lbl_h = f'tenant="{r["tenant_name"]}",device="{dev}",metric="humidity"'
        tc_lines.append(f"pvz_telemetry_points_24h{{{lbl_t}}} {r['temp_count']}")
        tc_lines.append(f"pvz_telemetry_points_24h{{{lbl_h}}} {r['hum_count']}")
    blocks.append(tc_lines)

    out = []
    for block in blocks:
        out.extend(block)
        out.append("")
    return "\n".join(out)


@router.get("/metrics", response_class=Response)
async def metrics():
    pool = await get_pool()
    async with pool.acquire() as conn:
        iot_text = await _collect_iot(conn)

    prom_resp = prometheus_text_response()
    combined = prom_resp.body.decode("utf-8") + "\n" + iot_text
    return Response(
        content=combined,
        media_type="text/plain; version=0.0.4; charset=utf-8",
    )