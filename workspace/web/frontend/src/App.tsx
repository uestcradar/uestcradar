import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import * as api from './api';
import type { ChainEntry, ClusterSnapshot, LinkSnapshot, NodeInspection, Role, Task, TaskOutputChunk, TelemetryNode } from './types';
import { reconcileRoles, roleAt, telemetryForEntry, validateChain } from './logic';

const newEntry = (ip: string): ChainEntry => ({ key: crypto.randomUUID(), ip, rdma_device: '', worker_image: '' });
const rdmaName = (item: {device: string; port: string}) => item.port ? `${item.device}:${item.port}` : item.device;
const terminal = (status: string) => ['completed', 'failed', 'partial'].includes(status);
const pause = (milliseconds: number) => new Promise(resolve => window.setTimeout(resolve, milliseconds));

interface ConsoleEntry { id: string; at: Date; stream: string; ip?: string; text: string }

export default function App() {
  const [authenticated, setAuthenticated] = useState(false);
  const [loginOpen, setLoginOpen] = useState(false);
  const pendingAction = useRef<null | (() => Promise<void>)>(null);
  const [nodes, setNodes] = useState<NodeInspection[]>([]);
  const [chain, setChain] = useState<ChainEntry[]>([]);
  const [snapshot, setSnapshot] = useState<ClusterSnapshot>({generated_at: '', nodes: []});
  const [logs, setLogs] = useState<ConsoleEntry[]>([]);
  const [busy, setBusy] = useState(false);
  const [isStreamRunning, setStreamRunning] = useState(false);
  const [workerNode, setWorkerNode] = useState<NodeInspection>();
  const [detail, setDetail] = useState<{entry: ChainEntry; index: number}>();
  const [hostKeyNode, setHostKeyNode] = useState<NodeInspection>();

  const activeIPs = useMemo(() => new Set(chain.map(entry => entry.ip)), [chain]);
  const poolNodes = useMemo(() => nodes.filter(node => !activeIPs.has(node.ip)), [nodes, activeIPs]);
  const chainError = useMemo(() => validateChain(chain, nodes), [chain, nodes]);
  const controlsLocked = busy || isStreamRunning;
  const totalGoodput = useMemo(() => snapshot.nodes.reduce((maximum, node) => Math.max(maximum, node.goodput_gbps || 0), 0), [snapshot]);

  const appendLog = useCallback((text: string, stream = 'system', ip?: string) => {
    setLogs(current => [...current, {id: crypto.randomUUID(), at: new Date(), stream, ip, text}].slice(-1000));
  }, []);
  const appendChunks = useCallback((chunks: TaskOutputChunk[]) => {
    if (!chunks.length) return;
    setLogs(current => [...current, ...chunks.map(chunk => ({id: `${chunk.sequence}-${chunk.at}-${chunk.ip || ''}`, at: new Date(chunk.at), stream: chunk.stream, ip: chunk.ip, text: chunk.text}))].slice(-1000));
  }, []);
  const refreshNodes = useCallback(async () => setNodes(await api.fetchNodes()), []);

  const runTask = useCallback(async (created: Task, label: string): Promise<Task> => {
    setBusy(true);
    appendLog(`${label}，任务 ${created.id} 已提交\n`);
    let latest = created;
    let after = 0;
    try {
      while (!terminal(latest.status)) {
        await pause(400);
        latest = await api.fetchTask(created.id, after);
        appendChunks(latest.output || []);
        for (const chunk of latest.output || []) after = Math.max(after, chunk.sequence);
      }
      if (latest.output_truncated) appendLog('远端输出超过 1 MiB，后续内容已截断。\n', 'stderr');
      appendLog(`${label}${latest.status === 'completed' ? '完成' : `结束：${latest.message || latest.status}`}\n`, latest.status === 'completed' ? 'system' : 'stderr', latest.current_ip);
      return latest;
    } finally {
      setBusy(false);
    }
  }, [appendChunks, appendLog]);

  const requireSession = useCallback((action: () => Promise<void>, alwaysPrompt = false) => {
    if (authenticated && !alwaysPrompt) void action();
    else {
      pendingAction.current = action;
      setLoginOpen(true);
    }
  }, [authenticated]);

  const inspectIPs = useCallback(async (ips: string[]) => {
    if (!ips.length) return;
    try {
      const result = await runTask(await api.inspectNodes(ips), `探查 ${ips.length} 个节点`);
      await refreshNodes();
      const refreshed = await api.fetchNodes();
      setNodes(refreshed);
      const hostKey = refreshed.find(node => ips.includes(node.ip) && node.host_key_required);
      if (hostKey) setHostKeyNode(hostKey);
      if (result.status !== 'completed') appendLog('部分节点探查未完成，请检查远端输出。\n', 'stderr');
    } catch (error) {
      handleError(error, appendLog, () => setLoginOpen(true));
    }
  }, [appendLog, refreshNodes, runTask]);

  useEffect(() => { refreshNodes().catch(error => appendLog(`${String(error)}\n`, 'stderr')); }, [appendLog, refreshNodes]);
  useEffect(() => {
    let socket: WebSocket | undefined;
    let retry = 0;
    let stopped = false;
    const connect = async () => {
      try { setSnapshot(await api.fetchSnapshot()); } catch { /* WebSocket remains authoritative. */ }
      if (stopped) return;
      const scheme = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      socket = new WebSocket(`${scheme}//${window.location.host}/ws`);
      socket.onmessage = event => { try { setSnapshot(JSON.parse(event.data) as ClusterSnapshot); } catch { /* Ignore malformed telemetry frames. */ } };
      socket.onclose = () => { if (!stopped) retry = window.setTimeout(connect, 1500); };
    };
    void connect();
    return () => { stopped = true; window.clearTimeout(retry); socket?.close(); };
  }, []);

  useEffect(() => {
    if (chain.length && chain.every(entry => nodes.find(node => node.ip === entry.ip)?.deployment_state === 'running')) setStreamRunning(true);
  }, [chain, nodes]);

  const login = async (credentials: Parameters<typeof api.createSession>[0]) => {
    try {
      await api.createSession(credentials);
      setAuthenticated(true);
      setLoginOpen(false);
      appendLog(`SSH 凭证 Session 已建立，用户 ${credentials.username}。\n`);
      const action = pendingAction.current;
      pendingAction.current = null;
      if (action) await action();
    } catch (error) {
      appendLog(`SSH Session 建立失败：${errorMessage(error)}\n`, 'stderr');
      throw error;
    }
  };

  const inspectAll = () => requireSession(() => inspectIPs(nodes.map(node => node.ip)));
  const inspectOne = (node: NodeInspection) => requireSession(() => inspectIPs([node.ip]));
  const addCustomNode = (ip: string) => requireSession(async () => {
    try {
      appendLog(`添加自定义节点 ${ip}。\n`);
      await api.addNode(ip);
      await refreshNodes();
    } catch (error) { handleError(error, appendLog, () => setLoginOpen(true)); }
  });
  const confirmHostKey = async (node: NodeInspection) => {
    try {
      await api.confirmHostKey(node.ip, node.host_key_fingerprint || '');
      appendLog(`已确认 ${node.ip} 的 SSH Host Key：${node.host_key_fingerprint}\n`);
      setHostKeyNode(undefined);
      await inspectIPs([node.ip]);
    } catch (error) { handleError(error, appendLog, () => setLoginOpen(true)); }
  };
  const updateSidecar = (node: NodeInspection) => requireSession(async () => {
    try { await runTask(await api.syncSidecar(node.ip), `更新 ${node.ip} Sidecar`); await refreshNodes(); }
    catch (error) { handleError(error, appendLog, () => setLoginOpen(true)); }
  });
  const updateWorker = async (node: NodeInspection, image: string) => {
    setWorkerNode(undefined);
    try { await runTask(await api.syncWorker(node.ip, image), `更新 ${node.ip} Worker`); await refreshNodes(); }
    catch (error) { handleError(error, appendLog, () => setLoginOpen(true)); }
  };
  const addToChain = (node: NodeInspection) => setChain(current => reconcileRoles([...current, newEntry(node.ip)], nodes));
  const updateEntry = (index: number, patch: Partial<ChainEntry>) => setChain(current => current.map((entry, position) => position === index ? {...entry, ...patch} : entry));
  const moveEntry = (index: number, offset: number) => setChain(current => {
    const target = index + offset;
    if (target < 0 || target >= current.length) return current;
    const next = [...current];
    [next[index], next[target]] = [next[target], next[index]];
    return reconcileRoles(next, nodes);
  });
  const removeEntry = (index: number) => {
    setChain(current => reconcileRoles(current.filter((_, position) => position !== index), nodes));
    setDetail(undefined);
  };

  const startStream = () => requireSession(async () => {
    if (chainError) { appendLog(`${chainError}\n`, 'stderr'); return; }
    try {
      appendLog('正在校验拓扑并生成各节点部署参数。\n');
      const plan = await api.previewPlan(chain);
      const replace = plan.nodes.some(node => node.existing_deployment);
      if (replace && !window.confirm('目标节点存在已有部署，是否停止并覆盖为当前拓扑？')) {
        appendLog('用户取消覆盖已有部署。\n');
        return;
      }
      const result = await runTask(await api.deployPlan(plan.id, replace), '下发并启动数据流');
      setStreamRunning(result.status === 'completed' || result.status === 'partial');
      await refreshNodes();
    } catch (error) { handleError(error, appendLog, () => setLoginOpen(true)); }
  });
  const stopStream = () => requireSession(async () => {
    if (!chain.length) return;
    try {
      const result = await runTask(await api.stopDeployment(chain.map(entry => entry.ip)), '停止数据流');
      if (result.status === 'completed') setStreamRunning(false);
      await refreshNodes();
    } catch (error) { handleError(error, appendLog, () => setLoginOpen(true)); }
  });

  return <div className="dashboard-shell">
    <header className="topbar">
      <div><span className="brand-kicker">UESTC RADAR</span><h1>UESTC Radar · 单页一体化控制台</h1></div>
      <button className={isStreamRunning ? 'stream-button stop' : 'stream-button start'} disabled={busy || (!isStreamRunning && Boolean(chainError))} onClick={isStreamRunning ? stopStream : startStream}>
        <PowerIcon />{busy ? '任务执行中' : isStreamRunning ? '停止数据流' : '一键下发并全速启动数据流'}
      </button>
    </header>
    {isStreamRunning && <div className="running-banner">数据流正在运行。拓扑配置已锁定，实时链路与 RingBuffer 指标保持更新。</div>}
    <main className="dashboard-body">
      <NodePool nodes={poolNodes} locked={controlsLocked} onInspect={inspectAll} onAdd={addCustomNode} onJoin={addToChain} onSidecar={updateSidecar} onWorker={node => requireSession(async () => setWorkerNode(node))} onLogin={inspectOne} />
      <section className="workspace-column">
        <TopologyCanvas chain={chain} nodes={nodes} telemetry={snapshot.nodes} locked={controlsLocked} goodput={totalGoodput} onChange={updateEntry} onMove={moveEntry} onRemove={removeEntry} onDetail={(entry, index) => setDetail({entry, index})} />
        <Console logs={logs} onClear={() => setLogs([])} />
      </section>
    </main>
    {loginOpen && <LoginModal onClose={() => {pendingAction.current = null; setLoginOpen(false);}} onLogin={login} />}
    {workerNode && <WorkerModal node={workerNode} onClose={() => setWorkerNode(undefined)} onSelect={image => updateWorker(workerNode, image)} />}
    {hostKeyNode && <HostKeyModal node={hostKeyNode} onClose={() => setHostKeyNode(undefined)} onConfirm={() => confirmHostKey(hostKeyNode)} />}
    {detail && <DetailDrawer entry={detail.entry} node={telemetryForEntry(detail.index, snapshot.nodes)} onClose={() => setDetail(undefined)} />}
  </div>;
}

