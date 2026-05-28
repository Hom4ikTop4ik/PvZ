<template>
  <div class="management-page">
    <section class="card p-24 toolbar-card">
      <div class="toolbar-head">
        <div>
          <h2 class="section-title" style="margin-bottom: 6px;">Управление устройствами</h2>
          <div class="helper">
            Здесь можно искать устройства, открывать карточки и добавлять новые датчики.
          </div>
        </div>

        <div class="toolbar-actions">
          <button class="btn" type="button" @click="loadAllDevices" :disabled="loading">
            {{ loading ? 'Обновляем...' : 'Обновить список' }}
          </button>

          <button class="btn primary" type="button" @click="handleAddDevice">
            Добавить устройство
          </button>
        </div>
      </div>

      <div class="toolbar-grid">
        <input
          v-model="search"
          class="input"
          placeholder="Поиск по имени, tenant или device_id"
        />

        <select v-model="tenantFilter" class="input">
          <option value="">Все тенанты</option>
          <option
            v-for="tenant in tenants"
            :key="tenant.tenant_id"
            :value="tenant.tenant_id"
          >
            {{ tenant.name }}
          </option>
        </select>
      </div>
    </section>

    <section v-if="loading" class="card p-24 state-card">
      <h3 style="margin: 0 0 8px;">Загрузка</h3>
      <div class="helper">Получаем список устройств...</div>
    </section>

    <section v-else-if="error" class="card p-24 state-card">
      <h3 style="margin: 0 0 8px;">Не удалось открыть управление</h3>
      <div class="helper" style="color: #b00020; margin-bottom: 16px;">
        {{ error }}
      </div>

      <button class="btn" type="button" @click="loadAllDevices">
        Попробовать снова
      </button>
    </section>

    <section v-else-if="filteredDevices.length" class="devices-grid">
      <article
        v-for="device in filteredDevices"
        :key="device.device_id"
        class="card device-card"
        :style="{ '--tenant-accent': getTenantAccent(device.tenant_id || device.tenant_name || 'default') }"
      >
        <div class="device-card__top">
          <div>
            <div class="device-card__eyebrow">
              {{ device.tenant_name || 'Тенант' }}
            </div>

            <h3 class="device-card__title">
              {{ getDisplayDeviceName(device) }}
            </h3>

            <div class="helper">Системное имя: {{ getBackendDeviceName(device) }}</div>
          </div>

          <span class="status-pill" :class="getPowerState(device) ? 'online' : 'offline'">
            {{ getPowerStateLabel(device) }}
          </span>
        </div>

        <div class="device-card__meta">
          <div class="meta-box">
            <span class="meta-label">Заряд</span>
            <strong>{{ formatBattery(device.battery ?? device.battery_level) }}</strong>
          </div>

          <div class="meta-box">
            <span class="meta-label">Местоположение</span>
            <strong>{{ formatCoords(device) }}</strong>
          </div>
        </div>

        <div v-if="isBatteryLow(device.battery ?? device.battery_level)" class="warning-chip">
          Низкий заряд устройства
        </div>

        <router-link
          class="btn primary device-card__action"
          :to="deviceLink(device)"
        >
          Открыть карточку
        </router-link>
      </article>
    </section>

    <section v-else class="card p-24 state-card">
      <h3 style="margin: 0 0 8px;">Устройств пока нет</h3>
      <div class="helper" style="margin-bottom: 16px;">
        Когда появятся устройства, они отобразятся здесь.
      </div>

      <button class="btn primary" type="button" @click="handleAddDevice">
        Добавить устройство
      </button>
    </section>
  </div>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { api } from '../api'
import {
  formatBattery,
  getBackendDeviceName,
  getDisplayDeviceName,
  getPowerState,
  getPowerStateLabel,
  getTenantAccent,
  isBatteryLow,
  matchesDeviceSearch,
} from '../devicePresentation'

const tenants = ref([])
const devices = ref([])
const loading = ref(false)
const error = ref('')
const search = ref('')
const tenantFilter = ref('')

function normalizeRows(payload) {
  if (Array.isArray(payload)) return payload
  if (Array.isArray(payload?.items)) return payload.items
  if (Array.isArray(payload?.data)) return payload.data
  if (Array.isArray(payload?.devices)) return payload.devices
  if (Array.isArray(payload?.results)) return payload.results
  return []
}

function normalizeTenant(raw) {
  return {
    tenant_id: String(raw?.tenant_id ?? raw?.id ?? raw?.name ?? ''),
    name: String(raw?.name ?? raw?.tenant_name ?? raw?.tenant_id ?? raw?.id ?? 'Тенант'),
  }
}

function normalizeDevice(raw, tenant = null) {
  return {
    ...raw,
    device_id: String(raw?.device_id ?? raw?.id ?? raw?.external_id ?? raw?.name ?? ''),
    tenant_id: String(
      raw?.tenant_id ??
      raw?.tenant?.tenant_id ??
      raw?.tenant?.id ??
      tenant?.tenant_id ??
      ''
    ),
    tenant_name: String(
      raw?.tenant_name ??
      raw?.tenant?.name ??
      tenant?.name ??
      'Тенант'
    ),
  }
}

