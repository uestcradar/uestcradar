import { flatbuffers } from 'flatbuffers';
import { uestcradar } from './generated/preview_generated';

const fb = uestcradar.preview;

export interface ContractRef {
  typeId: string;
  typeVersion: number;
}

export interface PreviewSelector extends ContractRef {
  subscriptionId: number;
  nodeId: string;
  leg: 'input' | 'output';
  requestedFps: number;
}

export interface ComplexPoint {
  x: number;
  i: number;
  q: number;
  magnitude: number;
}

export interface WaveformChannelData {
  channelIndex: number;
  minimum: ComplexPoint[];
  maximum: ComplexPoint[];
}

export interface PreviewFrameData {
  kind: 'waveform' | 'heatmap';
  nodeId: string;
  leg: 'input' | 'output';
  typeId: string;
  typeVersion: number;
  frameId: number;
  originalRows: number;
  originalColumns: number;
  poolRows: number;
  poolColumns: number;
  channels?: WaveformChannelData[];
  heatmap?: { channelIndex: number; values: Float32Array };
}

export interface PreviewStatusData {
  kind: 'status';
  nodeId: string;
  leg: 'input' | 'output';
  typeId: string;
  typeVersion: number;
  actualFps: number;
  snapshotDrops: number;
  encodeDrops: number;
  networkDrops: number;
}

export type PreviewData = PreviewFrameData | PreviewStatusData;

export function parseContract(value?: string): ContractRef | undefined {
  if (!value || value === 'none') return undefined;
  const match = /^(\d+):(\d+)$/.exec(value.trim());
  if (!match || BigInt(match[1]) === 0n || Number(match[2]) <= 0) return undefined;
  return {typeId: match[1], typeVersion: Number(match[2])};
}

function longFromString(builder: flatbuffers.Builder, value: string | number) {
  const integer = BigInt(value);
  return builder.createLong(
    Number(integer & 0xffffffffn),
    Number((integer >> 32n) & 0xffffffffn),
  );
}

function longToString(value: flatbuffers.Long): string {
  const integer = (BigInt(value.high >>> 0) << 32n) | BigInt(value.low >>> 0);
  return integer.toString();
}

function longToNumber(value: flatbuffers.Long): number {
  return Number((BigInt(value.high >>> 0) << 32n) | BigInt(value.low >>> 0));
}

export function buildSubscription(selectors: PreviewSelector[], version = Date.now()): Uint8Array {
  const builder = new flatbuffers.Builder(512);
  const offsets = selectors.map(selector => {
    const node = builder.createString(selector.nodeId);
    return fb.StreamSelector.createStreamSelector(
      builder,
      longFromString(builder, selector.subscriptionId),
      node,
      selector.leg === 'input' ? fb.Leg.Input : fb.Leg.Output,
      longFromString(builder, selector.typeId),
      selector.typeVersion,
      Math.max(0.1, Math.min(30, selector.requestedFps)),
    );
  });
  const vector = fb.SubscriptionUpdate.createSelectorsVector(builder, offsets);
  const update = fb.SubscriptionUpdate.createSubscriptionUpdate(
    builder,
    longFromString(builder, version),
    vector,
  );
  const message = fb.PreviewMessage.createPreviewMessage(
    builder,
    1,
    fb.MessagePayload.SubscriptionUpdate,
    update,
  );
  fb.PreviewMessage.finishPreviewMessageBuffer(builder, message);
  return builder.asUint8Array();
}

export function decodeHalf(bits: number): number {
  const sign = (bits & 0x8000) ? -1 : 1;
  const exponent = (bits >>> 10) & 0x1f;
  const fraction = bits & 0x3ff;
  if (exponent === 0) return sign * Math.pow(2, -14) * (fraction / 1024);
  if (exponent === 0x1f) return fraction ? Number.NaN : sign * Number.POSITIVE_INFINITY;
  return sign * Math.pow(2, exponent - 15) * (1 + fraction / 1024);
}

function legName(value: uestcradar.preview.Leg): 'input' | 'output' {
  if (value === fb.Leg.Input) return 'input';
  if (value === fb.Leg.Output) return 'output';
  throw new Error('Preview message has an invalid Leg');
}

function signedByte(value: number): number {
  return value > 127 ? value - 256 : value;
}