function NodePool({nodes, locked, onInspect, onAdd, onJoin, onSidecar, onWorker, onLogin}: {nodes: NodeInspection[]; locked: boolean; onInspect: () => void; onAdd: (ip: string) => void; onJoin: (node: NodeInspection) => void; onSidecar: (node: NodeInspection) => void; onWorker: (node: NodeInspection) => void; onLogin: (node: NodeInspection) => void}) {
  const [ip, setIP] = useState('');
  return <aside className="node-pool panel-surface">
    <div className="section-heading"><div><span className="section-kicker">PHYSICAL NODES</span><h2>已发现物理节点池</h2></div><button className="outline-button" disabled={locked} onClick={onInspect}><SearchIcon />探查节点</button></div>
    <div className={`node-pool-scroll ${locked ? 'locked-controls' : ''}`}>
      {nodes.map(node => <article className="pool-card" key={node.ip}>
        <div className="pool-card-title"><div><strong>{node.ip}</strong><span>{node.hostname || '等待 SSH 探查'}</span></div><StatusDot node={node} /></div>
        {node.error && <p className="node-error">{node.error}</p>}
        {node.host_key_required && <p className="node-warning">需要确认 SSH Host Key</p>}
        <button className="node-action sidecar" disabled={!node.hostname} onClick={() => onSidecar(node)}>更新 Sidecar</button>
        <button className="node-action worker" disabled={!node.hostname || !node.workers?.length} onClick={() => onWorker(node)}>更新 Worker</button>
        <button className="node-action login" onClick={() => onLogin(node)}>登录节点</button>
        <button className="join-button" disabled={!node.hostname || Boolean(node.error) || Boolean(node.host_key_required)} onClick={() => onJoin(node)}><PlusIcon />添加到拓扑</button>
      </article>)}
      {!nodes.length && <div className="pool-empty">当前没有可用节点，请添加自定义 IP。</div>}
    </div>
    <form className={`custom-ip ${locked ? 'locked-controls' : ''}`} onSubmit={event => {event.preventDefault(); const value = ip.trim(); if (value) {onAdd(value); setIP('');}}}>
      <label htmlFor="custom-ip">添加自定义节点 IP</label><div><input id="custom-ip" value={ip} onChange={event => setIP(event.target.value)} placeholder="例如 192.162.2.208" /><button>添加</button></div>
    </form>
  </aside>;
}

