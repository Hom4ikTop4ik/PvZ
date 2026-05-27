<template>
  <div class="device-page" v-if="device">
    <section
      class="card p-24 device-shell"
      :style="{ '--tenant-accent': getTenantAccent(device.tenant_id || device.tenant_name || 'default') }"
    >
      <div class="device-shell__head">
        <div>
          <div class="device-shell__eyebrow">{{ device.tenant_name || 'Тенант' }}</div>
          <h2 class="section-title device-shell__title">{{ displayName }}</h2>
        </div>

        <router-link to="/management" class="btn">К списку устройств</router-link>
      </div>

      <div class="device-shell__grid">
        <div class="info-box">
          <span class="info-label">Имя устройства</span>
          <strong>{{ displayName }}</strong>
        </div>

        <div class="info-box">
          <span class="info-label">Заряд</span>
          <strong>{{ formatBattery(device.battery ?? device.battery_level) }}</strong>
          <div
            v-if="isBatteryLow(device.battery ?? device.battery_level)"
            class="warning-chip mt-10"
          >
            Низкий заряд, стоит проверить устройство
          </div>
        </div>

        <div class="info-box">
          <span class="info-label">Местоположение</span>
          <strong>{{ coordsText }}</strong>
        </div>

        <div class="info-box">
          <span class="info-label">Тенант</span>
          <strong>{{ device.tenant_name || 'Тенант' }}</strong>
        </div>

        <div class="info-box">
          <span class="info-label">Статус устройства</span>
          <strong>{{ getPowerStateLabel(device) }}</strong>
        </div>

        <div class="info-box">
          <span class="info-label">Последнее обновление координат</span>
          <strong>{{ locationUpdatedAt }}</strong>
        </div>
      </div>
    </section>

    <section class="device-layout">
      <div class="card p-24">
        <h3 class="section-title">Действия</h3>

        <div class="actions-grid">
          <button
            class="btn primary"
            type="button"
            :disabled="pendingCommand === 'wake'"
            @click="sendCommand('wake')"
          >
            {{ pendingCommand === 'wake' ? 'Отправляем...' : 'Включить устройство' }}
          </button>

          <button
            class="btn"
            type="button"
            :disabled="pendingCommand === 'sleep'"
            @click="sendCommand('sleep')"
          >
            {{ pendingCommand === 'sleep' ? 'Отправляем...' : 'Выключить устройство' }}
          </button>

          <button
            class="btn"
            type="button"
            :disabled="pendingCommand === 'fetch'"
            @click="sendCommand('fetch')"
          >
            {{ pendingCommand === 'fetch' ? 'Запрашиваем...' : 'Запросить свежие данные' }}
          </button>
        </div>
      </div>
    </section>
  </div>

  <div v-else class="card p-24" style="max-width:980px; margin:24px auto">
    {{ loading ? 'Загрузка устройства...' : (error || 'Устройство не найдено') }}
  </div>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { useRoute } from 'vue-router'
import { useToast } from 'vue-toastification'
import { api } from '../api'
import {
  formatBattery,
  getDisplayDeviceName,
  getPowerStateLabel,
  getTenantAccent,
  isBatteryLow,
} from '../devicePresentation'

const route = useRoute()
const toast = useToast()

const device = ref(null)
const loading = ref(false)
const error = ref('')
const pendingCommand = ref('')

const deviceId = computed(() => String(route.params.deviceId || ''))
const tenantId = computed(() => String(route.query.tenant_id || ''))

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
    ...raw,
    tenant_id: raw?.tenant_id ?? raw?.id ?? '',
    name: raw?.name ?? raw?.tenant_name ?? raw?.tenant_id ?? raw?.id ?? 'Тенант',
  }
}

function normalizeDevice(raw, tenant = null) {
  return {
    ...raw,
    device_id: raw?.device_id ?? raw?.id ?? raw?.external_id ?? raw?.name,
    tenant_id:
      raw?.tenant_id ??
      raw?.tenant?.tenant_id ??
      raw?.tenant?.id ??
      tenant?.tenant_id ??
      '',
    tenant_name:
      raw?.tenant_name ??
      raw?.tenant?.name ??
      tenant?.name ??
      'Тенант',
  }
}

function devicesPath(tenantIdValue = '') {
  const query = tenantIdValue ? `?tenant_id=${encodeURIComponent(tenantIdValue)}` : ''
  return `/devices/${query}`
}

