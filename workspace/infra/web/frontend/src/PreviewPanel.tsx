import { useEffect, useMemo, useRef, useState } from 'react';
import type { ContractRef, PreviewFrameData, PreviewSelector, PreviewStatusData, WaveformChannelData } from './preview';
import { adaptiveWaveformPeak, buildSubscription, decodePreviewMessage, waveformXAxisLabel } from './preview';
import { rdColor, rdPixels } from './rdHeatmap';

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
        if (stopped) return;
        setConnection('connected');
        socket?.send(buildSubscription(selectors));
      };
      socket.onmessage = event => {
        if (stopped || !(event.data instanceof ArrayBuffer)) return;
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
        if (stopped) return;
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
      <div><strong>{contract?.typeId === '3' ? (leg === 'input' ? '输入 RD 图' : '输出 RD 图') : title}</strong><span>{contract ? `Type ${contract.typeId}:${contract.typeVersion}` : '该角色无此 Leg'}</span></div>
      {channels.length > 1 && <label>通道<select value={channel} onChange={event => setChannel(Number(event.target.value))}>{channels.map(item => <option key={item.channelIndex} value={item.channelIndex}>CH {item.channelIndex}</option>)}</select></label>}
    </div>
    {!contract ? <div className="preview-empty">当前 Worker 契约未声明该方向的数据类型。</div> : !frame ? <div className="preview-empty">等待 Sidecar 预览帧。</div> : frame.kind === 'waveform' ? <WaveformCanvas channel={channels.find(item => item.channelIndex === channel) || channels[0]} originalColumns={frame.originalColumns} xAxisLabel={waveformXAxisLabel(frame.typeId)} /> : <HeatmapCanvas frame={frame} />}
    <div className="preview-stats">
      <span>实际 <strong>{status?.actualFps.toFixed(1) || '0.0'} fps</strong></span>
      <span>快照丢弃 <strong>{status?.snapshotDrops || 0}</strong></span>
      <span>编码丢弃 <strong>{status?.encodeDrops || 0}</strong></span>
      <span>网络丢弃 <strong>{status?.networkDrops || 0}</strong></span>
    </div>
  </article>;
}

function formatAxisValue(value: number): string {
  if (value === 0) return '0';
  const absolute = Math.abs(value);
  if (absolute >= 10_000 || absolute < 0.01) return value.toExponential(1);
  if (absolute >= 100) return value.toFixed(0);
  if (absolute >= 10) return value.toFixed(1);
  return value.toFixed(2);
}

function WaveformCanvas({channel, originalColumns, xAxisLabel}: {channel?: WaveformChannelData; originalColumns: number; xAxisLabel: string}) {
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
      context.setTransform(ratio, 0, 0, ratio, 0, 0);
      context.fillStyle = '#fbfdff';
      context.fillRect(0, 0, width, height);

      const plot = {left: 62, right: width - 14, top: 12, bottom: height - 36};
      const plotWidth = Math.max(1, plot.right - plot.left);
      const plotHeight = Math.max(1, plot.bottom - plot.top);
      const peak = adaptiveWaveformPeak(channel);

      context.font = '9px "SFMono-Regular", Consolas, monospace';
      context.fillStyle = '#64748b';
      context.lineWidth = 1;
      context.textBaseline = 'middle';
      for (let index = 0; index <= 4; index += 1) {
        const fraction = index / 4;
        const y = plot.bottom - fraction * plotHeight;
        context.strokeStyle = index === 0 ? '#94a3b8' : '#e2e8f0';
        context.beginPath();
        context.moveTo(plot.left, y);
        context.lineTo(plot.right, y);
        context.stroke();
        context.textAlign = 'right';
        context.fillText(formatAxisValue(peak * fraction), plot.left - 7, y);
      }
      context.textBaseline = 'top';
      for (let index = 0; index <= 4; index += 1) {
        const fraction = index / 4;
        const x = plot.left + fraction * plotWidth;
        context.strokeStyle = index === 0 ? '#94a3b8' : '#eef2f7';
        context.beginPath();
        context.moveTo(x, plot.top);
        context.lineTo(x, plot.bottom);
        context.stroke();
        context.textAlign = index === 0 ? 'left' : index === 4 ? 'right' : 'center';
        const column = Math.round(Math.max(0, originalColumns - 1) * fraction);
        context.fillText(column.toLocaleString('en-US'), x, plot.bottom + 6);
      }

      context.fillStyle = '#475569';
      context.font = '10px sans-serif';
      context.textAlign = 'center';
      context.textBaseline = 'bottom';
      context.fillText(xAxisLabel, plot.left + plotWidth / 2, height - 2);
      context.save();
      context.translate(11, plot.top + plotHeight / 2);
      context.rotate(-Math.PI / 2);
      context.fillText('幅度 |I+jQ|', 0, 0);
      context.restore();

      context.save();
      context.beginPath();
      context.rect(plot.left, plot.top, plotWidth, plotHeight);
      context.clip();
      const plotWaveform = (points: WaveformChannelData['maximum'], color: string, alpha: number) => {
        context.beginPath();
        context.strokeStyle = color;
        context.globalAlpha = alpha;
        context.lineWidth = 1.5;
        let started = false;
        points.forEach((point, index) => {
          if (!Number.isFinite(point.x) || !Number.isFinite(point.magnitude)) {
            started = false;
            return;
          }
          const x = originalColumns <= 1 ? plot.left : plot.left + point.x / (originalColumns - 1) * plotWidth;
          const normalized = Math.max(0, Math.min(1, point.magnitude / peak));
          const y = plot.bottom - normalized * plotHeight;
          if (!started || index === 0) context.moveTo(x, y); else context.lineTo(x, y);
          started = true;
        });
        context.stroke();
      };
      plotWaveform(channel.minimum, '#94a3b8', .75);
      plotWaveform(channel.maximum, '#2563eb', 1);
      context.globalAlpha = 1;
      context.restore();
    };
    const observer = new ResizeObserver(draw);
    observer.observe(element);
    draw();
    return () => observer.disconnect();
  }, [channel, originalColumns, xAxisLabel]);
  return <canvas className="preview-canvas" ref={canvas} />;
}