function TopologyCanvas({chain, nodes, telemetry, locked, goodput, onChange, onMove, onRemove, onDetail}: {chain: ChainEntry[]; nodes: NodeInspection[]; telemetry: TelemetryNode[]; locked: boolean; goodput: number; onChange: (index: number, patch: Partial<ChainEntry>) => void; onMove: (index: number, offset: number) => void; onRemove: (index: number) => void; onDetail: (entry: ChainEntry, index: number) => void}) {
  const viewport = useRef<HTMLDivElement>(null);
  const cards = useRef(new Map<string, HTMLElement>());
  const [lines, setLines] = useState<{x1: number; y1: number; x2: number; y2: number}[]>([]);
  const draw = useCallback(() => {
    const root = viewport.current;
    if (!root) return;
    const rootRect = root.getBoundingClientRect();
    const next = chain.slice(0, -1).flatMap((entry, index) => {
      const from = cards.current.get(entry.key); const to = cards.current.get(chain[index + 1].key);
      if (!from || !to) return [];
      const a = from.getBoundingClientRect(); const b = to.getBoundingClientRect();
      return [{x1: a.right - rootRect.left + root.scrollLeft, y1: a.top + a.height / 2 - rootRect.top + root.scrollTop, x2: b.left - rootRect.left + root.scrollLeft, y2: b.top + b.height / 2 - rootRect.top + root.scrollTop}];
    });
    setLines(next);
  }, [chain]);
  useLayoutEffect(() => {
    const root = viewport.current; if (!root) return;
    const observer = new ResizeObserver(draw); observer.observe(root); cards.current.forEach(card => observer.observe(card));
    draw(); root.addEventListener('scroll', draw); window.addEventListener('resize', draw);
    return () => {observer.disconnect(); root.removeEventListener('scroll', draw); window.removeEventListener('resize', draw);};
  }, [draw]);
  return <section className="topology-panel panel-surface">
    <div className="topology-toolbar"><div><span className="section-kicker">LIVE CASCADE</span><h2>级联数据流拓扑</h2></div><div className="topology-stats"><span>节点数 <strong>{chain.length}</strong></span><span>Goodput <strong>{goodput.toFixed(2)} GB/s</strong></span></div></div>
    <div className="topology-viewport" ref={viewport}>
      {!chain.length ? <div className="topology-empty"><EmptyPipelineIcon /><strong>拓扑管道当前为空</strong><span>请从左侧节点池点击“添加到拓扑”</span></div> : <div className="topology-track">
        <svg className="flow-lines" width="100%" height="100%" aria-hidden="true">{lines.map((line, index) => <line key={index} {...line} />)}</svg>
        {chain.map((entry, index) => <TopologyCard key={entry.key} cardRef={element => {if (element) cards.current.set(entry.key, element); else cards.current.delete(entry.key);}} entry={entry} index={index} total={chain.length} node={nodes.find(item => item.ip === entry.ip)} telemetry={telemetryForEntry(index, telemetry)} locked={locked} onChange={patch => onChange(index, patch)} onMove={offset => onMove(index, offset)} onRemove={() => onRemove(index)} onDetail={() => onDetail(entry, index)} />)}
      </div>}
    </div>
  </section>;
}

