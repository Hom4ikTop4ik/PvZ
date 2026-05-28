import asyncpg

async def insert_humidity(conn, device_id: str, ts, humidity: float, seq: int | None):

    await conn.execute(
        """
        INSERT INTO iot.monitoring_raw(device_id, sent_ts, humidity, seq)
        VALUES ($1, $2, $3, $4)
        ON CONFLICT (device_id, sent_ts) DO UPDATE
          SET humidity = EXCLUDED.humidity,
              seq      = COALESCE(EXCLUDED.seq, iot.monitoring_raw.seq)
        """,
        device_id, ts, humidity, seq,
    )


async def insert_temperature(conn, device_id: str, ts, temperature: float, seq: int | None):

    await conn.execute(
        """
        INSERT INTO iot.monitoring_raw(device_id, sent_ts, temperature, seq)
        VALUES ($1, $2, $3, $4)
        ON CONFLICT (device_id, sent_ts) DO UPDATE
          SET temperature = EXCLUDED.temperature,
              seq         = COALESCE(EXCLUDED.seq, iot.monitoring_raw.seq)
        """,
        device_id, ts, temperature, seq,
    )


async def query_humidity(conn: asyncpg.Connection, device_id: str, since, until):
    return await conn.fetch(
        """
        SELECT sent_ts, humidity
        FROM iot.monitoring_raw
        WHERE device_id=$1 AND sent_ts >= $2 AND sent_ts <= $3
          AND humidity IS NOT NULL
        ORDER BY sent_ts
        """,
        device_id, since, until,
    )


async def query_temperature(conn: asyncpg.Connection, device_id: str, since, until):
    return await conn.fetch(
        """
        SELECT sent_ts, temperature
        FROM iot.monitoring_raw
        WHERE device_id=$1 AND sent_ts >= $2 AND sent_ts <= $3
          AND temperature IS NOT NULL
        ORDER BY sent_ts
        """,
        device_id, since, until,
    )
