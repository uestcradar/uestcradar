import { useEffect, useRef, useState } from 'react';
import * as api from './api';
import type { RecordingStatus } from './types';

const names = { idle: '待命', starting: '启动中', recording: '录制中', stopping: '收尾中', failed: '录制失败' };
export function bytesLabel(value: string | null | undefined): string {
  if (value == null || !/^\d+$/.test(value)) return '未上报';
  return `${(Number(value) / 1e9).toFixed(3)} GB`;
}
function Capacity({ title, used, total }: { title: string; used?: string | null; total?: string | null }) {
  const reported = used != null && total != null && /^\d+$/.test(used) && /^\d+$/.test(total) && BigInt(total) > 0n;
  const ratio = reported ? Math.min(1, Number(BigInt(used!) * 10000n / BigInt(total!)) / 10000) : 0;
  return <div className="recording-capacity"><span>{title}</span><span>{reported ? `${bytesLabel(used)} / ${bytesLabel(total)}` : '未上报'}</span>{reported && <meter aria-label={title} min={0} max={1} value={ratio} />}</div>;
}

export function RecordingView({ status, directory, busy, error, onDirectory, onStart, onStop }: {
  status: RecordingStatus | null; directory: string; busy: boolean; error: string;
  onDirectory: (value: string) => void; onStart: () => void; onStop: () => void;
}) {
  const canStart = status && (status.state === 'idle' || status.state === 'failed') && !busy;
  return <section className="recording-panel" aria-label="SignalSink 录制控制">
    <div className="recording-heading"><strong>SignalSink 数据保存</strong><span role="status">{status ? names[status.state] : '状态未知'}</span></div>
    <label className="recording-directory">保存子目录 <input value={directory} maxLength={240} disabled={!canStart} onChange={event => onDirectory(event.target.value)} placeholder="recordings" /></label>
    <div className="recording-actions"><button type="button" disabled={!canStart || !directory} onClick={onStart}>开始录制</button><button type="button" disabled={!status || status.state !== 'recording' || busy} onClick={onStop}>停止录制</button></div>
    <Capacity title="待写录制队列" used={status?.queue_used_bytes} total={status?.queue_capacity_bytes} />
    <Capacity title="目标磁盘已用" used={status?.disk_used_bytes} total={status?.disk_total_bytes} />
    <p>进程可用：{bytesLabel(status?.disk_available_bytes)} · 已写：{bytesLabel(status?.written_bytes)}</p>
    <p>采样连续性未验证；关闭抽屉不停止录制。</p>
    {(error || status?.error) && <p role="alert">{error || status?.error}</p>}
  </section>;
}

export function RecordingPanel({ ip }: { ip: string }) {
  const [status, setStatus] = useState<RecordingStatus | null>(null);
  const [directory, setDirectory] = useState('recordings');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [now, setNow] = useState(() => performance.now());
  const received = useRef(-Infinity);
  const directoryEdited = useRef(false);
  const action = useRef<AbortController | null>(null);
  const query = useRef<AbortController | null>(null);
  const alive = useRef(false);

  function accept(value: RecordingStatus) {
    if (!alive.current) return;
    received.current = performance.now();
    setNow(received.current);
    setStatus(value);
    if (!directoryEdited.current) setDirectory(value.directory);
    setError('');
  }

  useEffect(() => {
    alive.current = true;
    let disposed = false;
    let timer: ReturnType<typeof setTimeout>;
    const clock = setInterval(() => setNow(performance.now()), 500);
    async function poll() {
      if (action.current) { timer = setTimeout(poll, 1000); return; }
      const controller = new AbortController();
      query.current = controller;
      const deadline = setTimeout(() => controller.abort(), 5000);
      try { const value = await api.recordingStatus(ip, controller.signal); if (!disposed && !controller.signal.aborted) accept(value); }
      catch (reason) {
        if (!disposed && !controller.signal.aborted && !action.current) { setStatus(null); setError(String(reason)); }
      } finally {
        clearTimeout(deadline);
        if (query.current === controller) query.current = null;
        if (!disposed) timer = setTimeout(poll, 1000);
      }
    }
    void poll();
    return () => {
      disposed = true;
      alive.current = false;
      clearTimeout(timer);
      clearInterval(clock);
      query.current?.abort();
      action.current?.abort(); // Cancels HTTP only; never sends a recording stop.
    };
  }, [ip]);

  async function operate(stop: boolean) {
    if (action.current || !status || performance.now() - received.current >= 3000) return;
    const controller = new AbortController();
    action.current = controller;
    query.current?.abort();
    setBusy(true);
    const deadline = setTimeout(() => controller.abort(), 5000);
    try {
      const value = stop ? await api.stopRecording(ip, status.recording_id, controller.signal) : await api.startRecording(ip, directory, controller.signal);
      if (!controller.signal.aborted) { directoryEdited.current = false; accept(value); }
    } catch (reason) {
      if (alive.current) { setStatus(null); setError(`操作结果未知，请等待状态刷新：${String(reason)}`); }
    } finally {
      clearTimeout(deadline);
      action.current = null;
      if (alive.current) setBusy(false);
    }
  }

  const fresh = now - received.current < 3000;
  return <RecordingView status={fresh ? status : null} directory={directory} busy={busy} error={error}
    onDirectory={value => { directoryEdited.current = true; setDirectory(value); }}
    onStart={() => void operate(false)} onStop={() => void operate(true)} />;
}
