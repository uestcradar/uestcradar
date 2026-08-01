import { useEffect, useMemo, useState } from 'react';
import * as api from './api';
import Monitor from './Monitor';
import type { ChainEntry, DeploymentPlan, NodeInspection, Role, Task } from './types';
import { roleAt, validateChain } from './logic';

const newEntry = (): ChainEntry => ({ key: crypto.randomUUID(), ip: '', rdma_device: '', worker_image: '' });
const rdmaName = (item: {device: string; port: string}) => item.port ? `${item.device}:${item.port}` : item.device;

export default function App() {
  const [view, setView] = useState<'orchestration'|'monitor'>('orchestration');
  const [authenticated, setAuthenticated] = useState(false);
  const [nodes, setNodes] = useState<NodeInspection[]>([]);
  const [chain, setChain] = useState<ChainEntry[]>([newEntry(), newEntry()]);
  const [plan, setPlan] = useState<DeploymentPlan>();
  const [task, setTask] = useState<Task>();
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  const refreshNodes = async () => { setNodes(await api.fetchNodes()); };
  const inspected = useMemo(() => nodes.filter(node => node.hostname && !node.error && !node.host_key_required), [nodes]);
  const chainError = useMemo(() => validateChain(chain, nodes), [chain, nodes]);

  const login = async (body: Parameters<typeof api.createSession>[0]) => {
    setError(''); setBusy(true);
    try { await api.createSession(body); setAuthenticated(true); await refreshNodes(); }
    catch (value) { setError(String(value)); }
    finally { setBusy(false); }
  };
  const inspect = async () => {
    setBusy(true); setError('');
    try { setNodes(await api.inspectNodes(nodes.map(node => node.ip))); }
    catch (value) { setError(String(value)); } finally { setBusy(false); }
  };
  const confirm = async (node: NodeInspection) => {
    await api.confirmHostKey(node.ip, node.host_key_fingerprint!);
    const refreshed = await api.inspectNodes([node.ip]);
    setNodes(current => current.map(item => item.ip === node.ip ? refreshed[0] : item));
  };
  const updateEntry = (index: number, patch: Partial<ChainEntry>) => {
    setPlan(undefined); setChain(current => current.map((entry, i) => i === index ? {...entry, ...patch} : entry));
  };
  const addOperator = () => { setPlan(undefined); setChain(current => [...current.slice(0, -1), newEntry(), current[current.length - 1]]); };
  const move = (index: number, offset: number) => {
    const target = index + offset; if (target < 0 || target >= chain.length) return;
    setPlan(undefined); setChain(current => { const next = [...current]; [next[index], next[target]] = [next[target], next[index]]; return next; });
  };
  const remove = (index: number) => { if (chain.length <= 2) return; setPlan(undefined); setChain(current => current.filter((_, i) => i !== index)); };
  const preview = async () => { setBusy(true); setError(''); try { setPlan(await api.previewPlan(chain)); } catch (value) { setError(String(value)); } finally { setBusy(false); } };
  const deploy = async () => {
    if (!plan) return;
    const needsReplace = plan.nodes.some(node => node.existing_deployment);
    if (needsReplace && !window.confirm('目标节点存在已有部署，确认覆盖更新？')) return;
    try { const created = await api.deployPlan(plan.id, needsReplace); setTask(created); } catch (value) { setError(String(value)); }
  };
  const synchronize = async (entry: ChainEntry) => {
    try { setTask(await api.syncImage(entry.ip, entry.worker_image)); } catch (value) { setError(String(value)); }
  };
  const logout = async () => { await api.deleteSession(); setAuthenticated(false); setNodes([]); setPlan(undefined); setTask(undefined); };
  useEffect(() => {
    if (!task || task.status === 'completed' || task.status === 'failed') return;
    const timer = window.setInterval(async () => { try { setTask(await api.fetchTask(task.id)); } catch {} }, 800);
    return () => clearInterval(timer);
  }, [task]);

  return <div className="app-shell">
    <header><div><span className="eyebrow">UESTC RADAR</span><h1>Sidecar Control Plane</h1></div><nav><button className={view === 'orchestration' ? 'active' : ''} onClick={() => setView('orchestration')}>编排</button><button className={view === 'monitor' ? 'active' : ''} onClick={() => setView('monitor')}>监控</button></nav><span className={`badge ${authenticated ? 'green' : ''}`}>{authenticated ? 'SSH SESSION' : 'LOCKED'}</span>{authenticated && <button onClick={logout}>退出</button>}</header>
    <main>{view === 'monitor' ? <Monitor /> : authenticated ? <section className="orchestration-grid">
      <aside className="view-stack">
        <div className="panel"><div className="panel-title"><span>Cluster Nodes</span><button onClick={inspect} disabled={busy}>探查全部</button></div>
          <NodeList nodes={nodes} onConfirm={confirm} />
          <AddNode onAdd={async ip => { await api.addNode(ip); await refreshNodes(); }} />
        </div>
      </aside>
      <section className="view-stack">
        <div className="panel"><div className="panel-title"><span>Cascade Sequence</span><button onClick={addOperator}>+ 添加 Operator</button></div>
          <div className="chain-editor">{chain.map((entry, index) => <ChainCard key={entry.key} entry={entry} index={index} total={chain.length} nodes={inspected} usedIPs={chain.map(item => item.ip)} onChange={patch => updateEntry(index, patch)} onMove={offset => move(index, offset)} onRemove={() => remove(index)} onSync={() => synchronize(entry)} />)}</div>
          {chainError && <div className="notice failed">{chainError}</div>}
          <div className="actions"><button className="primary" disabled={busy || Boolean(chainError)} onClick={preview}>校验并预览</button></div>
        </div>
        {plan && <PlanPanel plan={plan} onDeploy={deploy} />}
        {task && <div className={`notice ${task.status}`}>{task.status.toUpperCase()} · {task.current_ip} · {task.message}</div>}
        {error && <div className="notice failed">{error}</div>}
      </section>
    </section> : <LoginPanel busy={busy} error={error} onLogin={login} />}</main>
  </div>;
}

