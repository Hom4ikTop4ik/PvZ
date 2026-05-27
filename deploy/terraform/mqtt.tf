data "twc_configurator" "mqtt" {
  location  = var.availability_zone
  disk_type = "nvme"
}

resource "twc_server" "mqtt_1" {
  name              = "pvz-mqtt-1"
  os_id             = 76
  availability_zone = var.availability_zone
  project_id        = twc_project.pvz.id

  configurator {
    configurator_id = data.twc_configurator.mqtt.id
    disk            = 15360
    cpu             = 1
    ram             = 1024
  }

  ssh_keys_ids = [local.ssh_key_id]

  local_network {
    id = twc_vpc.pvz_private.id
  }
}

resource "twc_server" "mqtt_2" {
  name              = "pvz-mqtt-2"
  os_id             = 76
  availability_zone = var.availability_zone
  project_id        = twc_project.pvz.id

  configurator {
    configurator_id = data.twc_configurator.mqtt.id
    disk            = 15360
    cpu             = 1
    ram             = 1024
  }

  ssh_keys_ids = [local.ssh_key_id]

  local_network {
    id = twc_vpc.pvz_private.id
  }
}

resource "twc_floating_ip" "mqtt_lb" {
  availability_zone = var.availability_zone
}

resource "twc_lb" "mqtt" {
  name              = "pvz-mqtt-lb"
  preset_id         = 1
  availability_zone = var.availability_zone
  project_id        = twc_project.pvz.id
  floating_ip_id    = twc_floating_ip.mqtt_lb.id
  algo              = "roundrobin"
  is_sticky         = false

  ips = [
    twc_server.mqtt_1.main_ipv4,
    twc_server.mqtt_2.main_ipv4,
  ]

  health_check {
    type                = "tcp"
    port                = 1883
    interval            = 10
    timeout             = 5
    unhealthy_threshold = 2
    healthy_threshold   = 2
  }

  local_network {
    id = twc_vpc.pvz_private.id
  }
}

resource "twc_lb_rule" "mqtt_1883" {
  balancer_id    = twc_lb.mqtt.id
  balancer_proto = "tcp"
  balancer_port  = 1883
  server_proto   = "tcp"
  server_port    = 1883
}

resource "twc_lb_rule" "mqtt_8883" {
  balancer_id    = twc_lb.mqtt.id
  balancer_proto = "tcp"
  balancer_port  = 8883
  server_proto   = "tcp"
  server_port    = 8883
}
