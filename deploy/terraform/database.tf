resource "twc_database_cluster" "postgres" {
  name              = "pvz-postgres"
  type              = "postgresql"
  version           = "16"
  preset_id         = 1
  availability_zone = var.availability_zone
  project_id        = twc_project.pvz.id

  local_network {
    id = twc_vpc.pvz_private.id
  }
}

resource "twc_database_instance" "pvzdb" {
  cluster_id = twc_database_cluster.postgres.id
  name       = "pvzdb"
}

resource "twc_database_user" "pvz" {
  cluster_id = twc_database_cluster.postgres.id
  name       = "pvz"
  password   = var.db_password
}
