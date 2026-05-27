variable "twc_token" {
  type      = string
  sensitive = true
}

variable "ssh_key_name" {
  type    = string
  default = "pvz-deploy"
}

variable "availability_zone" {
  type    = string
  default = "ru-1"
}

variable "mqtt_password" {
  type      = string
  sensitive = true
}

variable "db_password" {
  type      = string
  sensitive = true
}
