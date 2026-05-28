<script setup>
import { computed, onMounted, reactive, ref, watch } from 'vue'
import { useToast } from 'vue-toastification'
import { api } from '../api'
import { refreshPrefs, METRIC_LABELS } from '../notifications'

const toast = useToast()

const tenants = ref([])
const selectedTenant = ref('')
const devices = ref([])
const prefsByDevice = reactive({})
const loading = ref(false)
const saving = ref(new Set())

const METRIC_KEYS = Object.keys(METRIC_LABELS)

const DEFAULT_RULES = {
  temperature: { enabled: true, min: 0, max: 35 },
  humidity:    { enabled: true, min: 20, max: 80 },
  battery:     { enabled: true, min: 15, max: null },
}

function makeDefaultPref() {
  return {
    master_enabled: true,
    rules: JSON.parse(JSON.stringify(DEFAULT_RULES)),
  }
}

function ensurePrefShape(raw) {
  const base = makeDefaultPref()
  if (!raw) return base
  return {
    master_enabled: raw.master_enabled !== false,
    rules: METRIC_KEYS.reduce((acc, key) => {
      const incoming = raw.rules?.[key] || {}
      acc[key] = {
        enabled: incoming.enabled !== false,
        min: incoming.min ?? base.rules[key].min,
        max: incoming.max ?? base.rules[key].max,
      }
      return acc
    }, {}),
  }
}

async function loadTenants() {
  try {
    const resp = await api.get('/tenants')
    tenants.value = resp || []
    if (tenants.value.length && !selectedTenant.value) {
      selectedTenant.value = tenants.value[0].tenant_id
    }
  } catch (e) {
    toast.error('Не удалось загрузить тенанты')
  }
}

async function loadDevices() {
  if (!selectedTenant.value) {
    devices.value = []
    return
  }

  loading.value = true
  try {
    const [deviceList, prefList] = await Promise.all([
      api.get(`/devices/?tenant_id=${encodeURIComponent(selectedTenant.value)}`),
      api.get('/notifications/prefs'),
    ])
    devices.value = deviceList || []

    const prefMap = new Map((prefList || []).map((p) => [p.device_id, p]))

    for (const key of Object.keys(prefsByDevice)) delete prefsByDevice[key]
    for (const device of devices.value) {
      prefsByDevice[device.device_id] = ensurePrefShape(prefMap.get(device.device_id))
    }
  } catch (e) {
    toast.error('Не удалось загрузить настройки уведомлений')
  } finally {
    loading.value = false
  }
}

async function savePref(deviceId) {
  const pref = prefsByDevice[deviceId]
  if (!pref) return

  saving.value.add(deviceId)
  try {
    const body = {
      master_enabled: pref.master_enabled,
      rules: METRIC_KEYS.reduce((acc, key) => {
        const r = pref.rules[key]
        acc[key] = {
          enabled: !!r.enabled,
          min: r.min === '' || r.min == null ? null : Number(r.min),
          max: r.max === '' || r.max == null ? null : Number(r.max),
        }
        return acc
      }, {}),
    }
    await api.put(`/notifications/prefs/${encodeURIComponent(deviceId)}`, body)
    toast.success('Сохранено')
    await refreshPrefs()
  } catch (e) {
    toast.error(e?.body?.detail || 'Не удалось сохранить')
  } finally {
    saving.value.delete(deviceId)
    saving.value = new Set(saving.value)
  }
}

async function setAllMasters(value) {
  for (const device of devices.value) {
    const pref = prefsByDevice[device.device_id]
    if (!pref) continue
    pref.master_enabled = value
  }
  await Promise.all(devices.value.map((d) => savePref(d.device_id)))
}

function metricLabel(key) {
  return METRIC_LABELS[key]?.label || key
}

function metricUnit(key) {
  return METRIC_LABELS[key]?.unit || ''
}

watch(selectedTenant, loadDevices)

onMounted(async () => {
  await loadTenants()
  await loadDevices()
})

const anyMasterOn = computed(() =>
  devices.value.some((d) => prefsByDevice[d.device_id]?.master_enabled)
)
</script>

