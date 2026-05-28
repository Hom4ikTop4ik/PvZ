from pydantic import BaseModel, Field
from datetime import datetime


class MetricRule(BaseModel):
    enabled: bool = True
    min: float | None = None
    max: float | None = None


class NotificationPrefIn(BaseModel):
    master_enabled: bool = True
    rules: dict[str, MetricRule] = Field(default_factory=dict)


class NotificationPrefOut(NotificationPrefIn):
    device_id: str
    updated_at: datetime | None = None


class LatestReading(BaseModel):
    device_id: str
    external_id: str | None = None
    temperature: float | None = None
    humidity: float | None = None
    battery: float | None = None
    last_seen: datetime | None = None
