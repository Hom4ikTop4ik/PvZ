terraform {
  required_providers {
    twc = {
      source  = "timeweb-cloud/timeweb-cloud"
      version = "~> 1.6"
    }
  }
  required_version = ">= 1.6"
}

provider "twc" {
  token = var.twc_token
}

resource "twc_project" "pvz" {
  name        = "pvz-1"
  description = "IoT platform PvZ-1"
}

data "twc_ssh_keys" "deploy" {}

locals {
  ssh_key_id = [
    for k in data.twc_ssh_keys.deploy.ssh_keys :
    k.id if k.name == var.ssh_key_name
  ][0]
}
