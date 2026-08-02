import { useEffect, useMemo, useRef, useState } from 'react';
import type { ContractRef, PreviewFrameData, PreviewSelector, PreviewStatusData, WaveformChannelData } from './preview';
import { buildSubscription, decodePreviewMessage } from './preview';

interface PreviewPanelProps {
  nodeId?: string;
  input?: ContractRef;
  output?: ContractRef;
}

type ConnectionState = 'connecting' | 'connected' | 'closed' | 'unavailable';

export function PreviewPanel({nodeId, input, output}: PreviewPanelProps) {
  const [connection, setConnection] = useState<ConnectionState>('connecting');
  const [frames, setFrames] = useState<Partial<Record<'input' | 'output', PreviewFrameData>>>({});
  const [statuses, setStatuses] = useState<Partial<Record<'input' | 'output', PreviewStatusData>>>({});
  const selectors = useMemo(() => {
    if (!nodeId) return [];
    const available = ([['input', input], ['output', output]] as const).filter((item): item is readonly ['input' | 'output', ContractRef] => Boolean(item[1]));
    const requestedFps = available.length > 1 ? 15 : 30;
    return available.map(([leg, contract], index): PreviewSelector => ({
      subscriptionId: index + 1,
      nodeId,
      leg,
      typeId: contract.typeId,
      typeVersion: contract.typeVersion,
      requestedFps,
    }));
  }, [input?.typeId, input?.typeVersion, nodeId, output?.typeId, output?.typeVersion]);

  useEffect(() => {
    setFrames({});
    setStatuses({});
    if (!selectors.length) {
      setConnection('unavailable');
      return;
    }
    let socket: WebSocket | undefined;
    let retryTimer = 0;
    let stopped = false;
    const connect = () => {
      if (stopped) return;
      setConnection('connecting');
      const scheme = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      socket = new WebSocket(`${scheme}//${window.location.host}/ws/frames`);
      socket.binaryType = 'arraybuffer';
      socket.onopen = () => {
        setConnection('connected');
        socket?.send(buildSubscription(selectors));
      };
      socket.onmessage = event => {
        if (!(event.data instanceof ArrayBuffer)) return;
        try {
          const decoded = decodePreviewMessage(event.data);
          if (decoded.nodeId !== nodeId) return;
          if (decoded.kind === 'status') {
            setStatuses(current => ({...current, [decoded.leg]: decoded}));
          } else {
            setFrames(current => ({...current, [decoded.leg]: decoded}));
          }
        } catch {
          // The backend already validates frames; ignore a version-skewed frame.
        }
      };
      socket.onclose = () => {
        setConnection('closed');
        if (!stopped) retryTimer = window.setTimeout(connect, 1500);
      };
    };
    connect();
    return () => {
      stopped = true;
      window.clearTimeout(retryTimer);
      socket?.close();
    };
  }, [nodeId, selectors]);

  return <section className="preview-section">
    <div className="preview-heading">
      <div><span className="section-kicker">LOSSY LIVE PREVIEW</span><h3>输入 / 输出实时预览</h3></div>
      <span className={`preview-connection ${connection}`}>{connectionLabel(connection)}</span>
    </div>
    <p className="preview-note">仅在当前抽屉打开时订阅；双 Leg 合计最高 30 fps。预览资源不足会直接丢帧，不反向阻塞数据面。</p>
    <PreviewCard title="输入波形" leg="input" contract={input} frame={frames.input} status={statuses.input} />
    <PreviewCard title="输出波形" leg="output" contract={output} frame={frames.output} status={statuses.output} />
  </section>;
}

