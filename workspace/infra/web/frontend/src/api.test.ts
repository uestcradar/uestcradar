import { afterEach, expect, it, vi } from 'vitest';
import { addNode, restoreSession } from './api';
afterEach(() => vi.unstubAllGlobals());
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
it('reports an expired session as unauthorized', async () => {
  vi.stubGlobal('fetch', vi.fn().mockResolvedValue(new Response('SSH session required', {status: 401})));
  await expect(restoreSession()).rejects.toMatchObject({status: 401});
});
