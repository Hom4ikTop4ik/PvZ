<template>
  <div class="card p-28" style="margin-top:20px">
    <div v-if="!isAuth" class="hero">
      <h2>PVZ · мониторинг влажности</h2>
      <p>
        Это IoT-система для мониторинга влажности почвы.
        Чтобы продолжить, войдите в систему или зарегистрируйтесь.
      </p>

      <div class="actions">
        <router-link
          class="btn primary"
          style="min-width:220px; font-size:18px"
          to="/login"
        >
          ВОЙТИ
        </router-link>

        <router-link
          class="btn"
          style="min-width:220px; font-size:18px"
          to="/register"
        >
          РЕГИСТРАЦИЯ
        </router-link>
      </div>
    </div>

    <div v-else class="hero">
      <h2>Добро пожаловать</h2>
      <p>
        Это IoT-система для мониторинга влажности почвы.
      </p>

      <div class="actions">
        <router-link
          class="btn primary"
          style="min-width:160px; font-size:18px"
          to="/charts"
        >
          ГРАФИКИ
        </router-link>

        <router-link
          class="btn"
          style="min-width:160px; font-size:18px"
          to="/map"
        >
          КАРТА
        </router-link>

        <router-link
          class="btn"
          style="min-width:160px; font-size:18px"
          to="/management"
        >
          УПРАВЛЕНИЕ
        </router-link>
      </div>
    </div>
  </div>
</template>

<script setup>
import { useAuth } from '../auth'

const { isAuth } = useAuth()
</script>

<style scoped>
.hero {
  text-align: center;
  padding: 12px 0 8px;
}

.hero h2 {
  margin: 8px 0 12px;
  font-size: 54px;
  line-height: 1.05;
  font-weight: 800;
  color: #071a45;
}

.hero p {
  max-width: 920px;
  margin: 0 auto;
  color: #6b7280;
  font-size: 18px;
  line-height: 1.6;
}

.actions {
  margin-top: 28px;
  display: flex;
  justify-content: center;
  gap: 16px;
  flex-wrap: wrap;
}
</style>