function devicesPath(tenantIdValue = '') {
  const query = tenantIdValue ? `?tenant_id=${encodeURIComponent(tenantIdValue)}` : ''
  return `/devices/${query}`
}

function formatCoords(device) {
  if (device.lat == null || device.lon == null) return 'n/a'
  return `${Number(device.lat).toFixed(4)}, ${Number(device.lon).toFixed(4)}`
}

function deviceLink(device) {
  const query = device.tenant_id
    ? `?tenant_id=${encodeURIComponent(device.tenant_id)}`
    : ''
  return `/management/${encodeURIComponent(device.device_id)}${query}`
}

function handleAddDevice() {
  alert('Форму добавления устройства подключим следующим шагом')
}

const filteredDevices = computed(() => {
  return devices.value.filter((device) => {
    const matchesTenant =
      !tenantFilter.value || String(device.tenant_id) === String(tenantFilter.value)

    return matchesTenant && matchesDeviceSearch(device, search.value)
  })
})

async function loadAllDevices() {
  loading.value = true
  error.value = ''

  try {
    const tenantsResp = await api.get('/tenants')
    const tenantRows = normalizeRows(tenantsResp).map(normalizeTenant)
    tenants.value = tenantRows

    const collected = []

    for (const tenant of tenantRows) {
      try {
        const resp = await api.get(devicesPath(tenant.tenant_id))
        const rows = normalizeRows(resp).map((device) => normalizeDevice(device, tenant))
        collected.push(...rows)
      } catch (e) {
        console.warn('Не удалось загрузить устройства tenant', tenant.tenant_id, e)
      }
    }

    if (!collected.length) {
      try {
        const fallbackResp = await api.get('/devices/')
        const fallbackRows = normalizeRows(fallbackResp).map((device) => normalizeDevice(device))
        collected.push(...fallbackRows)
      } catch (e) {
        console.warn('Fallback /devices/ не сработал', e)
      }
    }

    const unique = new Map()
    for (const device of collected) {
      if (!device.device_id) continue
      unique.set(device.device_id, device)
    }

    devices.value = Array.from(unique.values())
  } catch (e) {
    error.value = e?.body?.detail || e?.message || 'Не удалось загрузить страницу управления'
  } finally {
    loading.value = false
  }
}

onMounted(() => {
  loadAllDevices().catch((e) => {
    console.error('ManagementView crashed during load', e)
    error.value = e?.message || 'Ошибка при загрузке управления'
    loading.value = false
  })
})
</script>

<style scoped>
.management-page {
  max-width: 1180px;
  margin: 24px auto;
  display: grid;
  gap: 18px;
}

.toolbar-card,
.state-card {
  width: 100%;
}

.toolbar-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 16px;
  flex-wrap: wrap;
}

.toolbar-actions {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.toolbar-grid {
  margin-top: 18px;
  display: grid;
  gap: 14px;
  grid-template-columns: minmax(0, 1.4fr) minmax(220px, 0.6fr);
}

.devices-grid {
  width: 100%;
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
  gap: 18px;
}

.device-card {
  padding: 20px;
  border-radius: 24px;
  border: 2px solid var(--tenant-accent);
  box-shadow: 0 18px 38px rgba(15, 23, 42, 0.08);
}

.device-card__top {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
  gap: 12px;
}

.device-card__eyebrow {
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--tenant-accent);
  font-weight: 700;
  margin-bottom: 6px;
}

.device-card__title {
  margin: 0 0 6px;
  font-size: 22px;
  line-height: 1.2;
}

.status-pill {
  padding: 8px 12px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 700;
  white-space: nowrap;
}

.status-pill.online {
  background: #dcfce7;
  color: #15803d;
}

.status-pill.offline {
  background: #fee2e2;
  color: #b91c1c;
}

.device-card__meta {
  margin-top: 16px;
  display: grid;
  gap: 10px;
  grid-template-columns: repeat(2, minmax(0, 1fr));
}

.meta-box {
  padding: 14px;
  border-radius: 18px;
  background: #f8fbff;
  border: 1px solid rgba(148, 163, 184, 0.18);
}

.meta-label {
  display: block;
  font-size: 12px;
  color: #64748b;
  margin-bottom: 6px;
}

.warning-chip {
  margin-top: 14px;
  padding: 10px 12px;
  border-radius: 14px;
  background: #fff7ed;
  color: #c2410c;
  border: 1px solid #fdba74;
  font-weight: 600;
}

.device-card__action {
  margin-top: 16px;
  width: 100%;
}

@media (max-width: 720px) {
  .toolbar-grid,
  .device-card__meta {
    grid-template-columns: 1fr;
  }
}
</style>