function HeatmapCanvas({frame}: {frame: PreviewFrameData}) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const [limits, setLimits] = useState([1, 1e8]);
  const [low, setLow] = useState('1');
  const [high, setHigh] = useState('100000000');
  const [error, setError] = useState('');
  const [now, setNow] = useState(Date.now());
  const [size, setSize] = useState(600);
  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(timer);
  }, []);
  useEffect(() => {
    if (!canvas.current) return;
    const observer = new ResizeObserver(entries => setSize(entries[0].contentRect.width));
    observer.observe(canvas.current);
    return () => observer.disconnect();
  }, []);
  useEffect(() => {
    const element = canvas.current;
    const heatmap = frame.heatmap;
    if (!element || !heatmap) return;
    const width = Math.max(300, size);
    const height = 280;
    element.width = width;
    element.height = height;
    const context = element.getContext('2d');
    if (!context) return;
    const left = 50, top = 25, plotWidth = Math.floor(width-130), plotHeight = 210;
    const grid = rdPixels(heatmap.values, frame.poolRows, frame.poolColumns, plotWidth, limits[0], limits[1]);
    const offscreen = document.createElement('canvas');
    offscreen.width = grid.width; offscreen.height = grid.height;
    offscreen.getContext('2d')!.putImageData(new ImageData(grid.pixels, grid.width, grid.height), 0, 0);
    context.fillStyle = '#fff'; context.fillRect(0,0,width,height);
    context.imageSmoothingEnabled = false;
    context.drawImage(offscreen,left,top,plotWidth,plotHeight);
    context.font = '10px sans-serif'; context.fillStyle = '#334155';
    context.fillText('多普勒 Bin', 3, 14);
    for (let i=0; i<=4; i++) {
      context.textAlign = 'center';
      context.fillText(String(Math.round(i*(frame.originalRows-1)/4)), left+i*plotWidth/4, top+plotHeight+16);
      context.textAlign = 'right';
      context.fillText(String(Math.round(i*(frame.originalColumns-1)/4)), left-5, top+plotHeight-i*plotHeight/4+3);
    }
    context.textAlign = 'center'; context.fillText('距离 Bin',left+plotWidth/2,height-6);
    const barX = left+plotWidth+12;
    for (let y=0; y<plotHeight; y++) {
      const value = 10**(Math.log10(limits[1]) - y/(plotHeight-1)*(Math.log10(limits[1])-Math.log10(limits[0])));
      const color = rdColor(value, limits[0], limits[1]);
      context.fillStyle = `rgb(${color[0]},${color[1]},${color[2]})`;
      context.fillRect(barX,top+y,12,1);
    }
    context.fillStyle='#334155'; context.textAlign='left';
    context.fillText('强度 (log)',barX-3,14);
    for (let i=0;i<=4;i++) {
      const value=10**(Math.log10(limits[1])-i/4*(Math.log10(limits[1])-Math.log10(limits[0])));
      context.fillText(value.toExponential(0),barX+16,top+i*plotHeight/4+3);
    }
  }, [frame, limits, size]);
  return <div style={{padding:'4px'}}>
    <form style={{display:'flex',gap:6,flexWrap:'wrap',fontSize:11}} onSubmit={event => {
      event.preventDefault();
      const a=Number(low), b=Number(high);
      if (!Number.isFinite(a) || !Number.isFinite(b) || a<=0 || a>=b) {setError('色标必须满足 0 < 下限 < 上限'); return;}
      setLimits([a,b]); setError('');
    }}>
      <label>下限 <input aria-label="RD 色标下限" style={{width:80}} value={low} onChange={e=>setLow(e.target.value)} /></label>
      <label>上限 <input aria-label="RD 色标上限" style={{width:100}} value={high} onChange={e=>setHigh(e.target.value)} /></label>
      <button type="submit">应用</button>
      <button type="button" onClick={()=>{setLow('1');setHigh('100000000');setLimits([1,1e8]);setError('');}}>重置</button>
    </form>
    {error && <div role="alert">{error}</div>}
    <div style={{fontSize:10,padding:'6px 0'}}>{frame.originalRows} 距离 × {frame.originalColumns} 多普勒 · 帧 {frame.frameId} · 距离步长 {frame.heatmap?.rangeStride}
      {frame.heatmap?.legacy && ' · 旧版压缩预览'}
      {' · 距接收 '}{Math.max(0, (now-(frame.receivedAt || now))/1000).toFixed(1)} 秒
    </div>
    <canvas className="preview-canvas heatmap" style={{height:280}} ref={canvas} />
  </div>;
}

function connectionLabel(state: ConnectionState) {
  if (state === 'connected') return '预览通道已连接';
  if (state === 'connecting') return '正在连接预览通道';
  if (state === 'unavailable') return '无可预览 Leg';
  return '预览通道重连中';
}
