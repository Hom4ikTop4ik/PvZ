import asyncpg
import json


async def list_for_user(conn: asyncpg.Connection, *, user_id: str, tenant_id: str):
    return await conn.fetch(
        """
        SELECT p.device_id::text, p.master_enabled, p.rules, p.updated_at
        FROM iot.user_notification_prefs p
        JOIN iot.devices d ON d.device_id = p.device_id
        WHERE p.user_id::text = $1 AND d.tenant_id::text = $2
        """,
        user_id, tenant_id,
    )


async def get_for_user(
    conn: asyncpg.Connection,
    *,
    user_id: str,
    device_id: str,
    tenant_id: str,
):
    return await conn.fetchrow(
        """
        SELECT p.device_id::text, p.master_enabled, p.rules, p.updated_at
        FROM iot.user_notification_prefs p
        JOIN iot.devices d ON d.device_id = p.device_id
        WHERE p.user_id::text = $1
          AND p.device_id::text = $2
          AND d.tenant_id::text = $3
        """,
        user_id, device_id, tenant_id,
    )


async def upsert(
    conn: asyncpg.Connection,
    *,
    user_id: str,
    device_id: str,
    master_enabled: bool,
    rules: dict,
):
    await conn.execute(
        """
        INSERT INTO iot.user_notification_prefs(user_id, device_id, master_enabled, rules)
        VALUES($1, $2, $3, $4::jsonb)
        ON CONFLICT (user_id, device_id) DO UPDATE
          SET master_enabled = EXCLUDED.master_enabled,
              rules          = EXCLUDED.rules,
              updated_at     = now()
        """,
        user_id, device_id, master_enabled, json.dumps(rules or {}),
    )


async def delete(conn: asyncpg.Connection, *, user_id: str, device_id: str):
    await conn.execute(
        """
        DELETE FROM iot.user_notification_prefs
        WHERE user_id::text = $1 AND device_id::text = $2
        """,
        user_id, device_id,
    )


async def latest_readings(conn: asyncpg.Connection, *, tenant_id: str):
    return await conn.fetch(
        """
        WITH latest AS (
          SELECT m.device_id,
                 (SELECT humidity FROM iot.monitoring_raw m2
                   WHERE m2.device_id = m.device_id AND m2.humidity IS NOT NULL
                   ORDER BY m2.sent_ts DESC LIMIT 1) AS humidity,
                 (SELECT temperature FROM iot.monitoring_raw m3
                   WHERE m3.device_id = m.device_id AND m3.temperature IS NOT NULL
                   ORDER BY m3.sent_ts DESC LIMIT 1) AS temperature
          FROM (SELECT DISTINCT device_id FROM iot.monitoring_raw) m
        )
        SELECT d.device_id::text, d.external_id,
               latest.temperature, latest.humidity,
               s.battery_level, s.last_seen
        FROM iot.devices d
        LEFT JOIN latest    ON latest.device_id = d.device_id
        LEFT JOIN iot.state s ON s.device_id = d.device_id
        WHERE d.tenant_id::text = $1
        """,
        tenant_id,
    )
