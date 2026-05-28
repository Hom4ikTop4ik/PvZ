import { ref } from 'vue'
import { api } from './api'

const COOLDOWN_MS = 10 * 60 * 1000
const POLL_INTERVAL_MS = 30 * 1000

const METRIC_LABELS = {
  temperature: { label: 'Температура', unit: '°C', key: 'temperature' },
  humidity:    { label: 'Влажность',   unit: '%',  key: 'humidity'     },
  battery:     { label: 'Заряд батареи', unit: '%', key: 'battery'     },
}

const prefsByDevice = ref(new Map())
const devicesById   = ref(new Map())
const lastAlertAt   = new Map()

let pollTimer = null
let toastRef  = null

function getTenantId() {
  try {
    const token = localStorage.getItem('pvz_token')
    if (!token) return null
    const payload = JSON.parse(atob(token.split('.')[1]))
    return payload.tenant_id || null
  } catch {
    return null
  }
}

function tenantQuery() {
  const id = getTenantId()
  return id ? `?tenant_id=${encodeURIComponent(id)}` : ''
}

async function reloadPrefs() {
  try {
    const items = await api.silentGet(`/notifications/prefs${tenantQuery()}`)
    const next = new Map()
    for (const item of items || []) next.set(item.device_id, item)
    prefsByDevice.value = next
  } catch (e) {
    if (e.status !== 401 && e.status !== 403) {
      console.warn('[notifications] prefs reload failed', e)
    }
  }
}

async function reloadDeviceLabels() {
  try {
    const tenants = await api.silentGet('/tenants')
    const next = new Map()
    for (const t of tenants || []) {
      try {
        const devices = await api.silentGet(`/devices/?tenant_id=${encodeURIComponent(t.tenant_id)}`)
        for (const d of devices || []) next.set(d.device_id, d)
      } catch {}
    }
    devicesById.value = next
  } catch (e) {
    if (e.status !== 401 && e.status !== 403) {
      console.warn('[notifications] device labels reload failed', e)
    }
  }
}

function shouldFire(deviceId, metric, direction) {
  const key = `${deviceId}:${metric}:${direction}`
  const last = lastAlertAt.get(key)
  const now  = Date.now()
  if (last && now - last < COOLDOWN_MS) return false
  lastAlertAt.set(key, now)
  return true
}

function deviceLabel(deviceId) {
  const dev = devicesById.value.get(deviceId)
  return dev?.external_id || dev?.model || deviceId.slice(0, 8)
}

function evaluateReading(reading) {
  const pref = prefsByDevice.value.get(reading.device_id)
  if (!pref || !pref.master_enabled) return

  for (const [metricKey, meta] of Object.entries(METRIC_LABELS)) {
    const rule = pref.rules?.[metricKey]
    if (!rule || !rule.enabled) continue

    const value = reading[meta.key]
    if (value == null || !Number.isFinite(value)) continue

    if (rule.min != null && value < rule.min && shouldFire(reading.device_id, metricKey, 'below')) {
      toastRef?.warning(
        `${deviceLabel(reading.device_id)}: ${meta.label} ${value.toFixed(1)}${meta.unit} ниже порога ${rule.min}${meta.unit}`,
        { timeout: 8000 }
      )
    }
    if (rule.max != null && value > rule.max && shouldFire(reading.device_id, metricKey, 'above')) {
      toastRef?.warning(
        `${deviceLabel(reading.device_id)}: ${meta.label} ${value.toFixed(1)}${meta.unit} выше порога ${rule.max}${meta.unit}`,
        { timeout: 8000 }
      )
    }
  }
}

async function pollOnce() {
  try {
    const readings = await api.silentGet(`/notifications/latest${tenantQuery()}`)
    for (const r of readings || []) evaluateReading(r)
  } catch {}
}

export function startNotificationWatcher(toast) {
  if (pollTimer) return
  toastRef = toast
  Promise.all([reloadPrefs(), reloadDeviceLabels()]).then(() => pollOnce())
  pollTimer = setInterval(pollOnce, POLL_INTERVAL_MS)
}

export function stopNotificationWatcher() {
  if (pollTimer) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

export async function refreshPrefs() {
  await reloadPrefs()
}

export { METRIC_LABELS }