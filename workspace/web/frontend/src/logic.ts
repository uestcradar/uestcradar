import type { ChainEntry, NodeInspection, Role, TelemetryNode } from './types';

export const roleAt = (index: number, total: number): Role => index === 0 ? 'source' : index === total - 1 ? 'sink' : 'operator';

export function shmSizeForSlots(slotCount: number): string {
  if (slotCount <= 64) return '256m';
  if (slotCount <= 128) return '512m';
  return '1g';
}

export function validateChain(chain: ChainEntry[], nodes: NodeInspection[]): string {
	if (chain.length < 2) return '至少需要一个 Source 节点和一个 Sink 节点';
  if (chain.some(entry => !entry.ip || !entry.rdma_device || !entry.worker_image)) return '请完整选择每个节点、RDMA 端口和 Worker';
  if (new Set(chain.map(entry => entry.ip)).size !== chain.length) return '同一物理节点不能重复出现在链路中';
  for (let index = 0; index < chain.length - 1; index++) {
    const currentNode = nodes.find(node => node.ip === chain[index].ip);
    const nextNode = nodes.find(node => node.ip === chain[index + 1].ip);
    const current = currentNode?.workers?.find(image => image.reference === chain[index].worker_image);
    const next = nextNode?.workers?.find(image => image.reference === chain[index + 1].worker_image);
    if (current && next && current.contract.output !== next.contract.input) return `类型不兼容：${current.contract.output} → ${next.contract.input}`;
  }
  return '';
}

export function workerSupportsRole(node: NodeInspection | undefined, reference: string, role: Role): boolean {
  return Boolean(node?.workers?.find(image => image.reference === reference && image.contract.roles.includes(role)));
}

export function reconcileRoles(chain: ChainEntry[], nodes: NodeInspection[]): ChainEntry[] {
  return chain.map((entry, index) => {
    const node = nodes.find(item => item.ip === entry.ip);
    return workerSupportsRole(node, entry.worker_image, roleAt(index, chain.length)) ? entry : {...entry, worker_image: ''};
  });
}

export function telemetryForEntry(index: number, nodes: TelemetryNode[]): TelemetryNode | undefined {
  return nodes.find(node => node.node_id === `node-${index + 1}`);
}

export interface TopologyEdge {
  from: string;
  to: string;
  status: 'connected' | 'disconnected';
  stale: boolean;
  transports: string[];
}

export function buildTopology(nodes: TelemetryNode[]): { order: string[]; edges: TopologyEdge[] } {
  const combined = new Map<string, TopologyEdge & { transportSet: Set<string> }>();
  for (const node of nodes) {
    for (const link of node.links || []) {
      if (link.status === 'disabled' || !link.peer_node_id) continue;
      const from = link.direction === 'ingress' ? link.peer_node_id : node.node_id;
      const to = link.direction === 'ingress' ? node.node_id : link.peer_node_id;
      const key = `${from}\u0000${to}`;
      const edge = combined.get(key) || { from, to, status: 'connected', stale: true, transports: [], transportSet: new Set<string>() };
      if (!link.stale) {
        if (edge.stale) edge.status = 'connected';
        if (link.status !== 'connected') edge.status = 'disconnected';
        edge.stale = false;
      } else if (edge.stale && link.status !== 'connected') {
        edge.status = 'disconnected';
      }
      if (link.transport && link.transport !== 'unknown') edge.transportSet.add(link.transport.toUpperCase());
      combined.set(key, edge);
    }
  }
  const edges = [...combined.values()].map(({transportSet, ...edge}) => ({...edge, transports: [...transportSet].sort()}));
  const ids = nodes.map(node => node.node_id);
  const known = new Set(ids);
  const indegree = new Map(ids.map(id => [id, 0]));
  const outgoing = new Map(ids.map(id => [id, [] as string[]]));
  for (const edge of edges) {
    if (!known.has(edge.from) || !known.has(edge.to)) continue;
    outgoing.get(edge.from)!.push(edge.to);
    indegree.set(edge.to, indegree.get(edge.to)! + 1);
  }
  const queue = ids.filter(id => indegree.get(id) === 0).sort();
  const order: string[] = [];
  while (queue.length) {
    const id = queue.shift()!;
    order.push(id);
    for (const next of outgoing.get(id)!.sort()) {
      indegree.set(next, indegree.get(next)! - 1);
      if (indegree.get(next) === 0) queue.push(next);
    }
    queue.sort();
  }
  for (const id of [...ids].sort()) if (!order.includes(id)) order.push(id);
  return { order, edges };
}
