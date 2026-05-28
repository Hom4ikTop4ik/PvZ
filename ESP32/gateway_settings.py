import os

try:
    import ujson as json
except ImportError:
    import json


SETTINGS_PATH = "gateway_settings.json"
NO_WIFI_SSID = "NO_SSID"
NO_WIFI_PASS = "NO_PASSWORD"


def _safe_replace(src_path, dst_path):
    replace = getattr(os, "replace", None)
    if replace:
        replace(src_path, dst_path)
        return

    try:
        os.remove(dst_path)
    except OSError:
        pass
    os.rename(src_path, dst_path)


def _valid_tenant(value):
    if value is None:
        return False
    if not isinstance(value, str):
        return False
    if not value or len(value) > 48:
        return False

    allowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-."
    for ch in value:
        if ch not in allowed:
            return False
    return True


def normalize_tenant(value, default_tenant):
    value = value.strip() if isinstance(value, str) else ""
    if _valid_tenant(value):
        return value
    return default_tenant


def normalize_wifi_ssid(value, default_ssid):
    value = value.strip() if isinstance(value, str) else ""
    if value and len(value.encode("utf-8")) <= 32:
        return value
    return default_ssid


def normalize_wifi_pass(value, default_pass):
    if value is None:
        return default_pass
    if not isinstance(value, str):
        value = str(value)
    if len(value) <= 64:
        return value
    return default_pass


def wifi_is_configured(settings):
    ssid = settings.get("wifi_ssid", "")
    return bool(ssid and ssid != NO_WIFI_SSID)


def _normalize_settings(data, default_tenant, default_wifi_ssid, default_wifi_pass):
    return {
        "tenant": normalize_tenant(data.get("tenant"), default_tenant),
        "wifi_ssid": normalize_wifi_ssid(data.get("wifi_ssid"), default_wifi_ssid),
        "wifi_pass": normalize_wifi_pass(data.get("wifi_pass"), default_wifi_pass),
    }


def load_settings(default_tenant, default_wifi_ssid=NO_WIFI_SSID, default_wifi_pass=NO_WIFI_PASS):
    data = {}
    try:
        with open(SETTINGS_PATH, "r") as f:
            data = json.loads(f.read())
        if not isinstance(data, dict):
            data = {}
    except Exception:
        data = {}

    return _normalize_settings(data, default_tenant, default_wifi_ssid, default_wifi_pass)


def save_settings(tenant, wifi_ssid, wifi_pass, default_tenant, default_wifi_ssid, default_wifi_pass):
    settings = _normalize_settings(
        {
            "tenant": tenant,
            "wifi_ssid": wifi_ssid,
            "wifi_pass": wifi_pass,
        },
        default_tenant,
        default_wifi_ssid,
        default_wifi_pass,
    )

    tmp_path = SETTINGS_PATH + ".tmp"
    with open(tmp_path, "w") as f:
        f.write(json.dumps(settings))

    _safe_replace(tmp_path, SETTINGS_PATH)
    return settings


def save_tenant(tenant, default_tenant):
    settings = load_settings(default_tenant)
    settings["tenant"] = normalize_tenant(tenant, default_tenant)
    return save_settings(
        settings["tenant"],
        settings["wifi_ssid"],
        settings["wifi_pass"],
        default_tenant,
        settings["wifi_ssid"],
        settings["wifi_pass"],
    )["tenant"]



def apply_settings(config_module, settings):
    tenant = settings.get("tenant", config_module.TENANT)
    tenant = normalize_tenant(tenant, config_module.TENANT)
    wifi_ssid = normalize_wifi_ssid(settings.get("wifi_ssid"), config_module.WIFI_SSID)
    wifi_pass = normalize_wifi_pass(settings.get("wifi_pass"), config_module.WIFI_PASS)

    config_module.TENANT = tenant
    config_module.WIFI_SSID = wifi_ssid
    config_module.WIFI_PASS = wifi_pass
    config_module.TOPIC_SUB_COMMANDS = "{}/{}/devices/+/command".format(config_module.ENV, tenant)
    config_module.TOPIC_PUB_PREFIX = "{}/{}/sensors".format(config_module.ENV, tenant)
    return {
        "tenant": tenant,
        "wifi_ssid": wifi_ssid,
        "wifi_pass": wifi_pass,
    }
