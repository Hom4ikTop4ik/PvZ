<template>
  <div class="card p-24" style="max-width:820px; margin:24px auto">
    <h2 class="section-title">Тенанты</h2>

    <div style="display:flex; gap:18px; align-items:flex-start; flex-wrap:wrap">
      <div style="flex:1 1 320px">
        <div class="helper">Создать новый тенант</div>
        <input class="input mt-8" v-model="name" placeholder="Имя тенанта" />
        <input class="input mt-8" v-model="desc" placeholder="Описание (опционально)" />
        <div style="margin-top:12px">
          <button class="btn primary" @click="createTenant">Создать</button>
        </div>
      </div>

      <div style="flex:2 1 360px">
        <div class="helper">Ваши тенанты</div>
        <div v-if="!tenants.length" class="mt-8">Тенанты не найдены.</div>
        <ul v-else class="mt-8" style="list-style:none; padding:0; margin:0">
          <li v-for="t in tenants" :key="t.tenant_id" class="tenant-item">
            <div style="display:flex; justify-content:space-between; gap:12px; align-items:center">
              <div>
                <div><strong>{{ t.name }}</strong></div>
                <div class="helper">{{ t.description || '—' }}</div>
              </div>
              <div class="helper">ID: {{ t.tenant_id }}</div>
            </div>
            <hr />
          </li>
        </ul>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { useToast } from 'vue-toastification'
import { useAuth } from '../auth'

const toast = useToast()
const auth = useAuth()

const tenants = ref([])
const name = ref('')
const desc = ref('')

async function loadTenants() {
  try {
    const { api } = await import('../api')
    const resp = await api.get('/tenants')
    tenants.value = resp || []
  } catch (e) {
    console.warn('Не удалось загрузить тенанты', e)
  }
}

async function createTenant() {
  if (!name.value) {
    toast.error('Задайте имя тенанта')
    return
  }
  try {
    const { api } = await import('../api')
    const payload = { tenant: name.value, env: 'dev' }
    const resp = await api.post('/tenants', payload)

    if (resp?.access_token) {
      auth.login(resp.access_token, auth.userEmail.value)
      toast.success('Тенант «' + resp.tenant + '» создан, сессия обновлена')
    } else {
      toast.success('Тенант создан')
    }

    name.value = ''
    desc.value = ''
    await loadTenants()
  } catch (e) {
    toast.error(e?.body?.detail || e?.message || 'Ошибка создания тенанта')
  }
}

onMounted(() => {
  loadTenants()
})
</script>

<style scoped>
.tenant-item { padding: 8px 0 }
</style>
