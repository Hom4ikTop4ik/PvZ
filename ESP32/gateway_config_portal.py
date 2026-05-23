import machine
import network
import socket
import time

import config_gateway as config_ga
import gateway_settings


AP_SSID = "PvZ-Gateway"
AP_IP = "192.168.4.1"
MAX_REQUEST_LEN = 4096


def _ap_interface():
    if hasattr(network, "WLAN") and hasattr(network.WLAN, "IF_AP"):
        return network.WLAN(network.WLAN.IF_AP)
    return network.WLAN(network.AP_IF)


def _sta_interface():
    if hasattr(network, "WLAN") and hasattr(network.WLAN, "IF_STA"):
        return network.WLAN(network.WLAN.IF_STA)
    return network.WLAN(network.STA_IF)


def _url_decode(value):
    value = value.replace("+", " ")
    result = ""
    i = 0

    while i < len(value):
        if value[i] == "%" and i + 2 < len(value):
            try:
                result += chr(int(value[i + 1:i + 3], 16))
                i += 3
                continue
            except ValueError:
                pass
        result += value[i]
        i += 1

    return result


def _parse_form(body):
    data = {}
    for pair in body.split("&"):
        if not pair:
            continue
        if "=" in pair:
            key, value = pair.split("=", 1)
        else:
            key, value = pair, ""
        data[_url_decode(key)] = _url_decode(value)
    return data


def _split_path_query(path):
    if "?" not in path:
        return path, {}
    route, query = path.split("?", 1)
    return route, _parse_form(query)


def _html_page(current_tenant, message=""):
    return """<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PvZ Gateway</title>
</head>
<body>
<h2>PvZ Gateway</h2>
<p>{message}</p>
<form method="POST" action="/save">
<p>Tenant:</p>
<input name="tenant" value="{tenant}" maxlength="48">
<p><button type="submit">Save</button></p>
</form>
<form method="POST" action="/reboot">
<p><button type="submit">Reboot gateway</button></p>
</form>
<p>Allowed: A-Z a-z 0-9 _ - .</p>
<p><a href="/status">Status</a> | <a href="/ping">Ping</a></p>
</body>
</html>
""".format(tenant=current_tenant, message=message)


def _write_all(conn, data):
    sent = 0
    while sent < len(data):
        n = conn.send(data[sent:sent + 256])
        if not n:
            raise OSError("socket send returned 0")
        sent += n
        time.sleep_ms(5)
    return sent