function waveformPoint(
  values: Uint8Array,
  offset: number,
  x: number,
  encoding: uestcradar.preview.ValueEncoding,
  scale: number,
): ComplexPoint {
  let i: number;
  let q: number;
  if (encoding === fb.ValueEncoding.ComplexInt8) {
    i = signedByte(values[offset]) * scale;
    q = signedByte(values[offset + 1]) * scale;
  } else if (encoding === fb.ValueEncoding.ComplexFloat16) {
    i = decodeHalf(values[offset] | (values[offset + 1] << 8));
    q = decodeHalf(values[offset + 2] | (values[offset + 3] << 8));
  } else {
    throw new Error('Unsupported waveform encoding');
  }
  return {x, i, q, magnitude: Math.hypot(i, q)};
}

function decodeWaveform(frame: uestcradar.preview.PreviewFrame): WaveformChannelData[] {
  const body = frame.body(new fb.WaveformPreview());
  if (!body) throw new Error('Preview waveform body is missing');
  const bytesPerPoint = frame.encoding() === fb.ValueEncoding.ComplexInt8 ? 2 : 4;
  const channels: WaveformChannelData[] = [];
  for (let index = 0; index < body.channelsLength(); index += 1) {
    const channel = body.channels(index);
    if (!channel) continue;
    const values = channel.valuesArray();
    const minimumOffsets = channel.minOffsetsArray();
    const maximumOffsets = channel.maxOffsetsArray();
    if (!values || !minimumOffsets || !maximumOffsets) continue;
    const minimum: ComplexPoint[] = [];
    const maximum: ComplexPoint[] = [];
    for (let bucket = 0; bucket < channel.bucketCount(); bucket += 1) {
      const base = bucket * bytesPerPoint * 2;
      minimum.push(waveformPoint(
        values,
        base,
        bucket * 128 + minimumOffsets[bucket],
        frame.encoding(),
        channel.scale(),
      ));
      maximum.push(waveformPoint(
        values,
        base + bytesPerPoint,
        bucket * 128 + maximumOffsets[bucket],
        frame.encoding(),
        channel.scale(),
      ));
    }
    channels.push({channelIndex: channel.channelIndex(), minimum, maximum});
  }
  return channels;
}

function decodeHeatmap(frame: uestcradar.preview.PreviewFrame) {
  const body = frame.body(new fb.HeatmapPreview());
  if (!body) throw new Error('Preview heatmap body is missing');
  const bytes = body.valuesArray();
  if (!bytes) throw new Error('Preview heatmap values are missing');
  const values = new Float32Array(body.rows() * body.columns());
  for (let index = 0; index < values.length; index += 1) {
    values[index] = decodeHalf(bytes[index * 2] | (bytes[index * 2 + 1] << 8));
  }
  return {channelIndex: body.channelIndex(), values};
}

export function decodePreviewMessage(data: ArrayBuffer | Uint8Array): PreviewData {
  const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
  const buffer = new flatbuffers.ByteBuffer(bytes);
  if (!fb.PreviewMessage.bufferHasIdentifier(buffer)) throw new Error('Invalid preview identifier');
  const message = fb.PreviewMessage.getRootAsPreviewMessage(buffer);
  if (message.protocolVersion() !== 1) throw new Error('Unsupported preview protocol');
  if (message.payloadType() === fb.MessagePayload.StreamStatus) {
    const status = message.payload(new fb.StreamStatus());
    if (!status) throw new Error('Preview status is missing');
    return {
      kind: 'status',
      nodeId: String(status.nodeId() || ''),
      leg: legName(status.leg()),
      typeId: longToString(status.frameTypeId()),
      typeVersion: status.frameTypeVersion(),
      actualFps: status.actualFps(),
      snapshotDrops: longToNumber(status.snapshotDrops()),
      encodeDrops: longToNumber(status.encodeDrops()),
      networkDrops: longToNumber(status.networkDrops()),
    };
  }
  if (message.payloadType() !== fb.MessagePayload.PreviewFrame) throw new Error('Unexpected preview payload');
  const frame = message.payload(new fb.PreviewFrame());
  if (!frame) throw new Error('Preview frame is missing');
  const common = {
    nodeId: String(frame.nodeId() || ''),
    leg: legName(frame.leg()),
    typeId: longToString(frame.frameTypeId()),
    typeVersion: frame.frameTypeVersion(),
    frameId: longToNumber(frame.frameId()),
    originalRows: frame.originalRows(),
    originalColumns: frame.originalColumns(),
    poolRows: frame.poolRows(),
    poolColumns: frame.poolColumns(),
  };
  if (frame.bodyType() === fb.PreviewBody.WaveformPreview) {
    return {kind: 'waveform', ...common, channels: decodeWaveform(frame)};
  }
  if (frame.bodyType() === fb.PreviewBody.HeatmapPreview) {
    return {kind: 'heatmap', ...common, heatmap: decodeHeatmap(frame)};
  }
  throw new Error('Unsupported preview body');
}