function TopologyCard({cardRef, entry, index, total, node, telemetry, locked, onChange, onMove, onRemove, onDetail}: {cardRef: (element: HTMLElement | null) => void; entry: ChainEntry; index: number; total: number; node?: NodeInspection; telemetry?: TelemetryNode; locked: boolean; onChange: (patch: Partial<ChainEntry>) => void; onMove: (offset: number) => void; onRemove: () => void; onDetail: () => void}) {
  const role = roleAt(index, total);
  const workers = (node?.workers || []).filter(image => image.architecture === 'arm64' && image.contract.roles.includes(role));
  const upstream = telemetry?.links?.find(link => link.link_id === 'upstream');
  const downstream = telemetry?.links?.find(link => link.link_id === 'downstream');
  return <article className="topology-card" ref={cardRef}>
    <div className="topology-card-head"><div><span className={`role-pill ${role}`}>{role}</span><strong>{node?.hostname || entry.ip}</strong><small>{entry.ip}</small></div><div className={`card-tools ${locked ? 'locked-controls' : ''}`}><IconButton label="前移" disabled={index === 0} onClick={() => onMove(-1)}><LeftIcon /></IconButton><IconButton label="后移" disabled={index === total - 1} onClick={() => onMove(1)}><RightIcon /></IconButton><IconButton label="移除" onClick={onRemove}><CloseIcon /></IconButton></div></div>
    <div className={`card-config ${locked ? 'locked-controls' : ''}`}>
      <label><span>1. Worker 算法</span><select title={entry.worker_image} value={entry.worker_image} onChange={event => onChange({worker_image: event.target.value})}><option value="">选择本地算法镜像</option>{workers.map(image => <option key={image.reference} value={image.reference}>{image.reference} · {image.contract.input}→{image.contract.output}</option>)}</select></label>
      <label><span>2. 网络 / RDMA</span><select value={entry.rdma_device} onChange={event => onChange({rdma_device: event.target.value})}><option value="">选择 RDMA 网卡与 IP</option>{(node?.rdma || []).filter(item => item.ipv4).map(item => <option key={rdmaName(item)} value={rdmaName(item)}>{rdmaName(item)} · {item.netdev} · {item.ipv4}</option>)}</select></label>
    </div>
    <div className="live-metrics"><div><span>Goodput</span><strong>{(telemetry?.goodput_gbps || 0).toFixed(2)} <small>GB/s</small></strong></div><StatusLabel node={telemetry} /></div>
    <RingMeter title="Upstream 槽位" link={upstream} variant="upstream" />
    <RingMeter title="Downstream 槽位" link={downstream} variant="downstream" />
    <button className="detail-button" onClick={onDetail}>查看链路与 RingBuffer 详情</button>
  </article>;
}

