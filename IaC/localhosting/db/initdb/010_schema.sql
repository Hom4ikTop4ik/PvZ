CREATE SCHEMA IF NOT EXISTS iot;

DO $$
BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_type WHERE typname = 'permission_level') THEN
    CREATE TYPE iot.permission_level AS ENUM ('OWNER','ADMIN','OPERATOR','VIEWER');
  END IF;
END$$;

CREATE OR REPLACE FUNCTION iot.tg_set_updated_at()
RETURNS trigger LANGUAGE plpgsql AS $$
BEGIN
  NEW.updated_at := now();
  RETURN NEW;
END$$;
CREATE TABLE IF NOT EXISTS iot.tenant (
  tenant_id    uuid PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_name  text NOT NULL UNIQUE,
  tenant_owner text,
  created_at   timestamptz NOT NULL DEFAULT now(),
  updated_at   timestamptz NOT NULL DEFAULT now()
);

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_trigger WHERE tgname = 'tr_tenant_updated'
  ) THEN
    CREATE TRIGGER tr_tenant_updated
    BEFORE UPDATE ON iot.tenant
    FOR EACH ROW EXECUTE FUNCTION iot.tg_set_updated_at();
  END IF;
END$$;

CREATE TABLE IF NOT EXISTS iot.users (
  user_id      uuid PRIMARY KEY DEFAULT uuid_generate_v4(),
  name         varchar(120) NOT NULL,
  email        text,
  password     text NOT NULL,
  permissions  iot.permission_level NOT NULL DEFAULT 'VIEWER',
  tenant_id    uuid REFERENCES iot.tenant(tenant_id) ON DELETE SET NULL,
  created_at   timestamptz NOT NULL DEFAULT now(),
  updated_at   timestamptz NOT NULL DEFAULT now()
);

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_trigger WHERE tgname = 'tr_users_updated'
  ) THEN
    CREATE TRIGGER tr_users_updated
    BEFORE UPDATE ON iot.users
    FOR EACH ROW EXECUTE FUNCTION iot.tg_set_updated_at();
  END IF;
END$$;

CREATE UNIQUE INDEX IF NOT EXISTS users_name_idx
  ON iot.users(name);

CREATE UNIQUE INDEX IF NOT EXISTS users_email_idx
  ON iot.users((email))
  WHERE email IS NOT NULL;

CREATE TABLE IF NOT EXISTS iot.devices (
  device_id        uuid PRIMARY KEY DEFAULT uuid_generate_v4(),
  tenant_id        uuid NOT NULL REFERENCES iot.tenant(tenant_id) ON DELETE CASCADE,

  external_id      text NOT NULL,

  installation_id  uuid,
  model            varchar(64),
  status           text,

  created_at       timestamptz NOT NULL DEFAULT now(),
  updated_at       timestamptz NOT NULL DEFAULT now()
);
ALTER TABLE iot.devices ADD COLUMN IF NOT EXISTS external_id text NOT NULL;
CREATE UNIQUE INDEX IF NOT EXISTS devices_tenant_external_uq
  ON iot.devices (tenant_id, external_id);

CREATE INDEX IF NOT EXISTS devices_tenant_idx
  ON iot.devices(tenant_id);

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_trigger WHERE tgname = 'tr_devices_updated'
  ) THEN
    CREATE TRIGGER tr_devices_updated
    BEFORE UPDATE ON iot.devices
    FOR EACH ROW EXECUTE FUNCTION iot.tg_set_updated_at();
  END IF;
END$$;

CREATE TABLE IF NOT EXISTS iot.state (
  device_id     uuid PRIMARY KEY REFERENCES iot.devices(device_id) ON DELETE CASCADE,
  battery_level double precision,
  status        boolean,
  updated_at    timestamptz NOT NULL DEFAULT now(),
  rssi          integer,
  snr           double precision,
  last_seen     timestamptz
);

CREATE INDEX IF NOT EXISTS state_updated_idx
  ON iot.state(updated_at DESC);

