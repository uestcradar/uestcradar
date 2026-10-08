import { renderToStaticMarkup } from 'react-dom/server';
import { afterEach, expect, it, vi } from 'vitest';
import { bytesLabel, RecordingView } from './RecordingPanel';
import { recordingStatus, restoreSession, startRecording, stopRecording } from './api';
import type { RecordingStatus } from './types';

afterEach(() => vi.unstubAllGlobals());
const idle: RecordingStatus = {
  ok: true, state: 'idle', recording_id: 'a'.repeat(32), directory: 'recordings', error: '',
  accepted_frames: '0', written_frames: '0', written_bytes: '0', queue_used_bytes: '0',
  queue_capacity_bytes: '536870912', elapsed_ms: '0', sample_continuity: 'unverified',
  disk_total_bytes: '1000000000', disk_used_bytes: '500000000', disk_available_bytes: '400000000',
};
function render(status: RecordingStatus | null) {
  return renderToStaticMarkup(<RecordingView status={status} directory="recordings" error="" busy={false}
    onDirectory={() => {}} onStart={() => {}} onStop={() => {}} />);
}
it('unknown state shows no fabricated zero meters and disables mutations', () => {
  const html = render(null);
  expect(html).toContain('状态未知');
  expect(html).toContain('未上报');
  expect(html).not.toContain('<meter');
  expect(html.match(/<button[^>]*disabled/g)).toHaveLength(2);
  expect(bytesLabel(null)).toBe('未上报');
});
it('shows actual disk and recording queue separately from preview', () => {
  const html = render(idle);
  expect(html).toContain('待写录制队列');
  expect(html).toContain('目标磁盘已用');
  expect(html).toContain('0.400 GB');
  expect(html).toContain('采样连续性未验证');
  expect(html).not.toContain('<iframe');
  expect(html.match(/<button[^>]*disabled/g)).toHaveLength(1);
});
it('starting and finalizing prevent duplicate operations', () => {
  for (const state of ['starting', 'stopping'] as const) {
    expect(render({ ...idle, state }).match(/<button[^>]*disabled/g)).toHaveLength(2);
  }
});
it('uses existing authentication, CSRF and bounded-target operation routes', async () => {
  const fetcher = vi.fn().mockResolvedValueOnce(new Response(JSON.stringify({csrf_token: 'capture-csrf'})))
    .mockImplementation(() => Promise.resolve(new Response(JSON.stringify({...idle, written_bytes:'18446744073709551615'}))));
  vi.stubGlobal('fetch', fetcher);
  await restoreSession();
  const controller = new AbortController();
  expect((await recordingStatus('192.162.2.64', controller.signal)).written_bytes).toBe('18446744073709551615');
  await startRecording('192.162.2.64', 'run-a');
  await stopRecording('192.162.2.64', idle.recording_id);
  expect(fetcher.mock.calls[1][1].signal).toBe(controller.signal);
  expect(fetcher.mock.calls[2][1].headers.get('X-CSRF-Token')).toBe('capture-csrf');
  expect(JSON.parse(fetcher.mock.calls[2][1].body)).toEqual({ip:'192.162.2.64',directory:'run-a'});
  expect(JSON.parse(fetcher.mock.calls[3][1].body)).toEqual({ip:'192.162.2.64',recording_id:idle.recording_id});
});