function RingMeter({title, link, variant}: {title: string; link?: LinkSnapshot; variant: string}) {
  const ring = link?.ring;
  const percent = Math.max(0, Math.min(100, ring?.watermark_pct || 0));
  return <div className="ring-meter"><div><span>{title}</span><strong>{ring ? `${ring.used_slots} / ${ring.capacity_slots} Slots` : '未上报'}</strong></div><div className="meter-track"><i className={variant} style={{width: `${percent}%`}} /></div></div>;
}

function Console({logs, onClear}: {logs: ConsoleEntry[]; onClear: () => void}) {
  const end = useRef<HTMLDivElement>(null);
  useEffect(() => end.current?.scrollIntoView({block: 'end'}), [logs]);
  return <section className="console-panel panel-surface"><div className="console-heading"><div><span className="section-kicker">COMMAND OUTPUT</span><h2>实时控制台与执行日志</h2></div><button className="outline-button" onClick={onClear}>清空日志</button></div><div className="console-output">{!logs.length && <span className="console-placeholder">等待执行操作，远端 SSH、Docker 与 Compose 输出将在这里显示。</span>}{logs.map(item => <div className={`console-entry ${item.stream}`} key={item.id}><span className="console-meta">{item.at.toLocaleTimeString('zh-CN', {hour12: false})}{item.ip ? `  ${item.ip}` : ''}{`  ${item.stream}`}</span><pre>{item.text}</pre></div>)}<div ref={end} /></div></section>;
}

