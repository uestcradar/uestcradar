import {describe, expect, it} from 'vitest';
import {frameIsFresh, resourceURL, websocketURL} from './api';
import type {PreviewFrameData} from './preview';

describe('standalone and embedded paths', () => {
  it('keeps HTTP and WebSocket requests under the current prefix', () => {
    for (const prefix of ['/', '/nodes/operator/frontend/']) {
      const base = `https://example.test${prefix}`;
      expect(resourceURL('api/node', base)).toBe(`${base}api/node`);
      expect(websocketURL('ws/frames', base)).toBe(`wss://example.test${prefix}ws/frames`);
      expect(resourceURL('api/node', `${base}index.html`)).toBe(`${base}api/node`);
    }
    expect(websocketURL('ws', 'http://localhost:8081/')).toBe('ws://localhost:8081/ws');
  });
  it('does not present stale or absent data as live', () => {
    const frame = {receivedAt: 1000} as PreviewFrameData;
    expect(frameIsFresh(undefined, 1000)).toBe(false);
    expect(frameIsFresh(frame, 4000)).toBe(true);
    expect(frameIsFresh(frame, 4001)).toBe(false);
    expect(frameIsFresh(frame, 999)).toBe(false);
  });
});
