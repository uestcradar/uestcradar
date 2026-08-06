import { describe, expect, it } from 'vitest';
import { flatbuffers } from 'flatbuffers';
import { uestcradar } from './generated/preview_generated';
import { adaptiveWaveformPeak, buildSubscription, decodeHalf, decodePreviewMessage, parseContract, waveformXAxisLabel } from './preview';

const fb = uestcradar.preview;

describe('preview protocol', () => {
  it('parses only valid worker contracts', () => {
    expect(parseContract('1:2')).toEqual({typeId: '1', typeVersion: 2});
    expect(parseContract('none')).toBeUndefined();
    expect(parseContract('1')).toBeUndefined();
  });

  it('builds independent multi-node and multi-leg selectors', () => {
    const bytes = buildSubscription([
      {subscriptionId: 1, nodeId: 'node-a', leg: 'input', typeId: '1', typeVersion: 2, requestedFps: 15},
      {subscriptionId: 2, nodeId: 'node-b', leg: 'output', typeId: '2', typeVersion: 2, requestedFps: 30},
    ], 7);
    const buffer = new flatbuffers.ByteBuffer(bytes);
    expect(fb.PreviewMessage.bufferHasIdentifier(buffer)).toBe(true);
    const message = fb.PreviewMessage.getRootAsPreviewMessage(buffer);
    const update = message.payload(new fb.SubscriptionUpdate());
    expect(update?.selectorsLength()).toBe(2);
    expect(update?.selectors(0)?.nodeId()).toBe('node-a');
    expect(update?.selectors(0)?.leg()).toBe(fb.Leg.Input);
    expect(update?.selectors(1)?.nodeId()).toBe('node-b');
    expect(update?.selectors(1)?.leg()).toBe(fb.Leg.Output);
  });

  it('decodes finite, subnormal and infinite float16 values', () => {
    expect(decodeHalf(0x3c00)).toBe(1);
    expect(decodeHalf(0xc000)).toBe(-2);
    expect(decodeHalf(0x0001)).toBeGreaterThan(0);
    expect(decodeHalf(0x7c00)).toBe(Number.POSITIVE_INFINITY);
  });

  it('uses the actual finite waveform range for adaptive display', () => {
    const point = (magnitude: number) => ({x: 0, i: magnitude, q: 0, magnitude});
    const channel = {
      channelIndex: 0,
      minimum: [point(4e-7), point(Number.NaN)],
      maximum: [point(0.003), point(0.006954)],
    };
    expect(adaptiveWaveformPeak(channel)).toBeCloseTo(0.006954);
    expect(adaptiveWaveformPeak()).toBe(Number.EPSILON);
    expect(waveformXAxisLabel('1')).toBe('采样点');
    expect(waveformXAxisLabel('2')).toBe('距离 Bin');
  });

  it('keeps waveform channels isolated while decoding', () => {
    const builder = new flatbuffers.Builder(512);
    const channelOffsets = [
      {index: 0, scale: 10, values: [1, 0, 2, 0]},
      {index: 1, scale: 100, values: [3, 0, 4, 0]},
    ].map(channel => {
      const minimum = fb.WaveformChannel.createMinOffsetsVector(builder, [5]);
      const maximum = fb.WaveformChannel.createMaxOffsetsVector(builder, [9]);
      const values = fb.WaveformChannel.createValuesVector(builder, channel.values);
      return fb.WaveformChannel.createWaveformChannel(
        builder, channel.index, 1, channel.scale, minimum, maximum, values,
      );
    });
    const channels = fb.WaveformPreview.createChannelsVector(builder, channelOffsets);
    const waveform = fb.WaveformPreview.createWaveformPreview(builder, channels);
    const node = builder.createString('node-a');
    const instance = builder.createString('instance-a');
    const frame = fb.PreviewFrame.createPreviewFrame(
      builder, node, instance, fb.Leg.Output,
      builder.createLong(1, 0), 2, builder.createLong(77, 0), builder.createLong(88, 0),
      2, 128, 2, 1, fb.ValueEncoding.ComplexInt8, 0,
      fb.PreviewBody.WaveformPreview, waveform,
    );
    const message = fb.PreviewMessage.createPreviewMessage(
      builder, 1, fb.MessagePayload.PreviewFrame, frame,
    );
    fb.PreviewMessage.finishPreviewMessageBuffer(builder, message);
    const decoded = decodePreviewMessage(builder.asUint8Array());
    expect(decoded.kind).toBe('waveform');
    if (decoded.kind !== 'waveform') return;
    expect(decoded.channels?.map(channel => channel.channelIndex)).toEqual([0, 1]);
    expect(decoded.channels?.[0].maximum[0].magnitude).toBe(20);
    expect(decoded.channels?.[1].maximum[0].magnitude).toBe(400);
  });
});
