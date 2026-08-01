import { useEffect, useMemo, useState } from 'react';
import { buildTopology } from './logic';
import type { ClusterSnapshot, LinkSnapshot, TelemetryNode } from './types';
import './Monitor.css';

const empty: ClusterSnapshot = { generated_at: '', nodes: [] };

function age(value: string): string {
  const milliseconds = Date.now() - Date.parse(value);
  if (!Number.isFinite(milliseconds)) return '—';
  return milliseconds < 1000 ? `${Math.max(0, milliseconds).toFixed(0)} ms` : `${(milliseconds / 1000).toFixed(1)} s`;
}

export default function Monitor() {
  const [snapshot, setSnapshot] = useState<ClusterSnapshot>(empty);
  const [connected, setConnected] = useState(false);

  useEffect(() => {
    let stopped = false;
    let socket: WebSocket | undefined;
    let timer = 0;
    let reconnectDelay = 500;
    fetch('/api/snapshot', { cache: 'no-store' }).then(r => r.json()).then(value => !stopped && setSnapshot(value)).catch(() => {});
    const connect = () => {
      const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
      socket = new WebSocket(`${protocol}//${location.host}/ws`);
      socket.onopen = () => { reconnectDelay = 500; setConnected(true); };
      socket.onmessage = event => { try { setSnapshot(JSON.parse(event.data)); } catch { socket?.close(); } };
      socket.onclose = () => {
        setConnected(false);
        if (!stopped) {
          timer = window.setTimeout(connect, reconnectDelay);
          reconnectDelay = Math.min(10_000, reconnectDelay * 2);
        }
      };
      socket.onerror = () => socket?.close();
    };
    connect();
    return () => { stopped = true; clearTimeout(timer); socket?.close(); };
  }, []);

  const links = useMemo(() => snapshot.nodes.flatMap(node => node.links.map(link => ({ node, link }))), [snapshot]);
  const topology = useMemo(() => buildTopology(snapshot.nodes), [snapshot]);
  const totalGoodput = snapshot.nodes.reduce((sum, node) => sum + node.goodput_gbps, 0);
  const warning = snapshot.nodes.filter(node => node.status === 'warning').length;
  const offline = snapshot.nodes.filter(node => node.status === 'offline').length;
  const disconnected = links.filter(({link}) => link.status === 'disconnected' && !link.stale).length;

  return <section className="view-stack">
    <div className="summary-grid">
      <Metric label="Nodes" value={String(snapshot.nodes.length)} tone="sky" />
      <Metric label="Goodput" value={`${totalGoodput.toFixed(3)} GB/s`} tone="green" />
      <Metric label="Warning" value={String(warning)} tone="amber" />
      <Metric label="Offline" value={String(offline)} tone="red" />
      <Metric label="Disconnected Legs" value={String(disconnected)} tone="red" />
    </div>
    <div className="panel">
      <div className="panel-title"><span>Sidecar Cascade Topology</span><span className={`badge ${connected ? 'green' : 'red'}`}>{connected ? 'LIVE' : 'RECONNECTING'}</span></div>
      <Topology nodes={snapshot.nodes} order={topology.order} edges={topology.edges} />
    </div>
    <div className="panel table-wrap">
      <div className="panel-title">Leg & RingBuffer Details</div>
      <table><thead><tr><th>Node</th><th>Node State</th><th>Leg</th><th>Peer</th><th>Transport</th><th>Link State</th><th>GB/s</th><th>Capacity</th><th>Used</th><th>Write Pos</th><th>Read Pos</th><th>Watermark</th><th>Last Seen</th></tr></thead>
        <tbody>{links.map(({node, link}) => <LinkRow key={`${node.node_id}-${link.link_id}`} node={node} link={link} />)}</tbody>
      </table>
      {links.length === 0 && <div className="empty">暂无链路数据</div>}
    </div>
  </section>;
}

function Metric({label, value, tone}: {label: string; value: string; tone: string}) {
  return <div className="metric"><span>{label}</span><strong className={tone}>{value}</strong></div>;
}

function Topology({nodes, order, edges}: {nodes: TelemetryNode[]; order: string[]; edges: ReturnType<typeof buildTopology>['edges']}) {
  if (!nodes.length) return <div className="empty topology-empty">等待 Sidecar 遥测心跳</div>;
  const byID = new Map(nodes.map(node => [node.node_id, node]));
  const positions = new Map(order.map((id, index) => [id, {x: 70 + index * 260, y: 70}]));
  const width = Math.max(900, order.length * 260 + 100);
  return <div className="topology-scroll"><svg className="topology-svg" role="img" aria-label="Sidecar 拓扑" viewBox={`0 0 ${width} 270`} style={{width}}>
    <defs>
      <marker id="arrow-connected" markerWidth="8" markerHeight="8" refX="7" refY="3" orient="auto"><path d="M0,0 L0,6 L8,3 z" fill="#34d399" /></marker>
      <marker id="arrow-disconnected" markerWidth="8" markerHeight="8" refX="7" refY="3" orient="auto"><path d="M0,0 L0,6 L8,3 z" fill="#fb7185" /></marker>
    </defs>
    {edges.map(edge => {
      const from = positions.get(edge.from); const to = positions.get(edge.to);
      if (!from || !to) return null;
      const startX = from.x + 180; const endX = to.x; const middleX = (startX + endX) / 2;
      return <g key={`${edge.from}-${edge.to}`}><line className={`edge ${edge.status} ${edge.stale ? 'stale' : ''}`} x1={startX} y1="130" x2={endX - 8} y2="130" markerEnd={`url(#arrow-${edge.status})`} /><text className="edge-label" x={middleX} y="115">{edge.transports.join('/') || 'UNKNOWN'} · {edge.status.toUpperCase()}{edge.stale ? ' · STALE' : ''}</text></g>;
    })}
    {order.map(id => {
      const node = byID.get(id)!; const position = positions.get(id)!;
      return <g key={id} transform={`translate(${position.x},${position.y})`}><rect className={`node-box ${node.status}`} width="180" height="125" /><text className="node-name" x="90" y="28">{node.node_id}</text><text className={`node-status ${node.status}`} x="90" y="49">{node.status.toUpperCase()}</text><text className="node-metric" x="90" y="76">GOODPUT {node.goodput_gbps.toFixed(3)} GB/s</text><text className="node-metric" x="90" y="96">MAX WATER {node.watermark_pct.toFixed(1)}%</text><text className="node-metric" x="90" y="115">HEARTBEAT {age(node.last_seen)}</text></g>;
    })}
  </svg></div>;
}

function LinkRow({node, link}: {node: TelemetryNode; link: LinkSnapshot}) {
  return <tr><td><strong>{node.node_id}</strong></td><td><span className={`badge ${node.status}`}>{node.status}</span></td><td>{link.link_id}</td><td>{link.peer_node_id || '—'}</td><td>{link.transport.toUpperCase()}</td><td><span className={`badge ${link.status}`}>{link.status}</span>{link.stale && <span className="stale-text"> STALE</span>}</td><td>{link.goodput_gbps.toFixed(4)}</td><td>{link.ring.capacity_slots}</td><td>{link.ring.used_slots}</td><td>{link.ring.write_position}</td><td>{link.ring.read_position}</td><td className={link.ring.watermark_pct > 70 ? 'water-high' : ''}>{link.ring.watermark_pct.toFixed(1)}%</td><td>{age(node.last_seen)}</td></tr>;
}
