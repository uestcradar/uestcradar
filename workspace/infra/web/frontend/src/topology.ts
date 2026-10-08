import type { ChainEntry } from './types';

export const topologyStorageKey = 'uestcradar.topology.v1';
export interface TopologyConfig {
  chain: ChainEntry[];
  detailKey?: string;
  slotCount: number;
  maxPayloadBytes: number;
}
const defaults = (): TopologyConfig => ({chain: [], slotCount: 64, maxPayloadBytes: 64 * 1024 * 1024});
const slots = [4, 6, 8, 16, 32, 64];
const payloads = [1048576, 8388608, 33554432, 67108864, 134217728];

export function loadTopology(storage: () => Pick<Storage, 'getItem'> = () => window.localStorage): {config: TopologyConfig; warning?: string} {
  try {
    const raw = storage().getItem(topologyStorageKey);
    if (!raw) return {config: defaults()};
    const value = JSON.parse(raw);
    if (!value || value.version !== 1 || !Array.isArray(value.chain) ||
        !slots.includes(value.slotCount) || !payloads.includes(value.maxPayloadBytes) ||
        !value.chain.every((entry: ChainEntry) => entry &&
          typeof entry.key === 'string' && Boolean(entry.key) &&
          typeof entry.ip === 'string' && Boolean(entry.ip) &&
          typeof entry.rdma_device === 'string' && typeof entry.worker_image === 'string') ||
        new Set(value.chain.map((entry: ChainEntry) => entry.key)).size !== value.chain.length ||
        new Set(value.chain.map((entry: ChainEntry) => entry.ip)).size !== value.chain.length) {
      throw new Error('Invalid saved topology');
    }
    return {config: {chain: value.chain.map(({key, ip, rdma_device, worker_image}: ChainEntry) => ({key, ip, rdma_device, worker_image})),
      slotCount: value.slotCount, maxPayloadBytes: value.maxPayloadBytes,
      ...(typeof value.detailKey === 'string' && value.chain.some((entry: ChainEntry) => entry.key === value.detailKey) ? {detailKey: value.detailKey} : {})}};
  } catch {
    return {config: defaults(), warning: '无法读取已保存拓扑，已使用默认配置。请检查浏览器存储。'};
  }
}

export function saveTopology(config: TopologyConfig, storage: () => Pick<Storage, 'setItem'> = () => window.localStorage): string | undefined {
  try {
    storage().setItem(topologyStorageKey, JSON.stringify({version: 1,
      chain: config.chain.map(({key, ip, rdma_device, worker_image}) => ({key, ip, rdma_device, worker_image})),
      slotCount: config.slotCount, maxPayloadBytes: config.maxPayloadBytes, detailKey: config.detailKey}));
  } catch {
    return '无法保存拓扑，刷新后可能丢失本次修改。请检查浏览器存储。';
  }
}
