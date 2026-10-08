import {describe, expect, it, vi} from 'vitest';
import {renderToStaticMarkup} from 'react-dom/server';
import App, {NodePool} from './App';

vi.mock('./topology', () => ({
  loadTopology: () => ({config: {chain: [{key: 'node', ip: '192.162.2.64', rdma_device: '', worker_image: ''}], slotCount: 4, maxPayloadBytes: 8388608, detailKey: 'node'}}),
  saveTopology: () => undefined,
}));

describe('same-origin Frontend embedding', () => {
  it('offers Frontend update alongside Sidecar and Worker on each inspected node', () => {
    const noop = () => undefined;
    const html = renderToStaticMarkup(<NodePool nodes={[{ip: '192.162.2.64', hostname: 'node4-1', reachable: true, rdma: [], workers: [], existing_deployment: false}]} locked={false} onInspect={noop} onAdd={noop} onJoin={noop} onSidecar={noop} onFrontend={noop} onWorker={noop} onLogin={noop} />);
    expect(html).toContain('更新 Sidecar');
    expect(html).toContain('更新 Worker');
    expect(html).toContain('<button class="node-action frontend">更新 Frontend</button>');
  });
  it('embeds the node page, not the former Web renderer', () => {
    const html = renderToStaticMarkup(<App />);
    expect(html).toContain('src="/api/v1/nodes/192.162.2.64/frontend/"');
    expect(html).toContain('title="192.162.2.64 节点预览"');
    expect(html).not.toContain('<canvas');
    expect(html).not.toContain('主链运输方式');
    expect(html).not.toContain('value="tcp"');
    expect(html).toContain('class="node-frontend"');
    expect(html).not.toContain('height:760');
    expect(html).toContain('<details class="drawer-counters"><summary>高级计数（读写位置）</summary>');
  });
});
