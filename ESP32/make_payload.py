from datetime import datetime, timezone

def now_iso() -> str:
    """Возвращает текущее время в формате ISO 8601."""
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

def make_humidity_payload(
    device_id: str,
    humidity: float,
    seq: int,
    timestamp: str = None
) -> str:
    if timestamp is None:
        timestamp = now_iso()
    return f"{device_id},{timestamp},{humidity:.2f},{seq}"

def make_temperature_payload(
    device_id: str,
    temperature: float,
    seq: int,
    timestamp: str = None
) -> str:
    if timestamp is None:
        timestamp = now_iso()
    return f"{device_id},{timestamp},{temperature:.2f},{seq}"

def make_location_payload(
    device_id: str,
    latitude: float,
    longitude: float,
    timestamp: str = None
) -> str:
    if timestamp is None:
        timestamp = now_iso()
    return f"{device_id},{timestamp},{latitude:.6f},{longitude:.6f}"

def make_ack_payload(
    command_id: str,
    status: str,
    details: str = "",
    timestamp: str = None
) -> str:
    if timestamp is None:
        timestamp = now_iso()
    return f"{command_id},{timestamp},{status},{details}"