function LoginModal({onClose, onLogin}: {onClose: () => void; onLogin: (body: Parameters<typeof api.createSession>[0]) => Promise<void>}) {
  const [username, setUsername] = useState('root'); const [mode, setMode] = useState<'password'|'key'>('password');
  const [password, setPassword] = useState(''); const [key, setKey] = useState(''); const [passphrase, setPassphrase] = useState(''); const [error, setError] = useState(''); const [submitting, setSubmitting] = useState(false);
  return <Modal title="SSH 安全登录" onClose={onClose}><form className="modal-form" onSubmit={async event => {event.preventDefault(); setSubmitting(true); setError(''); try {await onLogin(mode === 'password' ? {username, password} : {username, private_key: key, passphrase}); setPassword(''); setKey(''); setPassphrase('');} catch (value) {setError(errorMessage(value));} finally {setSubmitting(false);}}}><p>凭证仅保存在 Web 后端内存 Session 中，不会写入前端存储、日志或节点配置。</p><label>SSH User<input value={username} onChange={event => setUsername(event.target.value)} /></label><div className="segmented"><button type="button" className={mode === 'password' ? 'active' : ''} onClick={() => setMode('password')}>Password</button><button type="button" className={mode === 'key' ? 'active' : ''} onClick={() => setMode('key')}>Private Key</button></div>{mode === 'password' ? <label>Password<input type="password" required value={password} onChange={event => setPassword(event.target.value)} /></label> : <><label>Private Key<textarea required rows={7} value={key} onChange={event => setKey(event.target.value)} /></label><label>Passphrase<input type="password" value={passphrase} onChange={event => setPassphrase(event.target.value)} /></label></>} {error && <p className="form-error">{error}</p>}<button className="primary-button" disabled={submitting}>{submitting ? '正在建立 Session' : '登录并继续'}</button></form></Modal>;
}

function WorkerModal({node, onClose, onSelect}: {node: NodeInspection; onClose: () => void; onSelect: (image: string) => void}) {
  const [selected, setSelected] = useState(node.workers?.[0]?.reference || '');
  return <Modal title={`更新 Worker · ${node.ip}`} onClose={onClose}><div className="modal-form"><p>仅可选择该节点本地已探查且满足 worker/v1 契约的 Tag。点击后才会从私有源执行 docker pull。</p><label>Worker 镜像<select value={selected} onChange={event => setSelected(event.target.value)}>{node.workers.map(image => <option key={image.reference} value={image.reference}>{image.reference}</option>)}</select></label><button className="primary-button" disabled={!selected} onClick={() => onSelect(selected)}>同步所选 Worker</button></div></Modal>;
}

function HostKeyModal({node, onClose, onConfirm}: {node: NodeInspection; onClose: () => void; onConfirm: () => void}) {
  return <Modal title={`确认 SSH Host Key · ${node.ip}`} onClose={onClose}><div className="modal-form"><p>这是当前 Session 首次连接该节点。请核对以下 SHA256 指纹，指纹发生变化时系统会阻止连接。</p><code className="fingerprint">{node.host_key_fingerprint}</code><button className="primary-button" onClick={onConfirm}>确认指纹并继续探查</button></div></Modal>;
}

