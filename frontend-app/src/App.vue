<template>
  <div class="container">
    <header class="header">
      <div class="brand">
        <div class="logo"></div>
        <h1 class="title">PVZ · Мониторинг влажности</h1>
      </div>

      <nav class="tabs">
        <router-link to="/" class="tab">
          Главная
        </router-link>

        <template v-if="isAuth">
          <router-link to="/charts" class="tab">
            Графики
          </router-link>

          <router-link to="/tenants" class="tab">
            Тенанты
          </router-link>

          <router-link to="/map" class="tab">
            Карта
          </router-link>

          <router-link to="/management" class="tab">
            Управление
          </router-link>

          <router-link to="/notifications" class="tab">
            Уведомления
          </router-link>

          <span style="flex:1"></span>

          <button class="tab" type="button" @click="handleLogout">
            Выйти
          </button>
        </template>

        <template v-else>
          <span style="flex:1"></span>

          <router-link to="/login" class="tab">
            Войти
          </router-link>
          <router-link to="/register" class="tab">
            Регистрация
          </router-link>
        </template>
      </nav>
    </header>

    <main>
      <router-view />
    </main>
  </div>
</template>

<script setup>
import { onMounted, watch } from 'vue'
import { useRouter } from 'vue-router'
import { useToast } from 'vue-toastification'
import { useAuth } from './auth'
import { startNotificationWatcher, stopNotificationWatcher } from './notifications'

const router = useRouter()
const toast = useToast()
const { isAuth, logout } = useAuth()

function handleLogout() {
  logout()
  stopNotificationWatcher()
  toast.info('Вы вышли из аккаунта')
  router.push('/login')
}

onMounted(() => {
  if (isAuth.value) startNotificationWatcher(toast)
})

watch(isAuth, (value) => {
  if (value) startNotificationWatcher(toast)
  else stopNotificationWatcher()
})
</script>
