import { describe, expect, it } from 'vitest';
import { buildTopology, reconcileRoles, roleAt, shmSizeForRing, validateChain } from './logic';
import type { ChainEntry, ImageInfo, NodeInspection, TelemetryNode } from './types';

const image = (reference: string, input: string, output: string): ImageInfo => ({ reference, id: `sha256:${reference}`, architecture: 'arm64', contract: { roles: ['source', 'operator', 'sink'], input, output } });
const node = (ip: string, worker: ImageInfo): NodeInspection => ({ ip, reachable: true, rdma: [], workers: [worker], existing_deployment: false });
const entry = (ip: string, worker: string): ChainEntry => ({ key: ip, ip, rdma_device: 'hns_1:1', worker_image: worker });

describe('cascade planning', () => {
  it('infers terminal and operator roles', () => {
    expect([0, 1, 2].map(index => roleAt(index, 3))).toEqual(['source', 'operator', 'sink']);
  });
  it('reserves enough shared memory for both fixed-slot rings', () => {
    const mib = 1024 * 1024;
    expect(shmSizeForRing(64, mib)).toBe('256m');
    expect([4, 6, 8, 32, 64].map(slots => shmSizeForRing(slots, 128 * mib))).toEqual(['2g', '2g', '3g', '10g', '18g']);
  });
  it('rejects repeated physical nodes', () => {
    const worker = image('worker:a', '1:1', '1:1');
    expect(validateChain([entry('10.0.0.1', worker.reference), entry('10.0.0.1', worker.reference)], [node('10.0.0.1', worker)])).toContain('不能重复');
  });
  it('rejects adjacent type mismatch', () => {
    const source = image('worker:source', 'none', '1:1');
    const sink = image('worker:sink', '2:1', 'none');
    expect(validateChain([entry('10.0.0.1', source.reference), entry('10.0.0.2', sink.reference)], [node('10.0.0.1', source), node('10.0.0.2', sink)])).toContain('类型不兼容');
  });
  it('clears a Worker that no longer supports its reordered role', () => {
    const sourceOnly = {...image('worker:source', 'none', '1:1'), contract: {roles: ['source'], input: 'none', output: '1:1'}};
    const sinkOnly = {...image('worker:sink', '1:1', 'none'), contract: {roles: ['sink'], input: '1:1', output: 'none'}};
    const values = [entry('10.0.0.2', sinkOnly.reference), entry('10.0.0.1', sourceOnly.reference)];
    expect(reconcileRoles(values, [node('10.0.0.1', sourceOnly), node('10.0.0.2', sinkOnly)]).map(item => item.worker_image)).toEqual(['', '']);
  });
  it('orders nodes and merges both Leg observations', () => {
    const ring = {capacity_slots: 64, used_slots: 0, write_position: 0, read_position: 0, watermark_pct: 0, shutdown: false};
    const nodes: TelemetryNode[] = [
      {node_id: 'node-b', status: 'normal', last_seen: '', goodput_gbps: 1, watermark_pct: 2, links: [{link_id: 'upstream', peer_node_id: 'node-a', direction: 'ingress', status: 'connected', transport: 'rdma', goodput_gbps: 1, ring, stale: false}]},
      {node_id: 'node-a', status: 'normal', last_seen: '', goodput_gbps: 1, watermark_pct: 2, links: [{link_id: 'downstream', peer_node_id: 'node-b', direction: 'egress', status: 'disconnected', transport: 'rdma', goodput_gbps: 1, ring, stale: false}]},
    ];
    const topology = buildTopology(nodes);
    expect(topology.order).toEqual(['node-a', 'node-b']);
    expect(topology.edges).toEqual([{from: 'node-a', to: 'node-b', status: 'disconnected', stale: false, transports: ['RDMA']}]);
  });
});
