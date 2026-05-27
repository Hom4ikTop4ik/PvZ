from fastapi import Depends, HTTPException, Query
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from .auth import decode_token
import asyncpg
from .db import get_pool
from .services.config import settings
import re
import logging

logger = logging.getLogger(__name__)
bearer = HTTPBearer(auto_error=False)


def current_user(creds: HTTPAuthorizationCredentials | None = Depends(bearer)) -> dict:
    if creds is None:
        raise HTTPException(status_code=401, detail="Not authenticated")
    try:
        return decode_token(creds.credentials)
    except Exception:
        raise HTTPException(status_code=401, detail="Invalid/expired token")


async def db_conn() -> asyncpg.Connection:
    pool = await get_pool()
    async with pool.acquire() as conn:
        yield conn


async def tenant_guard(
    user=Depends(current_user),
    tenant_id: str | None = Query(None, alias='tenant_id'),
    conn: asyncpg.Connection = Depends(db_conn),
) -> str:
    username = user.get('name')

    if tenant_id:
        if not re.fullmatch(r"[0-9a-fA-F\-]{32,36}", tenant_id):
            raise HTTPException(status_code=403, detail="Invalid tenant id")

        if user.get('permissions') in {"ADMIN", "OWNER"}:
            return tenant_id

        token_tid = user.get('tenant_id')
        if isinstance(token_tid, str) and token_tid == tenant_id:
            return tenant_id

        try:
            row = await conn.fetchrow(
                """
                SELECT t.tenant_owner,
                       (SELECT u.tenant_id::text FROM iot.users u WHERE u.name = $2) AS user_tenant_id
                FROM iot.tenant t
                WHERE t.tenant_id::text = $1
                """,
                tenant_id, username,
            )
        except Exception:
            logger.exception("tenant_guard: DB lookup failed for tenant_id=%s", tenant_id)
            raise HTTPException(status_code=403, detail="Tenant lookup failed")

        if not row:
            raise HTTPException(status_code=403, detail="Tenant not found")

        if row['tenant_owner'] and row['tenant_owner'] == username:
            return tenant_id

        if row['user_tenant_id'] and row['user_tenant_id'] == tenant_id:
            return tenant_id

        raise HTTPException(status_code=403, detail="Not allowed for tenant")

    token_tid = user.get("tenant_id")
    if isinstance(token_tid, str) and re.fullmatch(r"[0-9a-fA-F\-]{32,36}", token_tid):
        return token_tid

    try:
        db_tid = await conn.fetchval(
            "SELECT tenant_id::text FROM iot.users WHERE name = $1",
            username,
        )
    except Exception:
        logger.exception("tenant_guard: failed to look up user tenant for %s", username)
        db_tid = None

    if db_tid and re.fullmatch(r"[0-9a-fA-F\-]{32,36}", db_tid):
        return db_tid

    raise HTTPException(status_code=403, detail="Tenant not found or not set in token")