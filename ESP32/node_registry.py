import os

try:
    import ujson as json
except ImportError:
    import json


REGISTRY_PATH = "node_registry.json"


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


def _default_registry():
    return {"next_node_id": 1, "nodes": {}}


def _normalize_mac(mac):
    if mac is None:
        return ""
    mac = str(mac).strip().upper()
    allowed = "0123456789ABCDEF:-_"
    cleaned = ""
    for ch in mac:
        if ch in allowed:
            cleaned += ch
    return cleaned


def load_registry():
    try:
        with open(REGISTRY_PATH, "r") as f:
            data = json.loads(f.read())
        if not isinstance(data, dict):
            return _default_registry()
        if "nodes" not in data or not isinstance(data["nodes"], dict):
            data["nodes"] = {}
        if "next_node_id" not in data:
            data["next_node_id"] = 1
        data["next_node_id"] = int(data["next_node_id"])
        return data
    except Exception:
        return _default_registry()


def save_registry(registry):
    tmp_path = REGISTRY_PATH + ".tmp"
    with open(tmp_path, "w") as f:
        f.write(json.dumps(registry))
    _safe_replace(tmp_path, REGISTRY_PATH)


def assign_node_id(mac):
    mac = _normalize_mac(mac)
    if not mac:
        return None

    registry = load_registry()
    nodes = registry["nodes"]

    if mac in nodes:
        return str(nodes[mac])

    node_id = int(registry.get("next_node_id", 1))
    while str(node_id) in set(str(v) for v in nodes.values()) or node_id == 0:
        node_id += 1

    nodes[mac] = str(node_id)
    registry["next_node_id"] = node_id + 1
    save_registry(registry)
    return str(node_id)
