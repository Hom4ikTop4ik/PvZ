<template>
  <div class="card p-24" style="max-width:1100px; margin:24px auto">
    <div style="display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:12px">
      <h2 class="section-title" style="margin:0">Карта</h2>

      <div v-if="error" class="helper" style="color:#b00020">
        {{ error }}
      </div>

      <button class="btn" type="button" @click="loadMarkers">
        Обновить
      </button>
    </div>

    <div
      ref="mapEl"
      style="width:100%; height:75vh; min-height:500px; border-radius:14px; overflow:hidden; margin-top:16px"
    ></div>
  </div>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref, shallowRef } from 'vue'
import { api } from '../api'
import {
  formatBattery,
  getDisplayDeviceName,
  getPowerState,
  getPowerStateLabel,
  getTenantAccent,
  isBatteryLow,
} from '../devicePresentation'

const mapEl = ref(null)
const error = ref('')
const map = shallowRef(null)
const placemarks = shallowRef([])

const deviceInfoMap = ref(new Map())

function escapeHtml(value) {
  return String(value ?? 'n/a')
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;')
}

function normalizeRows(payload) {
  if (Array.isArray(payload)) return payload
  if (Array.isArray(payload?.items)) return payload.items
  if (Array.isArray(payload?.data)) return payload.data
  if (Array.isArray(payload?.markers)) return payload.markers
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
    external_id: raw?.external_id ?? '',
    display_name: raw?.display_name ?? '',
    name: raw?.name ?? '',
    model: raw?.model ?? '',
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
      '',
  }
}

function devicesPath(tenantId = '') {
  const query = tenantId ? `?tenant_id=${encodeURIComponent(tenantId)}` : ''
  return `/devices/${query}`
}

function loadYmaps() {
  if (window.ymaps) return Promise.resolve()

  const key = import.meta.env.VITE_YMAPS_KEY
  if (!key) return Promise.reject(new Error('Не задан VITE_YMAPS_KEY в .env'))

  return new Promise((resolve, reject) => {
    const s = document.createElement('script')
    s.async = true
    s.src = `https://api-maps.yandex.ru/2.1/?apikey=${encodeURIComponent(key)}&lang=ru_RU`
    s.onload = resolve
    s.onerror = () => reject(new Error('Не удалось загрузить Яндекс Карты'))
    document.head.appendChild(s)
  })
}

async function ensureMap() {
  if (map.value) return

  await loadYmaps()
  await new Promise((resolve) => window.ymaps.ready(resolve))

  map.value = new window.ymaps.Map(mapEl.value, {
    center: [52.0907, 5.1214],
    zoom: 7,
    controls: ['zoomControl', 'typeSelector', 'fullscreenControl'],
  })
}

function clearPlacemarks() {
  if (!map.value) return
  for (const pm of placemarks.value) {
    map.value.geoObjects.remove(pm)
  }
  placemarks.value = []
}

async function buildDeviceInfoMap() {
  const index = new Map()

  try {
    const tenantsResp = await api.get('/tenants')
    const tenants = normalizeRows(tenantsResp).map(normalizeTenant)

    for (const tenant of tenants) {
      try {
        const resp = await api.get(devicesPath(tenant.tenant_id))
        const rows = normalizeRows(resp).map((device) => normalizeDevice(device, tenant))

        for (const row of rows) {
          if (!row?.device_id) continue
          index.set(String(row.device_id), row)
        }
      } catch (e) {
        console.warn('Не удалось загрузить устройства tenant', tenant.tenant_id, e)
      }
    }
  } catch (e) {
    console.warn('Не удалось загрузить tenants для карты', e)
  }

  deviceInfoMap.value = index
}

function mergeMarkerWithDevice(marker) {
  const deviceMeta = deviceInfoMap.value.get(String(marker?.device_id || ''))

  if (!deviceMeta) return marker

  return {
    ...deviceMeta,
    ...marker,
    device_id: marker?.device_id ?? deviceMeta?.device_id,
    external_id: deviceMeta?.external_id ?? marker?.external_id,
    display_name: deviceMeta?.display_name ?? marker?.display_name,
    name: deviceMeta?.name ?? marker?.name,
    model: deviceMeta?.model ?? marker?.model,
    tenant_id: marker?.tenant_id ?? deviceMeta?.tenant_id,
    tenant_name: marker?.tenant_name ?? deviceMeta?.tenant_name,
  }
}

