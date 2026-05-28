from fastapi import APIRouter, Depends, HTTPException
import asyncpg
from ..deps import current_user, db_conn
from ..schemas.tenants import (
    TenantCreateIn, GatewayCreateIn, BackendCreateIn, MoveGatewayIn, ToggleClientIn
)
from ..services.tenants import (
    create_tenant, delete_tenant, create_gateway_client, create_backend_client,
    move_gateway_to_tenant
)
from ..services.mosq_dynsec import DynSecError
from ..auth import User, make_token
from ..repositories.auth import get_user_by_username
import logging

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/tenants", tags=["tenants"])


@router.get("", summary="Список тенантов для пользователя")
async def api_list_tenants(user=Depends(current_user), conn: asyncpg.Connection = Depends(db_conn)):
    rows = await conn.fetch(
        """
        SELECT t.tenant_id::text, t.tenant_name AS name,
               t.tenant_owner AS description, t.created_at
        FROM iot.tenant t
        WHERE t.tenant_owner = $1
           OR t.tenant_name = 'fake'
        ORDER BY t.created_at
        """,
        user.get('name')
    )
    return [dict(r) for r in rows]


def _require_admin(user: dict):
    if user.get("permissions") not in {"ADMIN", "OWNER"}:
        raise HTTPException(403, "Not enough permissions")


@router.post("", summary="Создать тенант (роли + ACL)")
async def api_create_tenant(
    payload: TenantCreateIn,
    user=Depends(current_user),
    conn: asyncpg.Connection = Depends(db_conn),
):
    tenant_name = payload.tenant

    tenant_id = await conn.fetchval(
        "SELECT tenant_id::text FROM iot.tenant WHERE tenant_name = $1",
        tenant_name,
    )

    if not tenant_id:
        tenant_id = await conn.fetchval(
            """
            INSERT INTO iot.tenant (tenant_name, tenant_owner)
            VALUES ($1, $2)
            RETURNING tenant_id::text
            """,
            tenant_name, user.get('name'),
        )
    else:
        if tenant_name != "fake":
            raise HTTPException(status_code=400, detail="Tenant already exists")

    try:
        await conn.execute(
            "UPDATE iot.users SET tenant_id = $1::uuid, permissions = 'OWNER' WHERE name = $2",
            tenant_id, user.get('name'),
        )
        logger.info("Updated user %s -> tenant %s, permissions=OWNER", user.get('name'), tenant_id)
    except Exception:
        logger.exception("Failed to update user tenant/permissions for %s", user.get('name'))
        raise HTTPException(status_code=500, detail="Failed to assign tenant ownership")

    updated_user_row = await get_user_by_username(conn, username=user.get('name'))
    if updated_user_row:
        fresh_user = User(
            user_id=str(updated_user_row.get("user_id")),
            name=updated_user_row.get("name"),
            tenant_id=str(updated_user_row.get("tenant_id")) if updated_user_row.get("tenant_id") else None,
            permissions=str(updated_user_row.get("permissions")) if updated_user_row.get("permissions") else "OWNER",
        )
        access_token = make_token(fresh_user)
    else:
        access_token = None

    return {"tenant": tenant_name, "tenant_id": tenant_id, "access_token": access_token}


@router.delete("/{tenant}", summary="Удалить тенант (роли)")
async def api_delete_tenant(tenant: str, user=Depends(current_user)):
    _require_admin(user)
    return await delete_tenant(tenant)


@router.post("/gateways", summary="Создать шлюз-клиента и выдать роль тенанта")
async def api_create_gateway(payload: GatewayCreateIn, user=Depends(current_user)):
    _require_admin(user)
    return await create_gateway_client(payload.tenant, payload.client_id, payload.password)


@router.post("/backends", summary="Создать backend-клиента и выдать роль тенанта")
async def api_create_backend(payload: BackendCreateIn, user=Depends(current_user)):
    _require_admin(user)
    return await create_backend_client(payload.tenant, payload.username, payload.password)


@router.post("/gateways/move", summary="Перенести шлюз между тенантами")
async def api_move_gateway(payload: MoveGatewayIn, user=Depends(current_user)):
    _require_admin(user)
    return await move_gateway_to_tenant(payload.client_id, payload.old_tenant, payload.new_tenant)