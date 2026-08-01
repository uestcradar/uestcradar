import type { ChainEntry, DeploymentPlan, NodeInspection, Task } from './types';

let csrfToken = '';

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  const headers = new Headers(init.headers);
  if (init.body) headers.set('Content-Type', 'application/json');
  if (init.method && init.method !== 'GET') headers.set('X-CSRF-Token', csrfToken);
  const response = await fetch(path, { ...init, headers, credentials: 'same-origin', cache: 'no-store' });
  if (!response.ok) throw new Error((await response.text()).trim() || `HTTP ${response.status}`);
  if (response.status === 204) return undefined as T;
  return response.json() as Promise<T>;
}

export async function createSession(body: {username: string; password?: string; private_key?: string; passphrase?: string}) {
  const value = await request<{csrf_token: string}>('/api/v1/session', { method: 'POST', body: JSON.stringify(body) });
  csrfToken = value.csrf_token;
}
export async function deleteSession() { await request<void>('/api/v1/session', { method: 'DELETE' }); csrfToken = ''; }
export const fetchNodes = () => request<NodeInspection[]>('/api/v1/orchestration/nodes');
export const addNode = (ip: string) => request<NodeInspection>('/api/v1/orchestration/nodes', { method: 'POST', body: JSON.stringify({ ip }) });
export const inspectNodes = (ips: string[]) => request<NodeInspection[]>('/api/v1/orchestration/inspect', { method: 'POST', body: JSON.stringify({ ips }) });
export const confirmHostKey = (ip: string, fingerprint: string) => request<void>('/api/v1/orchestration/host-keys/confirm', { method: 'POST', body: JSON.stringify({ ip, fingerprint }) });
export const syncImage = (ip: string, image: string) => request<Task>('/api/v1/orchestration/images/sync', { method: 'POST', body: JSON.stringify({ ip, image }) });
export const previewPlan = (chain: ChainEntry[]) => request<DeploymentPlan>('/api/v1/orchestration/plans/preview', { method: 'POST', body: JSON.stringify({ chain: chain.map(({ip, rdma_device, worker_image}) => ({ip, rdma_device, worker_image})), slot_count: 64, max_payload_bytes: 1048576, shm_size: '256m' }) });
export const deployPlan = (planId: string, confirmReplace: boolean) => request<Task>('/api/v1/orchestration/deployments', { method: 'POST', body: JSON.stringify({ plan_id: planId, confirm_replace: confirmReplace }) });
export const fetchTask = (id: string) => request<Task>(`/api/v1/orchestration/tasks/${encodeURIComponent(id)}`);