<template>
  <div class="notifications-page">
    <div class="page-header">
      <h2 class="page-title">Уведомления</h2>
      <p class="page-hint">
        Получайте всплывающие предупреждения, когда показания устройства выходят за выбранный диапазон.
      </p>
    </div>

    <div class="top-controls">
      <div class="control-field">
        <div class="control-label">Тенант</div>
        <select class="input select" v-model="selectedTenant">
          <option value="" disabled>Выберите тенант</option>
          <option v-for="t in tenants" :key="t.tenant_id" :value="t.tenant_id">
            {{ t.name ?? t.tenant_name ?? t.tenant_id }}
          </option>
        </select>
      </div>

      <div class="bulk-actions">
        <button class="btn" type="button" :disabled="!devices.length" @click="setAllMasters(true)">
          Включить все
        </button>
        <button class="btn" type="button" :disabled="!devices.length" @click="setAllMasters(false)">
          Выключить все
        </button>
      </div>
    </div>

    <div v-if="loading" class="empty">Загрузка...</div>
    <div v-else-if="!devices.length" class="empty">У выбранного тенанта нет устройств</div>

    <div v-else class="device-grid">
      <div
        v-for="device in devices"
        :key="device.device_id"
        class="card device-card"
      >
        <div class="device-head">
          <div>
            <div class="device-title">{{ device.external_id || device.device_id.slice(0, 8) }}</div>
            <div class="device-sub">{{ device.model || '—' }}</div>
          </div>

          <label class="switch" v-if="prefsByDevice[device.device_id]">
            <input type="checkbox" v-model="prefsByDevice[device.device_id].master_enabled" />
            <span class="switch-track"></span>
            <span class="switch-label">
              {{ prefsByDevice[device.device_id].master_enabled ? 'Вкл' : 'Выкл' }}
            </span>
          </label>
        </div>

        <div
          v-if="prefsByDevice[device.device_id]"
          class="metric-list"
          :class="{ disabled: !prefsByDevice[device.device_id].master_enabled }"
        >
          <div
            v-for="key in METRIC_KEYS"
            :key="key"
            class="metric-row"
          >
            <label class="switch small">
              <input
                type="checkbox"
                :disabled="!prefsByDevice[device.device_id].master_enabled"
                v-model="prefsByDevice[device.device_id].rules[key].enabled"
              />
              <span class="switch-track"></span>
            </label>

            <div class="metric-name">{{ metricLabel(key) }}</div>

            <div class="metric-input">
              <span class="input-prefix">мин</span>
              <input
                class="input small"
                type="number"
                step="0.1"
                :disabled="!prefsByDevice[device.device_id].master_enabled || !prefsByDevice[device.device_id].rules[key].enabled"
                v-model.number="prefsByDevice[device.device_id].rules[key].min"
              />
              <span class="input-suffix">{{ metricUnit(key) }}</span>
            </div>

            <div class="metric-input">
              <span class="input-prefix">макс</span>
              <input
                class="input small"
                type="number"
                step="0.1"
                :disabled="!prefsByDevice[device.device_id].master_enabled || !prefsByDevice[device.device_id].rules[key].enabled"
                v-model.number="prefsByDevice[device.device_id].rules[key].max"
              />
              <span class="input-suffix">{{ metricUnit(key) }}</span>
            </div>
          </div>
        </div>

        <div class="device-actions">
          <button
            class="btn primary"
            type="button"
            :disabled="saving.has(device.device_id)"
            @click="savePref(device.device_id)"
          >
            {{ saving.has(device.device_id) ? 'Сохраняем...' : 'Сохранить' }}
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.notifications-page {
  padding-top: 8px;
}

.page-header {
  margin-bottom: 16px;
}

.page-title {
  margin: 0;
  font-size: 28px;
  line-height: 1.1;
  font-weight: 800;
  color: #0f2147;
}

.page-hint {
  margin: 6px 0 0;
  color: #4b5b7a;
  font-size: 14px;
}

.top-controls {
  display: flex;
  align-items: flex-end;
  gap: 12px;
  flex-wrap: wrap;
  margin-bottom: 16px;
}

.control-field {
  min-width: 240px;
}

.control-label {
  margin-bottom: 6px;
  font-size: 14px;
  color: #4b5b7a;
}

.bulk-actions {
  display: flex;
  gap: 8px;
  margin-left: auto;
}

.empty {
  padding: 32px;
  text-align: center;
  color: #6b7280;
  background: #f8fbff;
  border: 1px solid #e7eef8;
  border-radius: 16px;
}

.device-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(360px, 1fr));
  gap: 16px;
}

.device-card {
  background: #fff;
  border: 1px solid rgba(148, 163, 184, 0.18);
  box-shadow: 0 12px 30px rgba(15, 23, 42, 0.04);
  border-radius: 20px;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.device-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.device-title {
  font-size: 16px;
  font-weight: 700;
  color: #0f2147;
}

.device-sub {
  margin-top: 2px;
  font-size: 12px;
  color: #6b7280;
}

.switch {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
  user-select: none;
}

.switch input {
  display: none;
}

.switch-track {
  width: 38px;
  height: 22px;
  border-radius: 999px;
  background: #d7deea;
  position: relative;
  transition: background 0.15s ease;
}

.switch-track::after {
  content: '';
  position: absolute;
  top: 2px;
  left: 2px;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  background: #fff;
  box-shadow: 0 1px 3px rgba(15, 23, 42, 0.18);
  transition: transform 0.15s ease;
}

.switch input:checked + .switch-track {
  background: #2563eb;
}

.switch input:checked + .switch-track::after {
  transform: translateX(16px);
}

.switch.small .switch-track {
  width: 32px;
  height: 18px;
}

.switch.small .switch-track::after {
  width: 14px;
  height: 14px;
}

.switch.small input:checked + .switch-track::after {
  transform: translateX(14px);
}

.switch-label {
  font-size: 13px;
  color: #4b5b7a;
}

.metric-list {
  display: grid;
  gap: 10px;
}

.metric-list.disabled {
  opacity: 0.5;
}

.metric-row {
  display: grid;
  grid-template-columns: 32px 1fr 1fr 1fr;
  gap: 8px;
  align-items: center;
}

.metric-name {
  font-size: 14px;
  color: #0f2147;
  font-weight: 600;
}

.metric-input {
  display: flex;
  align-items: center;
  gap: 4px;
}

.input.small {
  padding: 6px 8px;
  font-size: 13px;
  width: 100%;
  min-width: 0;
}

.input-prefix,
.input-suffix {
  font-size: 11px;
  color: #6b7280;
}

.device-actions {
  display: flex;
  justify-content: flex-end;
}

@media (max-width: 600px) {
  .metric-row {
    grid-template-columns: 32px 1fr;
    grid-template-areas:
      'sw name'
      'sw min'
      'sw max';
  }

  .metric-row > .switch {
    grid-area: sw;
  }
  .metric-row > .metric-name {
    grid-area: name;
  }
  .metric-row > .metric-input:nth-of-type(1) {
    grid-area: min;
  }
  .metric-row > .metric-input:nth-of-type(2) {
    grid-area: max;
  }
}
</style>
