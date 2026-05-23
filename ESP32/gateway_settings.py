import os

try:
    import ujson as json
except ImportError:
    import json


SETTINGS_PATH = "gateway_settings.json"


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


def load_settings(default_tenant):
    tenant = default_tenant

    try:
        with open(SETTINGS_PATH, "r") as f:
            data = json.loads(f.read())
        tenant = normalize_tenant(data.get("tenant"), default_tenant)
    except Exception:
        tenant = default_tenant

    return {"tenant": tenant}


def save_tenant(tenant, default_tenant):
    tenant = normalize_tenant(tenant, default_tenant)
    tmp_path = SETTINGS_PATH + ".tmp"
    content = json.dumps({"tenant": tenant})

    with open(tmp_path, "w") as f:
        f.write(content)

    _safe_replace(tmp_path, SETTINGS_PATH)
    return tenant


def apply_settings(config_module, settings):
    tenant = settings.get("tenant", config_module.TENANT)
    tenant = normalize_tenant(tenant, config_module.TENANT)

    config_module.TENANT = tenant
    config_module.TOPIC_SUB_COMMANDS = "{}/{}/devices/+/command".format(config_module.ENV, tenant)
    config_module.TOPIC_PUB_PREFIX = "{}/{}/sensors".format(config_module.ENV, tenant)
    return tenant