const displayName = computed(() => (device.value ? getDisplayDeviceName(device.value) : ''))

const coordsText = computed(() => {
  if (!device.value || device.value.lat == null || device.value.lon == null) return 'n/a'
  return `${Number(device.value.lat).toFixed(5)}, ${Number(device.value.lon).toFixed(5)}`
})

const locationUpdatedAt = computed(() => {
  const value =
    device.value?.location_updated_at ||
    device.value?.updated_at ||
    device.value?.last_seen

  if (!value) return 'n/a'
  const d = new Date(value)
  return Number.isNaN(d.getTime()) ? String(value) : d.toLocaleString('ru-RU')
})

async function loadDevice() {
  loading.value = true
  error.value = ''

  try {
    const tenantsResp = await api.get('/tenants')
    const tenantList = normalizeRows(tenantsResp).map(normalizeTenant)

    const tenantCandidates = tenantId.value
      ? [tenantId.value]
      : tenantList.map((tenant) => tenant.tenant_id)

    for (const currentTenantId of tenantCandidates) {
      try {
        const listResp = await api.get(devicesPath(currentTenantId))
        const list = normalizeRows(listResp)
        const match = list.find(
          (item) => String(item.device_id ?? item.id ?? item.external_id) === deviceId.value
        )

        if (match) {
          const tenant = tenantList.find(
            (item) => String(item.tenant_id) === String(currentTenantId)
          )
          device.value = normalizeDevice(match, tenant)
          return
        }
      } catch (e) {
        console.warn('Ошибка загрузки устройств tenant', currentTenantId, e)
      }
    }

    try {
      const fallbackResp = await api.get('/devices/')
      const fallbackList = normalizeRows(fallbackResp)
      const match = fallbackList.find(
        (item) => String(item.device_id ?? item.id ?? item.external_id) === deviceId.value
      )

      if (match) {
        device.value = normalizeDevice(match)
        return
      }
    } catch (e) {
      console.warn('Fallback /devices/ не сработал', e)
    }

    error.value = 'Не удалось найти устройство'
  } catch (e) {
    error.value = e?.body?.detail || e?.message || 'Ошибка загрузки устройства'
  } finally {
    loading.value = false
  }
}

async function sendCommand(type) {
  if (!device.value) return

  pendingCommand.value = type

  try {
    const query = device.value.tenant_id
      ? `?tenant_id=${encodeURIComponent(device.value.tenant_id)}`
      : ''

    await api.post(`/devices/${encodeURIComponent(device.value.device_id)}/command${query}`, {
      type,
      params: {},
      retain: false,
    })

    toast.success('Команда отправлена')
  } catch (e) {
    toast.error(e?.body?.detail || e?.message || 'Не удалось отправить команду')
  } finally {
    pendingCommand.value = ''
  }
}

onMounted(loadDevice)
</script>

<style scoped>
.device-page {
  max-width: 1180px;
  margin: 24px auto;
  display: grid;
  gap: 18px;
}

.device-shell {
  border: 2px solid var(--tenant-accent);
}

.device-shell__head {
  display: flex;
  justify-content: space-between;
  gap: 16px;
  align-items: flex-start;
  flex-wrap: wrap;
}

.device-shell__eyebrow {
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--tenant-accent);
  font-weight: 700;
  margin-bottom: 8px;
}

.device-shell__title {
  font-size: 30px;
  margin-bottom: 6px;
}

.device-shell__grid {
  margin-top: 18px;
  display: grid;
  gap: 14px;
  grid-template-columns: repeat(3, minmax(0, 1fr));
}

.device-layout {
  display: grid;
  gap: 18px;
  grid-template-columns: 1fr;
}

.info-box {
  padding: 16px;
  border-radius: 18px;
  background: #f8fbff;
  border: 1px solid rgba(148, 163, 184, 0.18);
}

.info-label {
  display: block;
  font-size: 12px;
  color: #64748b;
  margin-bottom: 8px;
}

.actions-grid {
  display: grid;
  gap: 12px;
}

.warning-chip {
  margin-top: 10px;
  padding: 10px 12px;
  border-radius: 14px;
  background: #fff7ed;
  color: #c2410c;
  border: 1px solid #fdba74;
  font-weight: 600;
}

.mt-10 {
  margin-top: 10px;
}

@media (max-width: 900px) {
  .device-shell__grid,
  .device-layout {
    grid-template-columns: 1fr;
  }
}
</style>