CREATE TABLE IF NOT EXISTS iot.location (
  device_id  uuid PRIMARY KEY REFERENCES iot.devices(device_id) ON DELETE CASCADE,
  location   geography(Point, 4326),
  updated_at timestamptz NOT NULL DEFAULT now(),
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS location_geom_idx
  ON iot.location USING GIST(location);

CREATE TABLE IF NOT EXISTS iot.monitoring_raw (
  device_id   uuid NOT NULL REFERENCES iot.devices(device_id) ON DELETE CASCADE,
  humidity    double precision,
  temperature double precision,
  seq         bigint,
  sent_ts     timestamptz NOT NULL,
  PRIMARY KEY (device_id, sent_ts)
);

ALTER TABLE iot.monitoring_raw
  ADD COLUMN IF NOT EXISTS temperature double precision;

ALTER TABLE iot.monitoring_raw
  ALTER COLUMN humidity DROP NOT NULL;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_constraint WHERE conname = 'monitoring_raw_any_metric_ck'
  ) THEN
    ALTER TABLE iot.monitoring_raw
      ADD CONSTRAINT monitoring_raw_any_metric_ck
      CHECK (humidity IS NOT NULL OR temperature IS NOT NULL);
  END IF;
END$$;

SELECT create_hypertable('iot.monitoring_raw', 'sent_ts', if_not_exists => TRUE);

CREATE INDEX IF NOT EXISTS monitoring_raw_ts_idx
  ON iot.monitoring_raw (sent_ts DESC);

CREATE INDEX IF NOT EXISTS monitoring_raw_device_ts_idx
  ON iot.monitoring_raw (device_id, sent_ts DESC);
DROP MATERIALIZED VIEW IF EXISTS iot.monitoring_nm;
DROP VIEW IF EXISTS iot.monitoring_nm;
CREATE VIEW iot.monitoring_nm AS
SELECT
  device_id,
  time_bucket('5 minutes', sent_ts) AS bucket,
  avg(humidity)    AS humidity_avg,
  avg(temperature) AS temperature_avg
FROM iot.monitoring_raw
GROUP BY device_id, bucket;

CREATE TABLE IF NOT EXISTS iot.device_commands (
  cmd_id     uuid PRIMARY KEY DEFAULT uuid_generate_v4(),
  device_id  uuid NOT NULL REFERENCES iot.devices(device_id) ON DELETE CASCADE,
  issued_ts  timestamptz NOT NULL DEFAULT now(),
  sent_ts    timestamptz,
  ack_ts     timestamptz,
  type       text NOT NULL,
  params     jsonb NOT NULL DEFAULT '{}'::jsonb,
  retain     boolean NOT NULL DEFAULT FALSE,
  status     text,
  error      text,
  created_at timestamptz NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS device_commands_device_idx
  ON iot.device_commands(device_id);

CREATE INDEX IF NOT EXISTS device_commands_issued_idx
  ON iot.device_commands(issued_ts DESC);

CREATE OR REPLACE VIEW iot.device_overview AS
SELECT
  d.device_id,
  d.external_id,
  d.tenant_id,
  d.model,
  d.status AS device_status,
  s.battery_level,
  s.status AS online,
  s.rssi,
  s.snr,
  s.last_seen,
  l.location,
  l.updated_at AS location_updated_at
FROM iot.devices d
LEFT JOIN iot.state s    ON s.device_id = d.device_id
LEFT JOIN iot.location l ON l.device_id = d.device_id;

CREATE TABLE IF NOT EXISTS iot.user_notification_prefs (
  user_id        uuid NOT NULL REFERENCES iot.users(user_id) ON DELETE CASCADE,
  device_id      uuid NOT NULL REFERENCES iot.devices(device_id) ON DELETE CASCADE,
  master_enabled boolean NOT NULL DEFAULT true,
  rules          jsonb NOT NULL DEFAULT '{}'::jsonb,
  updated_at     timestamptz NOT NULL DEFAULT now(),
  PRIMARY KEY (user_id, device_id)
);

CREATE INDEX IF NOT EXISTS user_notification_prefs_user_idx
  ON iot.user_notification_prefs(user_id);

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT 1 FROM pg_trigger WHERE tgname = 'tr_user_notification_prefs_updated'
  ) THEN
    CREATE TRIGGER tr_user_notification_prefs_updated
    BEFORE UPDATE ON iot.user_notification_prefs
    FOR EACH ROW EXECUTE FUNCTION iot.tg_set_updated_at();
  END IF;
END$$;

DROP INDEX IF EXISTS iot.devices_external_id_uq;
