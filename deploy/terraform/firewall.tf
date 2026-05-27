resource "twc_firewall" "mqtt_fw" {
  name = "pvz-mqtt-fw"
}

resource "twc_firewall_rules" "mqtt_rules" {
  firewall_id = twc_firewall.mqtt_fw.id

  rule {
    direction = "ingress"
    protocol  = "tcp"
    port      = "22"
    cidr      = "0.0.0.0/0"
    action    = "accept"
  }

  rule {
    direction = "ingress"
    protocol  = "tcp"
    port      = "1883"
    cidr      = "192.168.10.0/24"
    action    = "accept"
  }

  rule {
    direction = "ingress"
    protocol  = "tcp"
    port      = "8883"
    cidr      = "192.168.10.0/24"
    action    = "accept"
  }

  rule {
    direction = "ingress"
    protocol  = "any"
    action    = "drop"
  }
}
