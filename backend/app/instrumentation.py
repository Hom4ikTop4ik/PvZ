import time
from fastapi import Request
from prometheus_client import (
    Counter, Histogram, Gauge,
    REGISTRY, CONTENT_TYPE_LATEST, generate_latest,
)

HTTP_REQUESTS_TOTAL = Counter(
    "http_requests_total",
    "Total HTTP requests",
    ["method", "route", "status_code"],
)

HTTP_REQUEST_DURATION_SECONDS = Histogram(
    "http_request_duration_seconds",
    "HTTP request latency",
    ["method", "route"],
    buckets=[0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0],
)

HTTP_REQUESTS_IN_FLIGHT = Gauge(
    "http_requests_in_flight",
    "HTTP requests currently being processed",
)

MQTT_MESSAGES_RECEIVED = Counter(
    "mqtt_messages_received_total",
    "MQTT messages received by leaf type",
    ["env", "leaf"],
)

MQTT_MESSAGES_ERRORS = Counter(
    "mqtt_messages_errors_total",
    "MQTT messages that failed processing",
    ["env", "leaf"],
)

MQTT_COMMANDS_PUBLISHED = Counter(
    "mqtt_commands_published_total",
    "MQTT commands published to devices",
    ["type"],
)

MQTT_CONNECTED = Gauge(
    "mqtt_broker_connected",
    "1 if backend is connected to MQTT broker",
)


async def prometheus_middleware(request: Request, call_next):
    route = request.scope.get("route")
    path = route.path if route else request.url.path

    skip = path in ("/metrics", "/health", "/ready", "/")
    if skip:
        return await call_next(request)

    HTTP_REQUESTS_IN_FLIGHT.inc()
    start = time.perf_counter()
    try:
        response = await call_next(request)
        status = str(response.status_code)
    except Exception:
        status = "500"
        raise
    finally:
        elapsed = time.perf_counter() - start
        HTTP_REQUESTS_IN_FLIGHT.dec()
        HTTP_REQUESTS_TOTAL.labels(
            method=request.method,
            route=path,
            status_code=status,
        ).inc()
        HTTP_REQUEST_DURATION_SECONDS.labels(
            method=request.method,
            route=path,
        ).observe(elapsed)

    return response


def prometheus_text_response():
    from fastapi import Response
    return Response(
        content=generate_latest(REGISTRY),
        media_type=CONTENT_TYPE_LATEST,
    )