from fastapi import APIRouter, Depends, HTTPException
from typing import List
import asyncpg
import json

from ..deps import db_conn, tenant_guard, current_user
from ..schemas.notifications import (
    NotificationPrefIn,
    NotificationPrefOut,
    MetricRule,
    LatestReading,
)
from ..repositories import notifications as repo
from ..repositories import devices as devices_repo

router = APIRouter(prefix="/notifications", tags=["notifications"])


def _row_to_out(row) -> NotificationPrefOut:
    rules_raw = row["rules"]
    if isinstance(rules_raw, str):
        try:
            rules_raw = json.loads(rules_raw)
        except Exception:
            rules_raw = {}
    rules = {k: MetricRule(**v) for k, v in (rules_raw or {}).items() if isinstance(v, dict)}
    return NotificationPrefOut(
        device_id=row["device_id"],
        master_enabled=row["master_enabled"],
        rules=rules,
        updated_at=row["updated_at"],
    )


@router.get("/prefs", response_model=List[NotificationPrefOut])
async def list_prefs(
    user=Depends(current_user),
    tenant_id: str = Depends(tenant_guard),
    conn: asyncpg.Connection = Depends(db_conn),
):
    rows = await repo.list_for_user(conn, user_id=user["sub"], tenant_id=tenant_id)
    return [_row_to_out(r) for r in rows]


@router.get("/prefs/{device_id}", response_model=NotificationPrefOut)
async def get_prefs(
    device_id: str,
    user=Depends(current_user),
    tenant_id: str = Depends(tenant_guard),
    conn: asyncpg.Connection = Depends(db_conn),
):
    row = await repo.get_for_user(conn, user_id=user["sub"], device_id=device_id, tenant_id=tenant_id)
    if row is None:
        return NotificationPrefOut(device_id=device_id, master_enabled=True, rules={})
    return _row_to_out(row)


@router.put("/prefs/{device_id}", response_model=NotificationPrefOut)
async def put_prefs(
    device_id: str,
    payload: NotificationPrefIn,
    user=Depends(current_user),
    tenant_id: str = Depends(tenant_guard),
    conn: asyncpg.Connection = Depends(db_conn),
):
    owns = await devices_repo.get_device_by_id(conn, device_id, tenant_id)
    if owns is None:
        raise HTTPException(status_code=404, detail="Device not found")

    rules_dict = {k: v.model_dump() for k, v in payload.rules.items()}
    await repo.upsert(
        conn,
        user_id=user["sub"],
        device_id=device_id,
        master_enabled=payload.master_enabled,
        rules=rules_dict,
    )
    row = await repo.get_for_user(conn, user_id=user["sub"], device_id=device_id, tenant_id=tenant_id)
    return _row_to_out(row)


@router.delete("/prefs/{device_id}")
async def delete_prefs(
    device_id: str,
    user=Depends(current_user),
    tenant_id: str = Depends(tenant_guard),
    conn: asyncpg.Connection = Depends(db_conn),
):
    await repo.delete(conn, user_id=user["sub"], device_id=device_id)
    return {"ok": True}


@router.get("/latest", response_model=List[LatestReading])
async def latest(
    tenant_id: str = Depends(tenant_guard),
    conn: asyncpg.Connection = Depends(db_conn),
):
    rows = await repo.latest_readings(conn, tenant_id=tenant_id)
    return [
        LatestReading(
            device_id=r["device_id"],
            external_id=r["external_id"],
            temperature=r["temperature"],
            humidity=r["humidity"],
            battery=r["battery_level"],
            last_seen=r["last_seen"],
        )
        for r in rows
    ]