function LoginPanel({busy, error, onLogin}: {busy: boolean; error: string; onLogin: (body: Parameters<typeof api.createSession>[0]) => void}) {
  const [username, setUsername] = useState('root'); const [mode, setMode] = useState<'password'|'key'>('password');
  const [password, setPassword] = useState(''); const [key, setKey] = useState(''); const [passphrase, setPassphrase] = useState('');
  const submit = (event: React.FormEvent) => { event.preventDefault(); onLogin(mode === 'password' ? {username, password} : {username, private_key: key, passphrase}); setPassword(''); setKey(''); setPassphrase(''); };
  return <form className="login panel" onSubmit={submit}><span className="eyebrow">SECURE SESSION</span><h2>连接物理集群</h2><p>凭证仅保存在 Web 后端内存中，退出、过期或服务重启后立即丢弃。</p><label>SSH User<input value={username} onChange={e => setUsername(e.target.value)} /></label><div className="segmented"><button type="button" className={mode === 'password' ? 'active' : ''} onClick={() => setMode('password')}>Password</button><button type="button" className={mode === 'key' ? 'active' : ''} onClick={() => setMode('key')}>Private Key</button></div>{mode === 'password' ? <label>Password<input type="password" value={password} onChange={e => setPassword(e.target.value)} /></label> : <><label>Private Key<textarea rows={7} value={key} onChange={e => setKey(e.target.value)} /></label><label>Passphrase<input type="password" value={passphrase} onChange={e => setPassphrase(e.target.value)} /></label></>}<button className="primary" disabled={busy}>{busy ? '建立 Session…' : '建立 SSH Session'}</button>{error && <div className="notice failed">{error}</div>}</form>;
}

function NodeList({nodes, onConfirm}: {nodes: NodeInspection[]; onConfirm: (node: NodeInspection) => void}) {
  return <div className="node-list">{nodes.map(node => <article key={node.ip} className="node-row"><div><strong>{node.hostname || node.ip}</strong><small>{node.hostname ? node.ip : node.reachable ? 'SSH reachable' : 'unreachable'}</small></div><div className="node-status">{node.host_key_required ? <button onClick={() => onConfirm(node)}>确认指纹</button> : <span className={`dot ${node.error || !node.reachable ? 'red' : node.hostname ? 'green' : 'amber'}`} />}</div>{node.host_key_required && <code>{node.host_key_fingerprint}</code>}{node.error && <code className="error-text">{node.error}</code>}</article>)}</div>;
}

function AddNode({onAdd}: {onAdd: (ip: string) => Promise<void>}) { const [ip, setIP] = useState(''); return <form className="inline-form" onSubmit={async e => {e.preventDefault(); await onAdd(ip); setIP('');}}><input placeholder="添加自定义 IPv4" value={ip} onChange={e => setIP(e.target.value)} /><button>添加</button></form>; }

function ChainCard({entry,index,total,nodes,usedIPs,onChange,onMove,onRemove,onSync}: {entry: ChainEntry; index:number; total:number; nodes:NodeInspection[]; usedIPs:string[]; onChange:(patch:Partial<ChainEntry>)=>void; onMove:(offset:number)=>void; onRemove:()=>void; onSync:()=>void}) {
  const role = roleAt(index,total); const node = nodes.find(item => item.ip === entry.ip);
  const workers = (node?.workers || []).filter(image => image.architecture === 'arm64' && image.contract.roles.includes(role));
  return <div className="chain-card"><div className="card-head"><span className={`role ${role}`}>{role}</span><div><button disabled={index===0} onClick={()=>onMove(-1)}>↑</button><button disabled={index===total-1} onClick={()=>onMove(1)}>↓</button><button disabled={total<=2} onClick={onRemove}>×</button></div></div><label>Node<select value={entry.ip} onChange={e=>onChange({ip:e.target.value,rdma_device:'',worker_image:''})}><option value="">选择节点</option>{nodes.filter(item=>item.ip===entry.ip||!usedIPs.includes(item.ip)).map(item=><option key={item.ip} value={item.ip}>{item.hostname} · {item.ip}</option>)}</select></label><label>RDMA<select value={entry.rdma_device} onChange={e=>onChange({rdma_device:e.target.value})}><option value="">选择 RDMA 端口</option>{(node?.rdma||[]).filter(item=>item.ipv4).map(item=><option key={rdmaName(item)} value={rdmaName(item)}>{rdmaName(item)} · {item.netdev} · {item.ipv4}</option>)}</select></label><label>Worker<select value={entry.worker_image} onChange={e=>onChange({worker_image:e.target.value})}><option value="">选择本地 Worker</option>{workers.map(image=><option key={image.reference} value={image.reference}>{image.reference.split(':').pop()} · {image.contract.input}→{image.contract.output}</option>)}</select></label><button disabled={!entry.worker_image} onClick={onSync}>从私有源同步更新</button></div>;
}

function PlanPanel({plan,onDeploy}:{plan:DeploymentPlan;onDeploy:()=>void}) { return <div className="panel"><div className="panel-title"><span>Deployment Preview</span><button className="primary" onClick={onDeploy}>一键部署</button></div>{plan.nodes.map(node=><details key={node.ip}><summary><span className={`role ${node.role}`}>{node.role}</span> {node.node_id} · {node.ip} · {node.worker_reference}{node.existing_deployment&&<span className="badge amber">REPLACE</span>}</summary><pre>{node.env_preview}</pre></details>)}</div>; }