function balloon(device) {
  const tenantKey = device?.tenant_id || device?.tenant_name || 'default'
  const tenantAccent = getTenantAccent(tenantKey)
  const displayName = getDisplayDeviceName(device)
  const battery = formatBattery(device.battery ?? device.battery_level)

  return `
    <div style="
      width: 248px;
      padding: 14px;
      border-radius: 18px;
      background: linear-gradient(180deg, #ffffff 0%, #f8fbff 100%);
      border: 2px solid ${tenantAccent};
      box-shadow: 0 14px 28px rgba(15, 23, 42, 0.10);
      color: #0f172a;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
    ">
      <div style="display:flex; justify-content:space-between; gap:10px; align-items:flex-start; margin-bottom:10px;">
        <div style="min-width:0;">
          <div style="font-size:16px; font-weight:800; line-height:1.2; word-break:break-word;">
            ${escapeHtml(displayName)}
          </div>
        </div>

        <div style="
          padding:5px 9px;
          border-radius:999px;
          background:${getPowerState(device) ? '#dcfce7' : '#fee2e2'};
          color:${getPowerState(device) ? '#15803d' : '#b91c1c'};
          font-size:11px;
          font-weight:700;
          white-space:nowrap;
        ">
          ${getPowerStateLabel(device)}
        </div>
      </div>

      <div style="display:grid; grid-template-columns:repeat(2, minmax(0, 1fr)); gap:8px;">
        <div style="padding:10px; border-radius:14px; background:#ffffff; border:1px solid rgba(224, 231, 255, 0.95);">
          <div style="font-size:11px; color:#64748b; margin-bottom:4px;">Заряд</div>
          <div style="font-size:15px; font-weight:700;">${escapeHtml(battery)}</div>
        </div>

        <div style="padding:10px; border-radius:14px; background:#ffffff; border:1px solid rgba(224, 231, 255, 0.95);">
          <div style="font-size:11px; color:#64748b; margin-bottom:4px;">Статус</div>
          <div style="font-size:13px; font-weight:600;">${escapeHtml(getPowerStateLabel(device))}</div>
        </div>

        <div style="padding:10px; border-radius:14px; background:#ffffff; border:1px solid rgba(224, 231, 255, 0.95);">
          <div style="font-size:11px; color:#64748b; margin-bottom:4px;">Широта</div>
          <div style="font-size:13px; font-weight:600;">${escapeHtml(device.lat)}</div>
        </div>

        <div style="padding:10px; border-radius:14px; background:#ffffff; border:1px solid rgba(224, 231, 255, 0.95);">
          <div style="font-size:11px; color:#64748b; margin-bottom:4px;">Долгота</div>
          <div style="font-size:13px; font-weight:600;">${escapeHtml(device.lon)}</div>
        </div>
      </div>

      ${
        isBatteryLow(device.battery ?? device.battery_level)
          ? '<div style="margin-top:10px; padding:9px 10px; border-radius:12px; background:#fff7ed; color:#c2410c; border:1px solid #fdba74; font-weight:600; font-size:13px;">Низкий заряд устройства</div>'
          : ''
      }

      <a
        href="/management/${encodeURIComponent(device.device_id)}${device.tenant_id ? `?tenant_id=${encodeURIComponent(device.tenant_id)}` : ''}"
        style="display:inline-flex; margin-top:10px; padding:9px 14px; border-radius:999px; background:#1d4ed8; color:#fff; text-decoration:none; font-weight:700; font-size:13px;"
      >
        Открыть управление
      </a>
    </div>
  `
}

async function loadMarkers() {
  error.value = ''

  try {
    await ensureMap()
    await buildDeviceInfoMap()

    const response = await api.get('/map/markers')
    const rawMarkers = normalizeRows(response)
    const markers = rawMarkers.map(mergeMarkerWithDevice)

    clearPlacemarks()

    const validMarkers = markers.filter((m) => {
      const lat = Number(m.lat)
      const lon = Number(m.lon)
      return !Number.isNaN(lat) && !Number.isNaN(lon)
    })

    if (!validMarkers.length) {
      error.value = 'Маркеры не найдены'
      return
    }

    map.value.setCenter(
      [Number(validMarkers[0].lat), Number(validMarkers[0].lon)],
      validMarkers.length === 1 ? 15 : 12
    )

    for (const marker of validMarkers) {
      const lat = Number(marker.lat)
      const lon = Number(marker.lon)
      const preset = getPowerState(marker) ? 'islands#greenDotIcon' : 'islands#redDotIcon'
      const displayName = getDisplayDeviceName(marker)

      const pm = new window.ymaps.Placemark(
        [lat, lon],
        {
          hintContent: `Устройство: ${escapeHtml(displayName)}`,
          balloonContent: balloon(marker),
        },
        { preset }
      )

      map.value.geoObjects.add(pm)
      placemarks.value.push(pm)
    }
  } catch (e) {
    error.value = e?.message || 'Ошибка загрузки карты'
  }
}

onMounted(async () => {
  await loadMarkers()
})

onBeforeUnmount(() => {
  clearPlacemarks()

  if (map.value) {
    map.value.destroy()
    map.value = null
  }
})
</script>