function DetailDrawer({entry, node, onClose}: {entry: ChainEntry; node?: TelemetryNode; onClose: () => void}) {
  return <div className="drawer-backdrop" onMouseDown={event => {if (event.target === event.currentTarget) onClose();}}><aside className="detail-drawer"><div className="drawer-heading"><div><span className="section-kicker">TELEMETRY DETAIL</span><h2>{entry.ip}</h2></div><IconButton label="关闭" onClick={onClose}><CloseIcon /></IconButton></div><div className="drawer-summary"><span>节点状态</span><StatusLabel node={node} /><span>Goodput</span><strong>{(node?.goodput_gbps || 0).toFixed(3)} GB/s</strong><span>最后心跳</span><strong>{node?.last_seen ? new Date(node.last_seen).toLocaleString('zh-CN') : '未上报'}</strong></div>{(node?.links || []).map(link => <article className="link-detail" key={link.link_id}><div><strong>{link.link_id}</strong><span>{link.transport?.toUpperCase() || 'UNKNOWN'} · {link.status}</span></div><dl><dt>Peer</dt><dd>{link.peer_node_id || '无'}</dd><dt>Goodput</dt><dd>{(link.goodput_gbps || 0).toFixed(3)} GB/s</dd><dt>Capacity</dt><dd>{link.ring.capacity_slots}</dd><dt>Used</dt><dd>{link.ring.used_slots}</dd><dt>Write Position</dt><dd>{link.ring.write_position}</dd><dt>Read Position</dt><dd>{link.ring.read_position}</dd><dt>Watermark</dt><dd>{link.ring.watermark_pct.toFixed(1)}%</dd></dl></article>)}{!node?.links?.length && <p className="drawer-empty">当前节点尚无遥测明细。</p>}</aside></div>;
}

function Modal({title, onClose, children}: {title: string; onClose: () => void; children: React.ReactNode}) { return <div className="modal-backdrop" onMouseDown={event => {if (event.target === event.currentTarget) onClose();}}><section className="modal"><div className="modal-heading"><h2>{title}</h2><IconButton label="关闭" onClick={onClose}><CloseIcon /></IconButton></div>{children}</section></div>; }
function IconButton({label, disabled, onClick, children}: {label: string; disabled?: boolean; onClick: () => void; children: React.ReactNode}) { return <button className="icon-button" aria-label={label} title={label} disabled={disabled} onClick={onClick}>{children}</button>; }
function StatusDot({node}: {node: NodeInspection}) { const state = node.error || !node.reachable ? 'offline' : node.hostname ? 'online' : 'unknown'; return <span className={`status-dot ${state}`} title={state} />; }
function StatusLabel({node}: {node?: TelemetryNode}) { const status = node?.status || 'offline'; const disconnected = node?.links?.some(link => !link.stale && link.status !== 'connected' && link.status !== 'disabled'); const label = status === 'offline' ? 'Offline' : disconnected ? 'Link Disconnected' : status === 'warning' ? 'Warning' : 'Normal'; return <span className={`status-label ${status === 'offline' ? 'offline' : disconnected ? 'disconnected' : status}`}>{label}</span>; }
function errorMessage(error: unknown) { return error instanceof Error ? error.message : String(error); }
function handleError(error: unknown, log: (text: string, stream?: string, ip?: string) => void, unauthorized: () => void) { log(`${errorMessage(error)}\n`, 'stderr'); if (error instanceof api.ApiError && error.status === 401) unauthorized(); }

const svg = (children: React.ReactNode, viewBox = '0 0 24 24') => <svg viewBox={viewBox} aria-hidden="true" focusable="false">{children}</svg>;
function SearchIcon() { return svg(<><circle cx="11" cy="11" r="7"/><path d="m20 20-4-4"/></>); }
function PowerIcon() { return svg(<><path d="M12 2v10"/><path d="M6.3 5.7a8 8 0 1 0 11.4 0"/></>); }
function PlusIcon() { return svg(<><path d="M12 5v14M5 12h14"/></>); }
function LeftIcon() { return svg(<path d="m15 18-6-6 6-6"/>); }
function RightIcon() { return svg(<path d="m9 18 6-6-6-6"/>); }
function CloseIcon() { return svg(<path d="M6 6l12 12M18 6 6 18"/>); }
function EmptyPipelineIcon() { return svg(<><rect x="3" y="7" width="6" height="10" rx="2"/><rect x="15" y="7" width="6" height="10" rx="2"/><path d="M9 12h6M12 9l3 3-3 3"/></>); }
