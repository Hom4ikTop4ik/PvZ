<script setup>
import { ref, reactive, computed, watch, nextTick, onMounted, onBeforeUnmount } from 'vue'
import { useToast } from 'vue-toastification'
import Papa from 'papaparse'
import {
  Chart,
  LineController,
  LineElement,
  PointElement,
  LinearScale,
  TimeScale,
  Tooltip,
  Legend,
  CategoryScale
} from 'chart.js'
import 'chartjs-adapter-date-fns'
import { ru } from 'date-fns/locale'

Chart.register(
  LineController,
  LineElement,
  PointElement,
  LinearScale,
  TimeScale,
  Tooltip,
  Legend,
  CategoryScale
)

const toast = useToast()

const temperatureChartEl = ref(null)
const humidityChartEl = ref(null)

let temperatureChart = null
let humidityChart = null
let refreshTimer = null

const tenants = ref([])
const selectedTenant = ref('')

const devices = ref([])
const selectedDeviceIds = ref([])

const selectedPeriod = ref('24h')
const dataSource = ref('backend') // backend | csv

const st = reactive({
  start: null,
  end: null,
  loading: false,
  lastError: null,

  rawTemperatureSeries: [],
  rawHumiditySeries: [],

  temperatureSeries: [],
  humiditySeries: [],

  totalBefore: 0,
  totalAfter: 0
})

const startLocal = computed({
  get: () => toLocalInput(st.start),
  set: (value) => {
    st.start = value ? new Date(value).getTime() : null
  }
})

const endLocal = computed({
  get: () => toLocalInput(st.end),
  set: (value) => {
    st.end = value ? new Date(value).getTime() : null
  }
})

