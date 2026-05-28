// src/router.js
import { createRouter, createWebHistory } from 'vue-router'

import HomeView from './components/HomeView.vue'
import ChartsView from './components/ChartsView.vue'
import LoginView from './components/LoginView.vue'
import RegisterView from './components/RegisterView.vue'
import TenantsView from './components/TenantsView.vue'
import MapView from './components/MapView.vue'
import ManagementView from './components/ManagementView.vue'
import DeviceControlView from './components/DeviceControlView.vue'
import NotificationsView from './components/NotificationsView.vue'

const routes = [
  { path: '/', component: HomeView },
  { path: '/login', component: LoginView },
  { path: '/register', component: RegisterView },

  { path: '/tenants', component: TenantsView, meta: { requiresAuth: true } },
  { path: '/map', component: MapView, meta: { requiresAuth: true } },
  { path: '/charts', component: ChartsView, meta: { requiresAuth: true } },

  { path: '/management', component: ManagementView, meta: { requiresAuth: true } },
  { path: '/management/:deviceId', component: DeviceControlView, meta: { requiresAuth: true } },

  { path: '/notifications', component: NotificationsView, meta: { requiresAuth: true } },
]

const router = createRouter({
  history: createWebHistory(),
  routes,
})

router.beforeEach((to, from, next) => {
  const token = localStorage.getItem('pvz_token')

  if (to.meta.requiresAuth && !token) {
    next('/login')
  } else {
    next()
  }
})

export { router }

