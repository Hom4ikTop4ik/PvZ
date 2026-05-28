<script setup>
import { ref, computed } from 'vue'
import { useRouter } from 'vue-router'
import { api } from '../api'
import { useToast } from 'vue-toastification'
import { useAuth } from '../auth'
import { isAllowedEmailDomain, getEmailDomainError } from '../utils/emailDomain'

const router = useRouter()
const toast = useToast()
const { login } = useAuth()

const email = ref('')
const password = ref('')
const password2 = ref('')
const loading = ref(false)

const validEmail = computed(() =>
  /^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$/.test(email.value.trim())
)

const validPassword = computed(() =>
  /^[A-Za-z0-9!@#$%^&*._-]+$/.test(password.value)
)

function extractErrorMessage(e, fallback) {
  const raw =
    e?.body?.detail ??
    e?.response?.data?.detail ??
    e?.data?.detail ??
    e?.message ??
    fallback

  if (typeof raw === 'string') return raw

  if (Array.isArray(raw)) {
    const first = raw[0]
    if (typeof first === 'string') return first
    if (first?.msg) return first.msg
    return fallback
  }

  if (raw && typeof raw === 'object') {
    if (typeof raw.detail === 'string') return raw.detail
    if (typeof raw.msg === 'string') return raw.msg
  }

  return fallback
}

async function onSubmit() {
  if (!email.value.trim() || !password.value || !password2.value) {
    toast.error('Заполни все поля')
    return
  }

  if (!validEmail.value) {
    toast.error('Некорректный email. Допустимы только латиница, цифры и ._%+-')
    return
  }

  if (!isAllowedEmailDomain(email.value.trim())) {
    toast.error(getEmailDomainError())
    return
  }

  if (!validPassword.value) {
    toast.error('Пароль может содержать только латиницу, цифры и !@#$%^&*._-')
    return
  }

  if (password.value !== password2.value) {
    toast.error('Пароли не совпадают')
    return
  }

  loading.value = true

  try {
    const trimmedEmail = email.value.trim()

    const res = await api.post('/auth/register', {
      user: {
        email: trimmedEmail,
        username: trimmedEmail,
      },
      password: password.value,
    })

    const token = res?.access_token || res?.token || ''
    if (!token) {
      throw new Error('Сервер не вернул токен авторизации')
    }

    login(token, trimmedEmail)

    toast.success('Аккаунт создан, вход выполнен')
    router.replace('/')
  } catch (e) {
    toast.error(
      extractErrorMessage(e, 'Не удалось создать аккаунт. Попробуйте позже.')
    )
  } finally {
    loading.value = false
  }
}
</script>

<template>
  <section class="auth-page">
    <div class="auth-card">
      <div class="auth-head">
        <h1 class="auth-title">Регистрация</h1>
        <p class="auth-subtitle">Создай аккаунт для доступа к системе</p>
      </div>

      <form class="auth-form" @submit.prevent="onSubmit">
        <label class="auth-label">
          <span class="label-text">Email</span>
          <input
            v-model="email"
            type="email"
            class="input auth-input"
            placeholder="Введите email"
            autocomplete="email"
          />
        </label>

        <label class="auth-label">
          <span class="label-text">Пароль</span>
          <input
            v-model="password"
            type="password"
            class="input auth-input"
            placeholder="Введите пароль"
            autocomplete="new-password"
          />
        </label>

        <label class="auth-label">
          <span class="label-text">Повторите пароль</span>
          <input
            v-model="password2"
            type="password"
            class="input auth-input"
            placeholder="Повторите пароль"
            autocomplete="new-password"
          />
        </label>

        <button class="btn auth-btn" type="submit" :disabled="loading">
          {{ loading ? 'Создаем аккаунт...' : 'Зарегистрироваться' }}
        </button>
      </form>

      <p class="auth-footer">
        Уже есть аккаунт?
        <router-link to="/login" class="auth-link">Войти</router-link>
      </p>
    </div>
  </section>
</template>

<style scoped>
.auth-page {
  min-height: calc(100vh - 120px);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 48px 20px 64px;
}

.auth-card {
  width: 100%;
  max-width: 520px;
  background: rgba(255, 255, 255, 0.94);
  border: 1px solid rgba(214, 222, 235, 0.9);
  border-radius: 28px;
  box-shadow:
    0 24px 60px rgba(36, 52, 89, 0.10),
    0 8px 24px rgba(36, 52, 89, 0.06);
  padding: 32px 30px 26px;
  backdrop-filter: blur(10px);
}

.auth-head {
  margin-bottom: 22px;
}

.auth-title {
  margin: 0 0 10px;
  font-size: 40px;
  line-height: 1.05;
  font-weight: 800;
  color: #0f2147;
  letter-spacing: -0.02em;
}

.auth-subtitle {
  margin: 0;
  font-size: 16px;
  line-height: 1.5;
  color: #5e6f91;
}

.auth-form {
  display: grid;
  gap: 16px;
}

.auth-label {
  display: grid;
  gap: 8px;
}

.label-text {
  font-size: 14px;
  font-weight: 600;
  color: #22345f;
}

.auth-input {
  width: 100%;
  min-height: 54px;
  border-radius: 18px;
  border: 1px solid #d6deeb;
  background: #f8fbff;
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.65);
  transition: border-color 0.18s ease, box-shadow 0.18s ease, background 0.18s ease;
}

.auth-input:focus {
  outline: none;
  border-color: #7aa2ff;
  background: #ffffff;
  box-shadow: 0 0 0 4px rgba(122, 162, 255, 0.16);
}

.auth-btn {
  margin-top: 8px;
  min-height: 54px;
  border-radius: 18px;
  font-weight: 700;
  font-size: 16px;
  background: linear-gradient(135deg, #ffffff 0%, #f4f7fb 100%);
  color: #0f2147;
  border: 1px solid #d8dfeb;
  box-shadow: 0 10px 20px rgba(52, 72, 120, 0.08);
  transition: transform 0.15s ease, box-shadow 0.15s ease, background 0.15s ease;
}

.auth-btn:hover:not(:disabled) {
  transform: translateY(-1px);
  box-shadow: 0 14px 26px rgba(52, 72, 120, 0.12);
  background: linear-gradient(135deg, #ffffff 0%, #eef3fb 100%);
}

.auth-btn:disabled {
  opacity: 0.72;
  cursor: default;
  transform: none;
}

.auth-footer {
  margin: 18px 2px 0;
  font-size: 15px;
  color: #51627f;
}

.auth-link {
  font-weight: 600;
  color: #5b55d6;
  text-decoration: none;
}

.auth-link:hover {
  text-decoration: underline;
}

@media (max-width: 640px) {
  .auth-page {
    padding: 24px 14px 40px;
    align-items: flex-start;
  }

  .auth-card {
    max-width: 100%;
    padding: 24px 18px 20px;
    border-radius: 22px;
  }

  .auth-title {
    font-size: 32px;
  }
}
</style>
