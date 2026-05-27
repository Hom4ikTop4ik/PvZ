import asyncpg
import json

async def create_command(conn: asyncpg.Connection, *, device_id: str, cmd_id: str, type_: str, params: dict, retain: bool = False):
    await conn.execute(
        """
        INSERT INTO iot.device_commands(cmd_id, device_id, type, params, retain)
        VALUES($1,$2,$3,$4::jsonb,$5)
        ON CONFLICT DO NOTHING
        """,
        cmd_id, device_id, type_, json.dumps(params or {}), retain,
    )

async def ack_command(conn: asyncpg.Connection, *, cmd_id: str, status: str, error: str | None):
    await conn.execute(
        """
        UPDATE iot.device_commands
        SET status=$2, ack_ts=now(), error=$3
        WHERE cmd_id=$1
        """,
        cmd_id, status, error,
    )

async def mark_sent(conn: asyncpg.Connection, *, cmd_id: str):
    await conn.execute(
        """
        UPDATE iot.device_commands
        SET sent_ts = now()
        WHERE cmd_id = $1
        """,
        cmd_id,
    )


async def list_for_device(
    conn: asyncpg.Connection,
    *,
    device_id: str,
    tenant_id: str,
    limit: int = 50,
):
    return await conn.fetch(
        """
        SELECT c.cmd_id::text, c.device_id::text, c.type, c.params, c.retain,
               c.issued_ts, c.sent_ts, c.ack_ts, c.status, c.error
        FROM iot.device_commands c
        JOIN iot.devices d ON d.device_id = c.device_id
        WHERE c.device_id::text = $1 AND d.tenant_id::text = $2
        ORDER BY c.issued_ts DESC
        LIMIT $3
        """,
        device_id, tenant_id, limit,
    )


async def get_by_id(
    conn: asyncpg.Connection,
    *,
    cmd_id: str,
    device_id: str,
    tenant_id: str,
):
    return await conn.fetchrow(
        """
        SELECT c.cmd_id::text, c.device_id::text, c.type, c.params, c.retain,
               c.issued_ts, c.sent_ts, c.ack_ts, c.status, c.error
        FROM iot.device_commands c
        JOIN iot.devices d ON d.device_id = c.device_id
        WHERE c.cmd_id::text = $1 AND c.device_id::text = $2 AND d.tenant_id::text = $3
        """,
        cmd_id, device_id, tenant_id,
    )