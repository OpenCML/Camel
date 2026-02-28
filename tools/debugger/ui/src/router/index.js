import { createRouter, createWebHashHistory } from 'vue-router'

const routes = [
  { path: '/', name: 'Empty', component: () => import('../views/Empty.vue'), meta: { scriptPath: null } },
  { path: '/script/:taskId', name: 'Script', component: () => import('../views/ScriptLayout.vue'), meta: { scriptPath: true } },
]

const router = createRouter({
  history: createWebHashHistory(),
  routes,
})

export default router
