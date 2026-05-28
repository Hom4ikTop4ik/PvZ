import asyncpg
from passlib.context import CryptContext
from ..services.config import settings


pwd_context = CryptContext(schemes=["pbkdf2_sha256"], deprecated="auto")


async def get_user_by_username(conn: asyncpg.Connection, *, username: str) -> asyncpg.Record | None:
    query = "SELECT * FROM iot.users WHERE name = $1"
    return await conn.fetchrow(query, username)

async def get_user_by_email(conn: asyncpg.Connection, *, email: str) -> asyncpg.Record | None:
    query = "SELECT * FROM iot.users WHERE email = $1"
    return await conn.fetchrow(query, email)

async def create_user(conn: asyncpg.Connection, *, username: str, email: str | None, hashed_password: str) -> asyncpg.Record:
    tenant_name = settings.app_tenant
    tenant_id = await conn.fetchval("SELECT tenant_id::text FROM iot.tenant WHERE tenant_name = $1", tenant_name)
    if tenant_id is None:
        tenant_id = await conn.fetchval(
            "INSERT INTO iot.tenant (tenant_name, tenant_owner) VALUES ($1, $2) RETURNING tenant_id::text",
            tenant_name, username,
        )

    query = """
    INSERT INTO iot.users (name, email, password, tenant_id)
    VALUES ($1, $2, $3, $4)
    RETURNING *
    """
    return await conn.fetchrow(query, username, email, hashed_password, tenant_id)

async def verify_user_credentials(conn: asyncpg.Connection, *, username: str | None, email: str | None, password: str) -> asyncpg.Record | None:
    if username:
        user = await get_user_by_username(conn, username=username)
    elif email:
        user = await get_user_by_email(conn, email=email)
    else:
        return None

    if user and pwd_context.verify(password, user['password']):
        return user
    return None

