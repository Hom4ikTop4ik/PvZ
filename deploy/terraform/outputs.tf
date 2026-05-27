output "mqtt_lb_ip" {
  value = twc_floating_ip.mqtt_lb.ip
}

output "mqtt_1_ip" {
  value = twc_server.mqtt_1.main_ipv4
}

output "mqtt_2_ip" {
  value = twc_server.mqtt_2.main_ipv4
}

output "postgres_host" {
  value     = twc_database_cluster.postgres.local_ip
  sensitive = true
}

output "vpc_id" {
  value = twc_vpc.pvz_private.id
}
