import asyncio
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
import uvicorn
from .routers import (
    auth as r_auth,
    devices as r_devices,
    map as r_map,
    charts as r_charts,
    tenants as r_tenants,
    notifications as r_notifications,
    metrics as r_metrics,
)
from .instrumentation import prometheus_middleware
from .mqtt_runtime import run_mqtt_forever
from .services.config import settings
import logging

logger = logging.getLogger(__name__)

app = FastAPI(title="IoT Backend", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.allow_origin,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.middleware("http")(prometheus_middleware)

app.include_router(r_auth.router)
app.include_router(r_devices.router)
app.include_router(r_map.router)
app.include_router(r_charts.router)
app.include_router(r_tenants.router)
app.include_router(r_notifications.router)
app.include_router(r_metrics.router)


@app.on_event("startup")
async def _startup():
    task = asyncio.create_task(run_mqtt_forever(), name="mqtt_runtime")
    app.state.mqtt_task = task

    def _done(t: asyncio.Task):
        try:
            t.result()
        except asyncio.CancelledError:
            logger.info("MQTT task cancelled")
        except Exception:
            logger.exception("MQTT task crashed")

    task.add_done_callback(_done)
    logger.info("MQTT task started")


@app.on_event("shutdown")
async def _shutdown():
    app.state.mqtt_task.cancel()


@app.get("/")
async def root():
    return {"message": "IoT Backend is running"}


@app.get("/health")
async def health():
    return {"status": "ok"}


@app.get("/ready")
async def ready():
    return {"status": "ready"}


if __name__ == "__main__":
    uvicorn.run("app.main:app", host="0.0.0.0", port=443, reload=True)