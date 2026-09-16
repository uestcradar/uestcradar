import { describe, expect, it } from 'vitest';
import { loadTopology, saveTopology, topologyStorageKey } from './topology';

const config = {
  chain: [
    {key: 'source', ip: '10.0.0.1', worker_image: 'worker:source', rdma_device: 'mlx5_0:1'},
    {key: 'sink', ip: '10.0.0.2', worker_image: 'worker:sink', rdma_device: 'mlx5_1:1'},
  ], slotCount: 8, maxPayloadBytes: 33554432,
};
function memory(raw: string | null = null) {
  return {getItem: (key: string) => key === topologyStorageKey ? raw : null,
    setItem: (_key: string, value: string) => { raw = value; }};
}

describe('topology persistence', () => {
  it('restores ordered nodes, selections and ring settings', () => {
    const storage = memory();
    expect(saveTopology(config, () => storage)).toBeUndefined();
    expect(loadTopology(() => storage)).toEqual({config});
  });
  it('persists removal of all nodes', () => {
    const storage = memory();
    saveTopology(config, () => storage);
    saveTopology({...config, chain: []}, () => storage);
    expect(loadTopology(() => storage).config.chain).toEqual([]);
  });
  it('does not persist credentials or live status', () => {
    const storage = memory();
    saveTopology({...config, chain: config.chain.map(entry => ({...entry, password: 'secret', deployment_state: 'running'}))}, () => storage);
    const raw = storage.getItem(topologyStorageKey)!;
    expect(raw).not.toContain('secret');
    expect(raw).not.toContain('running');
    expect(loadTopology(() => storage).config).toEqual(config);
  });
  it.each(['{', 'null', JSON.stringify({version: 2, ...config}),
    JSON.stringify({version: 1, ...config, chain: [config.chain[0], config.chain[0]]}),
    JSON.stringify({version: 1, ...config, slotCount: -1}),
    JSON.stringify({version: 1, ...config, chain: [{}]})])('rejects invalid storage %s', raw => {
    const result = loadTopology(() => memory(raw));
    expect(result.config.chain).toEqual([]);
    expect(result.warning).toBeTruthy();
  });
  it('handles unavailable storage without crashing', () => {
    const unavailable = () => { throw new Error('SecurityError'); };
    expect(loadTopology(unavailable).warning).toBeTruthy();
    expect(saveTopology(config, unavailable)).toBeTruthy();
    expect(loadTopology(() => memory()).warning).toBeUndefined();
  });
});