def _send_response(conn, status, content, content_type="text/html"):
    if isinstance(content, str):
        content = content.encode("utf-8")

    header = (
        "HTTP/1.0 {}\r\n"
        "Content-Type: {}; charset=utf-8\r\n"
        "Content-Length: {}\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).format(status, content_type, len(content)).encode("utf-8")

    sent = _write_all(conn, header + content)
    print("[CONFIG] HTTP response:", status, sent, "bytes")


def _send_empty(conn, status):
    header = (
        "HTTP/1.0 {}\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).format(status).encode("utf-8")
    _write_all(conn, header)


def _read_request(conn):
    raw = b""

    while b"\r\n\r\n" not in raw and len(raw) < MAX_REQUEST_LEN:
        chunk = conn.recv(512)
        if not chunk:
            break
        raw += chunk

    if not raw:
        return "", "", ""

    header_raw, _, body_raw = raw.partition(b"\r\n\r\n")
    header = header_raw.decode("utf-8", "ignore")
    body = body_raw.decode("utf-8", "ignore")
    first = header.split("\r\n", 1)[0]
    parts = first.split()
    method = parts[0] if len(parts) >= 1 else "GET"
    path = parts[1] if len(parts) >= 2 else "/"
    if not path:
        path = "/"

    content_length = 0
    for line in header.split("\r\n")[1:]:
        if line.lower().startswith("content-length:"):
            try:
                content_length = int(line.split(":", 1)[1].strip())
            except ValueError:
                content_length = 0

    while len(body.encode("utf-8")) < content_length and len(raw) < MAX_REQUEST_LEN:
        chunk = conn.recv(512)
        if not chunk:
            break
        raw += chunk
        body += chunk.decode("utf-8", "ignore")

    return method, path, body


def _set_ap_name(ap):
    attempts = (
        ("ssid", {"ssid": AP_SSID}),
        ("essid", {"essid": AP_SSID}),
    )

    for label, kwargs in attempts:
        try:
            ap.config(**kwargs)
            return True
        except Exception:
            pass

    return False


def _set_ap_ip(ap):
    try:
        ap.ifconfig((AP_IP, "255.255.255.0", AP_IP, AP_IP))
        return True
    except Exception:
        return False


def _start_ap():
    try:
        sta = _sta_interface()
        sta.active(False)
    except Exception:
        pass

    ap = _ap_interface()

    try:
        ap.active(False)
        time.sleep_ms(200)
    except Exception:
        pass

    _set_ap_name(ap)

    try:
        ap.active(True)
    except Exception as e:
        print("[CONFIG] AP activate error:", e)
        raise

    time.sleep(1)
    name_ok = _set_ap_name(ap)
    ip_ok = _set_ap_ip(ap)

    print("[CONFIG] AP name set:", name_ok)
    print("[CONFIG] AP IP set:", ip_ok)
    print("[CONFIG] AP active:", ap.active())
    print("[CONFIG] AP ifconfig:", ap.ifconfig())
    return ap


def run_config_portal(current_tenant):
    ap = _start_ap()
    ap_ip = ap.ifconfig()[0]
    tenant = current_tenant
    print("[CONFIG] AP started:", AP_SSID, ap.ifconfig())
    print("[CONFIG] AP is open, no password")
    print("[CONFIG] Open http://{}/".format(ap_ip))

    addr = socket.getaddrinfo("0.0.0.0", 80)[0][-1]
    server = socket.socket()
    try:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    except Exception:
        pass
    server.bind(addr)
    server.listen(1)
    print("[CONFIG] HTTP server listening on port 80")

    while True:
        conn, remote = server.accept()
        try:
            conn.settimeout(5)
        except Exception:
            pass

        print("[CONFIG] HTTP client connected:", remote)
        try:
            method, path, body = _read_request(conn)
            print("[CONFIG] HTTP request:", method, path)
            route, query = _split_path_query(path)

            if route.startswith("/favicon.ico"):
                _send_empty(conn, "204 No Content")
            elif route.startswith("/ping"):
                _send_response(conn, "200 OK", "OK\n", "text/plain")
            elif route.startswith("/status"):
                text = "tenant={}\nip={}\n".format(tenant, ap_ip)
                _send_response(conn, "200 OK", text, "text/plain")
            elif method == "GET" and route.startswith("/set"):
                tenant = gateway_settings.save_tenant(query.get("tenant", ""), config_ga.TENANT)
                _send_response(conn, "200 OK", "saved tenant={}\n".format(tenant), "text/plain")
                print("[CONFIG] Tenant saved:", tenant)
            elif method == "POST" and route.startswith("/save"):
                form = _parse_form(body)
                tenant = gateway_settings.save_tenant(form.get("tenant", ""), config_ga.TENANT)
                _send_response(conn, "200 OK", _html_page(tenant, "Saved. Reboot when ready."))
                print("[CONFIG] Tenant saved:", tenant)
            elif method == "POST" and route.startswith("/reboot"):
                _send_response(conn, "200 OK", "Rebooting gateway...\n", "text/plain")
                time.sleep_ms(500)
                conn.close()
                time.sleep(1)
                machine.reset()
            else:
                _send_response(conn, "200 OK", _html_page(tenant))

            time.sleep_ms(500)
        except Exception as e:
            print("[CONFIG] request error:", e)
            try:
                _send_response(conn, "500 Internal Server Error", "Internal error\n", "text/plain")
                time.sleep_ms(500)
            except Exception:
                pass
        finally:
            try:
                conn.close()
            except Exception:
                pass
