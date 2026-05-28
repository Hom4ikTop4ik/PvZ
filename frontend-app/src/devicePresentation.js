const ALIAS_STORAGE_KEY = 'pvz_device_aliases'
const TENANT_PALETTE = [
  '#2563eb',
  '#7c3aed',
  '#059669',
  '#ea580c',
  '#db2777',
  '#0891b2',
  '#65a30d',
  '#dc2626',
]

function readAliases() {
  try {
    return JSON.parse(localStorage.getItem(ALIAS_STORAGE_KEY) || '{}')
  } catch {
    return {}
  }
}

export function getDeviceAliases() {
  return readAliases()
}

export function getDeviceAlias(deviceId) {
  return readAliases()[deviceId] || ''
}

export function setDeviceAlias(deviceId, alias) {
  const next = readAliases()
  if (alias && alias.trim()) next[deviceId] = alias.trim()
  else delete next[deviceId]

  localStorage.setItem(ALIAS_STORAGE_KEY, JSON.stringify(next))
  window.dispatchEvent(
    new CustomEvent('pvz-device-aliases-updated', {
      detail: { deviceId, alias: next[deviceId] || '' },
    })
  )
}

export function getBackendDeviceName(device) {
  return (
    device?.external_id ||
    device?.display_name ||
    device?.name ||
    device?.model ||
    device?.device_id ||
    'Устройство'
  )
}

export function getDisplayDeviceName(device) {
  return getDeviceAlias(device?.device_id) || getBackendDeviceName(device)
}

export function matchesDeviceSearch(device, query) {
  const q = String(query || '').trim().toLowerCase()
  if (!q) return true

  const haystack = [
    getDisplayDeviceName(device),
    getBackendDeviceName(device),
    device?.device_id,
    device?.tenant_name,
    device?.tenant_id,
  ]
    .filter(Boolean)
    .join(' ')
    .toLowerCase()

  return haystack.includes(q)
}

function hashString(input) {
  let h = 0
  const s = String(input || '')
  for (let i = 0; i < s.length; i += 1) {
    h = (h << 5) - h + s.charCodeAt(i)
    h |= 0
  }
  return Math.abs(h)
}

export function getTenantAccent(tenantKey) {
  return TENANT_PALETTE[hashString(tenantKey) % TENANT_PALETTE.length]
}

export function formatBattery(value) {
  if (value === null || value === undefined || value === '') return 'n/a'
  const num = Number(value)
  if (Number.isNaN(num)) return String(value)
  return `${Math.round(num)}%`
}

export function isBatteryLow(value) {
  const num = Number(value)
  return !Number.isNaN(num) && num <= 5
}

export function getPowerState(device) {
  return !!(
    device?.enabled ??
    device?.is_enabled ??
    device?.is_on ??
    device?.power_on ??
    device?.online
  )
}

export function getPowerStateLabel(device) {
  return getPowerState(device) ? 'Вкл' : 'Выкл'
}
