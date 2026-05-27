resource "twc_vpc" "pvz_private" {
  name              = "pvz-private-net"
  subnet_v4         = "192.168.10.0/24"
  availability_zone = var.availability_zone
}
