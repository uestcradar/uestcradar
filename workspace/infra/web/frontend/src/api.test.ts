import { afterEach, expect, it, vi } from 'vitest';
import { addNode, previewPlan, restoreSession, syncFrontend } from './api';
afterEach(() => vi.unstubAllGlobals());
it('always deploys the server chain with strict-RDMA', async () => {
  const fetcher = vi.fn().mockImplementation(() => Promise.resolve(new Response('{}')));
  vi.stubGlobal('fetch', fetcher);
  await previewPlan([], 4, 8388608);
  expect(JSON.parse(fetcher.mock.calls[0][1].body).transport).toBe('strict-rdma');
});
it('restores CSRF from the cookie session for subsequent mutations', async () => {
  const fetcher = vi.fn().mockResolvedValueOnce(new Response(JSON.stringify({csrf_token: 'restored', username: 'root'})))
    .mockResolvedValueOnce(new Response('{}'));
  vi.stubGlobal('fetch', fetcher);
  await restoreSession();
  await addNode('10.0.0.1');
  expect(fetcher.mock.calls[0][0]).toBe('/api/v1/session');
  expect(fetcher.mock.calls[0][1].credentials).toBe('same-origin');
  expect(fetcher.mock.calls[1][1].headers.get('X-CSRF-Token')).toBe('restored');
});
it('synchronizes Frontend through the existing authenticated task API', async () => {
  const fetcher = vi.fn().mockResolvedValueOnce(new Response(JSON.stringify({csrf_token: 'frontend-csrf', username: 'root'})))
    .mockResolvedValueOnce(new Response(JSON.stringify({id: 'frontend-task'})));
  vi.stubGlobal('fetch', fetcher);
  await restoreSession();
  expect(await syncFrontend('192.162.2.64')).toMatchObject({id: 'frontend-task'});
  const [url, options] = fetcher.mock.calls[1];
  expect(url).toBe('/api/v1/orchestration/images/frontend/sync');
  expect(options.method).toBe('POST');
  expect(JSON.parse(options.body)).toEqual({ip: '192.162.2.64'});
  expect(options.headers.get('X-CSRF-Token')).toBe('frontend-csrf');
});
it('reports an expired session as unauthorized', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('SSH session required', {status: 401})));
  await expect(restoreSession()).rejects.toMatchObject({status: 401});
});