function toLocalInput(ms) {
  if (ms == null) return ''
  const d = new Date(ms)
  const pad = (n) => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`
}

function parseDate(v) {
  if (v == null || v === '') return null
  const s = String(v)

  if (/^\d+$/.test(s)) {
    const n = Number(s)
    return s.length === 10 ? n * 1000 : n
  }

  if (/\d{4}-\d{2}-\d{2} \d{2}:\d{2}(:\d{2})?/.test(s)) {
    const d = new Date(s.replace(' ', 'T'))
    return Number.isNaN(d.getTime()) ? null : d.getTime()
  }

  const d = new Date(s)
  return Number.isNaN(d.getTime()) ? null : d.getTime()
}

function isoOrNull(ms) {
  return ms != null ? new Date(ms).toISOString() : null
}

function formatDateTime24(ms) {
  return new Date(ms).toLocaleString('ru-RU', {
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    hour12: false
  })
}

function palette(i) {
  const colors = [
    '#2563eb',
    '#16a34a',
    '#dc2626',
    '#7c3aed',
    '#ea580c',
    '#0891b2',
    '#db2777',
    '#4b5563'
  ]
  return colors[i % colors.length]
}

function csvEscape(value) {
  if (value == null) return ''
  const s = String(value)
  return /[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s
}

function downloadCsv(filename, content) {
  const blob = new Blob(['﻿' + content], { type: 'text/csv;charset=utf-8' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = filename
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}

function buildCsv(scope = 'all') {
  const deviceMap = new Map(devices.value.map(d => [d.device_id, d]))
  const wantTemp = scope === 'all' || scope === 'temperature'
  const wantHum = scope === 'all' || scope === 'humidity'

  const rowsByDeviceTs = new Map()
  const ensureRow = (deviceId, ts) => {
    if (!rowsByDeviceTs.has(deviceId)) rowsByDeviceTs.set(deviceId, new Map())
    const perTs = rowsByDeviceTs.get(deviceId)
    if (!perTs.has(ts)) perTs.set(ts, { temperature: null, humidity: null, location: null })
    return perTs.get(ts)
  }

  if (wantTemp) {
    for (const series of st.temperatureSeries) {
      for (const p of series.points) {
        const row = ensureRow(series.device_id, p.x)
        row.temperature = p.y
        if (row.location == null) row.location = p.location ?? null
      }
    }
  }
  if (wantHum) {
    for (const series of st.humiditySeries) {
      for (const p of series.points) {
        const row = ensureRow(series.device_id, p.x)
        row.humidity = p.y
        if (row.location == null) row.location = p.location ?? null
      }
    }
  }

  const columns = ['device_id', 'external_id', 'ts']
  if (wantTemp) columns.push('temperature')
  if (wantHum) columns.push('humidity')
  columns.push('location')

  const lines = [columns.join(',')]
  for (const [deviceId, perTs] of rowsByDeviceTs) {
    const dev = deviceMap.get(deviceId) || {}
    const sorted = Array.from(perTs.entries()).sort((a, b) => a[0] - b[0])
    for (const [ts, row] of sorted) {
      const fields = [
        csvEscape(deviceId),
        csvEscape(dev.external_id ?? ''),
        new Date(ts).toISOString(),
      ]
      if (wantTemp) fields.push(row.temperature == null ? '' : row.temperature)
      if (wantHum) fields.push(row.humidity == null ? '' : row.humidity)
      fields.push(csvEscape(row.location ?? ''))
      lines.push(fields.join(','))
    }
  }
  return lines.join('\n')
}

function exportCsv(scope = 'all') {
  const tempCount = st.temperatureSeries.reduce((s, x) => s + x.points.length, 0)
  const humCount = st.humiditySeries.reduce((s, x) => s + x.points.length, 0)
  const total = (scope === 'humidity' ? humCount : 0) +
                (scope === 'temperature' ? tempCount : 0) +
                (scope === 'all' ? tempCount + humCount : 0)
  if (!total) {
    toast.error('Нет данных для экспорта')
    return
  }

  const csv = buildCsv(scope)
  const stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)
  const suffix = scope === 'all' ? 'charts' : scope
  downloadCsv(`pvz_${suffix}_${stamp}.csv`, csv)
}

function buildMetricPoints(rows, metricName) {
  const points = []

  for (const row of rows || []) {
    const x = parseDate(row.ts || row.sent_ts || row.timestamp || row.time || row.datetime || row.date)
    const y = Number(row[metricName])

    if (x == null || !Number.isFinite(y)) continue

    points.push({
      x,
      y,
      location: row.location ?? 'нет данных'
    })
  }

  points.sort((a, b) => a.x - b.x)
  return points
}

function detectCols(fields) {
  const lower = fields.map(x => x.toLowerCase())
  const pick = (...names) => {
    for (const n of names) {
      const idx = lower.indexOf(n)
      if (idx !== -1) return fields[idx]
    }
    return null
  }

  return {
    time: pick('timestamp', 'time', 'datetime', 'date', 'ts', 'sent_ts'),
    temperature: pick('temperature', 'temp', 'air_temperature', 'soil_temperature'),
    humidity: pick('humidity', 'humidity_percent', 'soil_humidity', 'moisture'),
    device: pick('device_id', 'device', 'external_id'),
    location: pick('location', 'place')
  }
}

function periodToRange(period) {
  const now = Date.now()

  if (period === '24h') {
    return { start: now - 24 * 60 * 60 * 1000, end: now }
  }

  if (period === '7d') {
    return { start: now - 7 * 24 * 60 * 60 * 1000, end: now }
  }

  if (period === '30d') {
    return { start: now - 30 * 24 * 60 * 60 * 1000, end: now }
  }

  return { start: null, end: now }
}

function applyPreset(period) {
  selectedPeriod.value = period
  const range = periodToRange(period)
  st.start = range.start
  st.end = range.end
}

function makeSeriesLabel(device) {
  const ext = device?.external_id ? String(device.external_id) : ''
  const model = device?.model ? String(device.model) : ''
  const base = ext || String(device?.device_id ?? '').slice(0, 8)
  return model ? `${base} (${model})` : base
}

function toggleDevice(deviceId) {
  if (selectedDeviceIds.value.includes(deviceId)) {
    selectedDeviceIds.value = selectedDeviceIds.value.filter(id => id !== deviceId)
    return
  }
  selectedDeviceIds.value = [...selectedDeviceIds.value, deviceId]
}

function applyLocalFilter() {
  const start = st.start
  const end = st.end

  st.totalBefore =
    st.rawTemperatureSeries.reduce((sum, item) => sum + item.points.length, 0) +
    st.rawHumiditySeries.reduce((sum, item) => sum + item.points.length, 0)

  st.temperatureSeries = st.rawTemperatureSeries
    .map(series => ({
      device_id: series.device_id,
      points: series.points.filter(p => {
        const afterStart = start == null || p.x >= start
        const beforeEnd = end == null || p.x <= end
        return afterStart && beforeEnd
      })
    }))
    .filter(series => series.points.length)

  st.humiditySeries = st.rawHumiditySeries
    .map(series => ({
      device_id: series.device_id,
      points: series.points.filter(p => {
        const afterStart = start == null || p.x >= start
        const beforeEnd = end == null || p.x <= end
        return afterStart && beforeEnd
      })
    }))
    .filter(series => series.points.length)

  st.totalAfter =
    st.temperatureSeries.reduce((sum, item) => sum + item.points.length, 0) +
    st.humiditySeries.reduce((sum, item) => sum + item.points.length, 0)
}

async function loadTenants() {
  try {
    const { api } = await import('../api')
    const resp = await api.get('/tenants')
    tenants.value = resp || []
    if (tenants.value.length && !selectedTenant.value) {
      selectedTenant.value = tenants.value[0].tenant_id
    }
  } catch (e) {
    console.warn('Не удалось загрузить тенанты:', e)
  }
}

async function loadDevices() {
  try {
    const { api } = await import('../api')
    const tenantQuery = selectedTenant.value ? `?tenant_id=${selectedTenant.value}` : ''
    const resp = await api.get(`/devices/${tenantQuery}`)
    devices.value = resp || []

    if (devices.value.length && !selectedDeviceIds.value.length) {
      selectedDeviceIds.value = [devices.value[0].device_id]
    } else {
      selectedDeviceIds.value = selectedDeviceIds.value.filter(id =>
        devices.value.some(d => d.device_id === id)
      )
    }
  } catch (e) {
    console.warn('Не удалось загрузить устройства:', e)
  }
}

async function fetchHumiditySeries(deviceId, sinceIso, untilIso) {
  const { api } = await import('../api')
  const resp = await api.post(`/charts/humidity/${deviceId}`, {
    since: sinceIso,
    until: untilIso
  })
  return resp || []
}

async function fetchTemperatureSeries(deviceId, sinceIso, untilIso) {
  const { api } = await import('../api')
  const resp = await api.post(`/charts/temperature/${deviceId}`, {
    since: sinceIso,
    until: untilIso
  })
  return resp || []
}

function makeTemperatureDatasets() {
  const deviceMap = new Map(devices.value.map(d => [d.device_id, d]))
  return st.temperatureSeries.map((series, index) => {
    const color = palette(index)
    return {
      label: makeSeriesLabel(deviceMap.get(series.device_id) || { device_id: series.device_id }),
      data: series.points,
      parsing: { xAxisKey: 'x', yAxisKey: 'y' },
      borderColor: color,
      pointBackgroundColor: color,
      pointBorderColor: color,
      borderWidth: 2.4,
      pointRadius: 1.8,
      pointHoverRadius: 5,
      tension: 0.25
    }
  })
}

function makeHumidityDatasets() {
  const deviceMap = new Map(devices.value.map(d => [d.device_id, d]))
  return st.humiditySeries.map((series, index) => {
    const color = palette(index)
    return {
      label: makeSeriesLabel(deviceMap.get(series.device_id) || { device_id: series.device_id }),
      data: series.points,
      parsing: { xAxisKey: 'x', yAxisKey: 'y' },
      borderColor: color,
      pointBackgroundColor: color,
      pointBorderColor: color,
      borderDash: [8, 6],
      borderWidth: 2.4,
      pointRadius: 1.8,
      pointHoverRadius: 5,
      tension: 0.25
    }
  })
}

function commonChartOptions(yTitle, yMin = undefined, yMax = undefined, suffix = '') {
  return {
    maintainAspectRatio: false,
    interaction: {
      intersect: false,
      mode: 'nearest'
    },
    scales: {
      x: {
        type: 'time',
        time: {
          tooltipFormat: 'dd.MM.yyyy HH:mm',
          unit: 'hour',
          displayFormats: {
            minute: 'dd.MM HH:mm',
            hour: 'dd.MM HH:mm',
            day: 'dd.MM'
          }
        },
        adapters: { date: { locale: ru } },
        grid: { color: '#eef2f8' },
        ticks: { color: '#3a4b66' }
      },
      y: {
        type: 'linear',
        min: yMin,
        max: yMax,
        title: {
          display: true,
          text: yTitle,
          color: '#2b3a55'
        },
        grid: { color: '#eef2f8' },
        ticks: {
          color: '#3a4b66',
          callback: value => `${value}${suffix}`
        }
      }
    },
    plugins: {
      legend: {
        display: true,
        position: 'bottom'
      },
      tooltip: {
        callbacks: {
          label: ctx => `${ctx.dataset.label}: ${Number(ctx.parsed.y).toFixed(1)}${suffix}`,
          afterLabel: ctx => {
            const point = ctx.raw
            return [
              `Время: ${formatDateTime24(point.x)}`,
              `Локация: ${point.location ?? '—'}`
            ]
          }
        }
      }
    }
  }
}

function destroyCharts() {
  if (temperatureChart) {
    temperatureChart.destroy()
    temperatureChart = null
  }
  if (humidityChart) {
    humidityChart.destroy()
    humidityChart = null
  }
}

function drawCharts() {
  destroyCharts()

  if (temperatureChartEl.value) {
    temperatureChart = new Chart(temperatureChartEl.value.getContext('2d'), {
      type: 'line',
      data: { datasets: makeTemperatureDatasets() },
      options: commonChartOptions('Температура (°C)', undefined, undefined, '°C')
    })
  }

  if (humidityChartEl.value) {
    humidityChart = new Chart(humidityChartEl.value.getContext('2d'), {
      type: 'line',
      data: { datasets: makeHumidityDatasets() },
      options: commonChartOptions('Влажность (%)', 0, 100, '%')
    })
  }
}

function extractErrorText(error) {
  const raw = error?.body?.detail ?? error?.message ?? error

  if (typeof raw === 'string') return raw
  if (Array.isArray(raw) && raw[0]?.msg) return raw[0].msg
  return 'Ошибка загрузки данных'
}

async function loadChartsFromBackend({ silent = false } = {}) {
  if (!selectedDeviceIds.value.length) {
    if (!silent) toast.error('Выберите хотя бы одно устройство')
    return
  }

  const sinceIso = isoOrNull(st.start) || new Date(Date.now() - 24 * 60 * 60 * 1000).toISOString()
  const untilIso = isoOrNull(st.end) || new Date().toISOString()

  st.loading = true
  st.lastError = null
  dataSource.value = 'backend'

  try {
    const results = await Promise.all(
      selectedDeviceIds.value.map(async deviceId => {
        const [tempRows, humRows] = await Promise.all([
          fetchTemperatureSeries(deviceId, sinceIso, untilIso),
          fetchHumiditySeries(deviceId, sinceIso, untilIso)
        ])

        return {
          device_id: deviceId,
          temperaturePoints: buildMetricPoints(tempRows, 'temperature'),
          humidityPoints: buildMetricPoints(humRows, 'humidity')
        }
      })
    )

    st.rawTemperatureSeries = results
      .filter(r => r.temperaturePoints.length)
      .map(r => ({
        device_id: r.device_id,
        points: r.temperaturePoints
      }))

    st.rawHumiditySeries = results
      .filter(r => r.humidityPoints.length)
      .map(r => ({
        device_id: r.device_id,
        points: r.humidityPoints
      }))

    applyLocalFilter()

    await nextTick()
    drawCharts()

    if (!st.totalAfter && !silent) {
      toast.warning('Нет данных за выбранный период')
    }
  } catch (e) {
    const message = extractErrorText(e)
    st.lastError = message

    if (!silent && !String(message).includes('Not Found')) {
      toast.error(message)
    }
  } finally {
    st.loading = false
  }
}

async function loadCsv(file) {
  if (!file) return

  st.loading = true
  st.lastError = null

  Papa.parse(file, {
    header: true,
    skipEmptyLines: true,
    dynamicTyping: true,
    complete: async (res) => {
      try {
        const rows = res?.data || []
        if (!rows.length) {
          toast.error('CSV пустой')
          return
        }

        const fields = Object.keys(rows[0] || {})
        const cols = detectCols(fields)

        if (!cols.time) {
          toast.error('В CSV не найдена колонка времени')
          return
        }

        dataSource.value = 'csv'

        const grouped = new Map()

        for (const row of rows) {
          const rawDeviceId = cols.device ? row[cols.device] : 'csv'
          const deviceId = String(rawDeviceId ?? 'csv').trim() || 'csv'

          if (!grouped.has(deviceId)) {
            grouped.set(deviceId, [])
          }

          grouped.get(deviceId).push({
            timestamp: cols.time ? row[cols.time] : null,
            temperature: cols.temperature ? row[cols.temperature] : null,
            humidity: cols.humidity ? row[cols.humidity] : null,
            location: cols.location ? row[cols.location] : null
          })
        }

        const deviceIds = Array.from(grouped.keys())
        selectedDeviceIds.value = deviceIds

        if (!devices.value.length) {
          devices.value = deviceIds.map(id => ({
            device_id: id,
            external_id: id,
            model: ''
          }))
        } else {
          const knownIds = new Set(devices.value.map(d => d.device_id))
          for (const id of deviceIds) {
            if (!knownIds.has(id)) {
              devices.value.push({
                device_id: id,
                external_id: id,
                model: ''
              })
            }
          }
        }

        st.rawTemperatureSeries = deviceIds
          .map(deviceId => ({
            device_id: deviceId,
            points: buildMetricPoints(grouped.get(deviceId), 'temperature')
          }))
          .filter(item => item.points.length)

        st.rawHumiditySeries = deviceIds
          .map(deviceId => ({
            device_id: deviceId,
            points: buildMetricPoints(grouped.get(deviceId), 'humidity')
          }))
          .filter(item => item.points.length)

        const allPoints = [
          ...st.rawTemperatureSeries.flatMap(s => s.points),
          ...st.rawHumiditySeries.flatMap(s => s.points)
        ].sort((a, b) => a.x - b.x)

        if (allPoints.length && (st.start == null || st.end == null)) {
          st.start = allPoints[0].x
          st.end = allPoints[allPoints.length - 1].x
        }

        applyLocalFilter()

        await nextTick()
        drawCharts()

        toast.success('CSV загружен')
      } catch (e) {
        st.lastError = e?.message || 'Ошибка обработки CSV'
        toast.error(st.lastError)
      } finally {
        st.loading = false
      }
    },
    error: (err) => {
      st.loading = false
      st.lastError = err?.message || 'Ошибка чтения CSV'
      toast.error(st.lastError)
    }
  })
}

function startAutoRefresh() {
  stopAutoRefresh()
  refreshTimer = setInterval(() => {
    if (!st.loading && dataSource.value === 'backend') {
      loadChartsFromBackend({ silent: true })
    }
  }, 30000)
}

function stopAutoRefresh() {
  if (refreshTimer) {
    clearInterval(refreshTimer)
    refreshTimer = null
  }
}

watch(
  () => selectedTenant.value,
  async () => {
    if (dataSource.value === 'csv') return
    selectedDeviceIds.value = []
    await loadDevices()
    await loadChartsFromBackend({ silent: true })
  }
)

watch(
  () => [st.start, st.end],
  async () => {
    applyLocalFilter()
    await nextTick()
    drawCharts()
  }
)

onMounted(async () => {
  await loadTenants()
  await loadDevices()
  applyPreset('24h')
  await loadChartsFromBackend({ silent: true })
  startAutoRefresh()
})

onBeforeUnmount(() => {
  stopAutoRefresh()
  destroyCharts()
})
</script>

<template>
  <div class="charts-page">
    <div class="charts-title-wrap">
      <h2 class="charts-title">Показания датчиков</h2>
    </div>

    <div class="top-controls">
      <div class="control-field">
        <div class="control-label">Тенант</div>
        <select class="input select" v-model="selectedTenant" :disabled="dataSource === 'csv'">
          <option value="" disabled>Выберите тенант</option>
          <option v-for="t in tenants" :key="t.tenant_id" :value="t.tenant_id">
            {{ t.name ?? t.tenant_name ?? t.tenant_id }}
          </option>
        </select>
      </div>

      <div class="control-field">
        <div class="control-label">Период</div>
        <select class="input select" v-model="selectedPeriod" @change="applyPreset(selectedPeriod)">
          <option value="24h">24 часа</option>
          <option value="7d">7 дней</option>
          <option value="30d">30 дней</option>
          <option value="all">Весь период</option>
        </select>
      </div>

      <div class="control-field datetime-field">
        <div class="control-label">Начало</div>
        <input class="input" type="datetime-local" v-model="startLocal" />
      </div>

      <div class="control-field datetime-field">
        <div class="control-label">Конец</div>
        <input class="input" type="datetime-local" v-model="endLocal" />
      </div>

      <div class="control-field file-field">
        <div class="control-label">CSV</div>
        <input
          class="input file-input"
          type="file"
          accept=".csv,text/csv"
          @change="e => loadCsv(e.target.files?.[0])"
        />
      </div>

      <button class="btn primary refresh-btn" :disabled="st.loading" @click="loadChartsFromBackend()">
        {{ st.loading ? 'Обновление...' : 'Обновить' }}
      </button>
    </div>

    <div class="layout-grid">
      <div class="card devices-card">
        <div class="panel-title">Устройства</div>

        <div class="device-list">
          <button
            v-for="device in devices"
            :key="device.device_id"
            type="button"
            class="device-item"
            :class="{ active: selectedDeviceIds.includes(device.device_id) }"
            @click="toggleDevice(device.device_id)"
          >
            <div class="device-main">{{ device.external_id ?? device.device_id }}</div>
            <div class="device-sub">{{ device.external_id ?? device.device_id }}</div>
          </button>
        </div>
      </div>

      <div class="graphs-column">
        <div class="card graph-card">
          <div class="panel-title">Температура</div>
          <div class="graph-body">
            <div v-if="!st.temperatureSeries.length" class="empty">
              Нет данных по температуре
            </div>
            <canvas v-else ref="temperatureChartEl"></canvas>
          </div>
        </div>

        <div class="card graph-card">
          <div class="panel-title">Влажность</div>
          <div class="graph-body">
            <div v-if="!st.humiditySeries.length" class="empty">
              Нет данных по влажности
            </div>
            <canvas v-else ref="humidityChartEl"></canvas>
          </div>
        </div>

        <div
          v-if="st.lastError && !String(st.lastError).includes('Not Found')"
          class="graph-error card"
        >
          {{ st.lastError }}
        </div>
      </div>
    </div>

    <div class="card stats-card">
      <div class="stats-grid">
        <div class="stat-box">
          <div class="stat-k">Выбрано устройств</div>
          <div class="stat-v">{{ selectedDeviceIds.length }}</div>
        </div>
        <div class="stat-box">
          <div class="stat-k">Точек до фильтра</div>
          <div class="stat-v">{{ st.totalBefore }}</div>
        </div>
        <div class="stat-box">
          <div class="stat-k">Точек после фильтра</div>
          <div class="stat-v">{{ st.totalAfter }}</div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.charts-page {
  padding-top: 8px;
}

.charts-title-wrap {
  margin-bottom: 16px;
}

.charts-title {
  margin: 0;
  font-size: 28px;
  line-height: 1.1;
  font-weight: 800;
  color: #0f2147;
}

.top-controls {
  display: flex;
  align-items: flex-end;
  gap: 12px;
  flex-wrap: wrap;
  margin-bottom: 16px;
}

.control-field {
  min-width: 150px;
}

.datetime-field {
  min-width: 210px;
}

.file-field {
  min-width: 220px;
}

.control-label {
  margin-bottom: 6px;
  font-size: 14px;
  color: #4b5b7a;
}

.file-input {
  padding: 10px 12px;
}

.refresh-btn {
  min-width: 146px;
}

.layout-grid {
  display: grid;
  grid-template-columns: 250px 1fr;
  gap: 16px;
  align-items: start;
}

.devices-card,
.graph-card,
.stats-card,
.graph-error {
  background: #ffffff;
  border: 1px solid rgba(148, 163, 184, 0.18);
  box-shadow: 0 20px 40px rgba(15, 23, 42, 0.06);
  border-radius: 24px;
}

.devices-card {
  padding: 12px;
  width: 250px;
}

.graphs-column {
  display: grid;
  gap: 16px;
  min-width: 0;
}

.graph-card {
  padding: 12px;
  min-width: 0;
}

.graph-error {
  padding: 12px 14px;
  color: #dc2626;
  font-size: 14px;
}

.stats-card {
  margin-top: 16px;
  padding: 16px;
}

.panel-title {
  margin-bottom: 10px;
  font-size: 17px;
  font-weight: 700;
  color: #0f2147;
}

.device-list {
  border: 1px solid #d7deea;
  border-radius: 14px;
  overflow-y: auto;
  overflow-x: hidden;
  background: #fff;
  height: 640px;
}

.device-item {
  display: block;
  width: 100%;
  padding: 10px 12px;
  text-align: left;
  border: 0;
  border-bottom: 1px solid #eef2f8;
  background: #fff;
  cursor: pointer;
}

.device-item:last-child {
  border-bottom: 0;
}

.device-item.active {
  background: #eaf1ff;
}

.device-main {
  font-size: 15px;
  font-weight: 700;
  color: #0f2147;
}

.device-sub {
  margin-top: 2px;
  font-size: 12px;
  color: #6b7280;
}

.graph-body {
  height: 300px;
}

.empty {
  height: 100%;
  display: grid;
  place-items: center;
  color: #6b7280;
  text-align: center;
}

.stats-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 12px;
}

.stat-box {
  padding: 12px 14px;
  border-radius: 16px;
  background: #f8fbff;
  border: 1px solid #e7eef8;
}

.stat-k {
  font-size: 13px;
  color: #6b7280;
}

.stat-v {
  margin-top: 6px;
  font-size: 24px;
  font-weight: 800;
  color: #0f2147;
}

@media (max-width: 1300px) {
  .datetime-field {
    min-width: 180px;
  }

  .file-field {
    min-width: 180px;
  }
}

@media (max-width: 1100px) {
  .layout-grid {
    grid-template-columns: 1fr;
  }

  .devices-card {
    width: 100%;
  }

  .device-list {
    height: 220px;
  }
}

@media (max-width: 760px) {
  .stats-grid {
    grid-template-columns: 1fr;
  }

  .graph-body {
    height: 260px;
  }

  .datetime-field,
  .file-field {
    min-width: 100%;
  }
}
</style>
