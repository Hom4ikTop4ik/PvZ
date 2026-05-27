from fastapi import APIRouter, Depends, HTTPException, Query
from typing import List
import asyncpg
import json
from ..deps import db_conn, tenant_guard
from ..schemas.devices import DeviceOut, CommandIn, CommandOut
from ..repositories import devices as repo
from ..repositories import commands as cmd_repo
from ..mqtt_runtime import publish_command

router = APIRouter(prefix="/devices", tags=["devices"])

@router.get("/", response_model=List[DeviceOut])
async def list_devices(tenant_id: str | None = Query(None, alias='tenant_id'), tenant_from_token: str = Depends(tenant_guard), conn: asyncpg.Connection = Depends(db_conn)):
    effective_tenant = tenant_id or tenant_from_token
    rows = await repo.list_devices(conn, effective_tenant)
    return [
        DeviceOut(
            device_id=r["device_id"], external_id=r["external_id"], model=r["model"], status=r["status"],
            rssi=r["rssi"], snr=r["snr"], battery=r["battery_level"], online=r["online"],
            lat=r["lat"], lon=r["lon"], location_updated_at=r["location_updated_at"],
        )
        for r in rows
    ]

@router.post("/{device_id}/command")
async def send_command(device_id: str, payload: CommandIn, tenant_id: str = Depends(tenant_guard)):
    try:
        cmd_id = await publish_command(
            tenant_id=tenant_id,
            device_id=device_id,
            type_=payload.type,
            params=payload.params,
            retain=payload.retain,
        )
    except ValueError as e:
        raise HTTPException(status_code=403, detail=str(e))
    return {"command_id": cmd_id}


def _command_row_to_out(row) -> CommandOut:
    raw = row["params"]
    if isinstance(raw, str):
        try:
            params = json.loads(raw)
        except Exception:
            params = raw
    else:
        params = raw
    return CommandOut(
        cmd_id=row["cmd_id"],
        device_id=row["device_id"],
        type=row["type"],
        params=params,
        retain=row["retain"],
        issued_ts=row["issued_ts"],
        sent_ts=row["sent_ts"],
        ack_ts=row["ack_ts"],
        status=row["status"],
        error=row["error"],
    )


@router.get("/{device_id}/commands", response_model=List[CommandOut])
async def list_commands(
    device_id: str,
    limit: int = Query(50, ge=1, le=500),
    tenant_id: str = Depends(tenant_guard),
    conn: asyncpg.Connection = Depends(db_conn),
):
    rows = await cmd_repo.list_for_device(conn, device_id=device_id, tenant_id=tenant_id, limit=limit)
    return [_command_row_to_out(r) for r in rows]


@router.get("/{device_id}/commands/{cmd_id}", response_model=CommandOut)
async def get_command(
    device_id: str,
    cmd_id: str,
    tenant_id: str = Depends(tenant_guard),
    conn: asyncpg.Connection = Depends(db_conn),
):
    row = await cmd_repo.get_by_id(conn, cmd_id=cmd_id, device_id=device_id, tenant_id=tenant_id)
    if row is None:
        raise HTTPException(status_code=404, detail="Command not found")
    return _command_row_to_out(row)