function PreviewCard({title, leg, contract, frame, status}: {title: string; leg: 'input' | 'output'; contract?: ContractRef; frame?: PreviewFrameData; status?: PreviewStatusData}) {
  const [channel, setChannel] = useState(0);
  const channels = frame?.channels || [];
  useEffect(() => {
    if (channels.length && !channels.some(item => item.channelIndex === channel)) setChannel(channels[0].channelIndex);
  }, [channel, channels]);
  return <article className={`preview-card ${leg}`}>
    <div className="preview-card-head">
      <div><strong>{title}</strong><span>{contract ? `Type ${contract.typeId}:${contract.typeVersion}` : '该角色无此 Leg'}</span></div>
      {channels.length > 1 && <label>通道<select value={channel} onChange={event => setChannel(Number(event.target.value))}>{channels.map(item => <option key={item.channelIndex} value={item.channelIndex}>CH {item.channelIndex}</option>)}</select></label>}
    </div>
    {!contract ? <div className="preview-empty">当前 Worker 契约未声明该方向的数据类型。</div> : !frame ? <div className="preview-empty">等待 Sidecar 预览帧。</div> : frame.kind === 'waveform' ? <WaveformCanvas channel={channels.find(item => item.channelIndex === channel) || channels[0]} originalColumns={frame.originalColumns} /> : <HeatmapCanvas frame={frame} />}
    <div className="preview-stats">
      <span>实际 <strong>{status?.actualFps.toFixed(1) || '0.0'} fps</strong></span>
      <span>快照丢弃 <strong>{status?.snapshotDrops || 0}</strong></span>
      <span>编码丢弃 <strong>{status?.encodeDrops || 0}</strong></span>
      <span>网络丢弃 <strong>{status?.networkDrops || 0}</strong></span>
    </div>
  </article>;
}

function WaveformCanvas({channel, originalColumns}: {channel?: WaveformChannelData; originalColumns: number}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const element = canvas.current;
    if (!element || !channel) return;
    const draw = () => {
      const width = Math.max(300, element.clientWidth);
      const height = Math.max(150, element.clientHeight);
      const ratio = window.devicePixelRatio || 1;
      element.width = width * ratio;
      element.height = height * ratio;
      const context = element.getContext('2d');
      if (!context) return;
      context.scale(ratio, ratio);
      context.fillStyle = '#fbfdff';
      context.fillRect(0, 0, width, height);
      context.strokeStyle = '#e2e8f0';
      context.lineWidth = 1;
      for (let index = 1; index < 4; index += 1) {
        context.beginPath(); context.moveTo(0, height * index / 4); context.lineTo(width, height * index / 4); context.stroke();
      }
      const peak = channel.maximum.reduce(
        (maximum, point) => Math.max(maximum, point.magnitude), 1,
      );
      const plot = (points: WaveformChannelData['maximum'], color: string, alpha: number) => {
        context.beginPath();
        context.strokeStyle = color;
        context.globalAlpha = alpha;
        context.lineWidth = 1.5;
        points.forEach((point, index) => {
          const x = originalColumns <= 1 ? 0 : point.x / (originalColumns - 1) * width;
          const y = height - point.magnitude / peak * (height - 10) - 5;
          if (index === 0) context.moveTo(x, y); else context.lineTo(x, y);
        });
        context.stroke();
      };
      plot(channel.minimum, '#94a3b8', .75);
      plot(channel.maximum, '#2563eb', 1);
      context.globalAlpha = 1;
    };
    const observer = new ResizeObserver(draw);
    observer.observe(element);
    draw();
    return () => observer.disconnect();
  }, [channel, originalColumns]);
  return <canvas className="preview-canvas" ref={canvas} />;
}

function HeatmapCanvas({frame}: {frame: PreviewFrameData}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const element = canvas.current;
    const heatmap = frame.heatmap;
    if (!element || !heatmap) return;
    const width = Math.max(300, element.clientWidth);
    const height = Math.max(150, element.clientHeight);
    element.width = width;
    element.height = height;
    const context = element.getContext('2d');
    if (!context) return;
    let peak = 1e-12;
    for (const value of heatmap.values) peak = Math.max(peak, Math.abs(value));
    const cellWidth = width / frame.poolColumns;
    const cellHeight = height / frame.poolRows;
    heatmap.values.forEach((value, index) => {
      const normalized = Math.min(1, Math.abs(value) / peak);
      const hue = 220 - normalized * 180;
      context.fillStyle = `hsl(${hue} 85% ${92 - normalized * 48}%)`;
      context.fillRect(index % frame.poolColumns * cellWidth, Math.floor(index / frame.poolColumns) * cellHeight, Math.ceil(cellWidth), Math.ceil(cellHeight));
    });
  }, [frame]);
  return <canvas className="preview-canvas heatmap" ref={canvas} />;
}

function connectionLabel(state: ConnectionState) {
  if (state === 'connected') return '预览通道已连接';
  if (state === 'connecting') return '正在连接预览通道';
  if (state === 'unavailable') return '无可预览 Leg';
  return '预览通道重连中';
}
