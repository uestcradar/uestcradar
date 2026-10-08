import {useEffect, useState} from 'react';
import {PreviewPanel} from './PreviewPanel';
import {resourceURL, websocketURL} from './api';

interface NodeInfo {
  node_id: string;
  instance_id: string;
  connected: boolean;
  streams: {leg: 'input' | 'output'; type_id: string; type_version: number}[];
}
interface Link {
  link_id: string; peer_node_id: string; status: string; transport: string;
  goodput_gbps: number; stale: boolean;
  ring: {used_slots: number; capacity_slots: number; watermark_pct: number; shutdown: boolean};
}
interface TelemetryNode {node_id: string; status: string; links: Link[]}

export default function App() {
  const [node, setNode] = useState<NodeInfo>();
  const [telemetry, setTelemetry] = useState<TelemetryNode[]>([]);
  const [error, setError] = useState('');
  const [telemetryConnected, setTelemetryConnected] = useState(false);
  useEffect(() => {
    const abort = new AbortController();
    let stopped = false;
    let pollTimer = 0;
    let reconnectTimer = 0;
    let socket: WebSocket;
    const poll = async () => {
      try {
        const response = await fetch(resourceURL('api/node'), {signal: abort.signal, cache: 'no-store'});
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const info: NodeInfo = await response.json();
        if (!stopped) { setNode(info); setError(''); }
      } catch {
        if (!stopped) { setNode(undefined); setError('无法连接 Frontend'); }
      } finally {
        if (!stopped) pollTimer = window.setTimeout(poll, 1000);
      }
    };
    const connect = () => {
      socket = new WebSocket(websocketURL('ws'));
      socket.onopen = () => { if (!stopped) setTelemetryConnected(true); };
      socket.onmessage = event => {
        if (stopped) return;
        try {
          const value = JSON.parse(event.data);
          if (Array.isArray(value.nodes)) setTelemetry(value.nodes);
        } catch { /* Invalid telemetry is not rendered. */ }
      };
      socket.onclose = () => {
        if (!stopped) { setTelemetryConnected(false); setTelemetry([]); reconnectTimer = window.setTimeout(connect, 1000); }
      };
    };
    void poll();
    connect();
    return () => { stopped = true; abort.abort(); window.clearTimeout(pollTimer); window.clearTimeout(reconnectTimer); socket?.close(); };
  }, []);
  const state = telemetry.find(item => item.node_id === node?.node_id);
  const contract = (leg: 'input' | 'output') => {
    const stream = node?.connected && node.streams.find(item => item.leg === leg);
    return stream ? {typeId: stream.type_id, typeVersion: stream.type_version} : undefined;
  };
  return <main>
    <header><span className="section-kicker">UESTC RADAR · NODE PREVIEW</span><h1>{node?.node_id || '节点预览'}</h1>
      <p role="status">{error || (node?.connected ? 'Sidecar 预览连接已建立' : '等待 Sidecar 连接')} · 遥测 {telemetryConnected ? (state?.status || '暂无数据') : '未连接'}</p>
      {node?.instance_id && <small>实例 {node.instance_id}</small>}
    </header>
    <PreviewPanel nodeId={node?.node_id} instanceId={node?.instance_id} input={contract('input')} output={contract('output')} />
    <section aria-label="链路与 Ring 状态">
      <h2>链路 / Ring</h2>
      {!state?.links.length && <p>等待节点遥测；进程就绪不代表已有算法结果。</p>}
      {state?.links.map(link => <article className="link-detail" key={link.link_id}>
        <h3>{link.link_id} · {link.status}{link.stale ? '（过期）' : ''}</h3>
        <p>{link.transport} · {link.goodput_gbps.toFixed(3)} GB/s · 对端 {link.peer_node_id || '—'}</p>
        <p>Ring {link.ring.used_slots}/{link.ring.capacity_slots} · {link.ring.watermark_pct.toFixed(1)}%{link.ring.shutdown ? ' · 已关闭' : ''}</p>
      </article>)}
    </section>
  </main>;
}
