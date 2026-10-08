import type {PreviewFrameData} from './preview';

export function resourceURL(path: string, base = document.baseURI): string {
  return new URL(path, base).href;
}

export function websocketURL(path: string, base = document.baseURI): string {
  const url = new URL(resourceURL(path, base));
  url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:';
  return url.href;
}

export function frameIsFresh(frame: PreviewFrameData | undefined, now: number): boolean {
  const age = now - (frame?.receivedAt ?? 0);
  return !!frame && age >= 0 && age <= 3000;
}
