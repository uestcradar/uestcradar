import {describe, expect, it} from 'vitest';
import {flatbuffers} from 'flatbuffers';
import {uestcradar} from './generated/preview_generated';
import {decodePreviewMessage} from './preview';
const fb = uestcradar.preview;
function frame(channels: number, bad = '') {
  const b = new flatbuffers.Builder(1024), count = 8192;
  const rows = Array.from({length: channels}, (_, channel) => {
    const bytes = new Uint8Array(count * 4 - (bad === 'length' ? 1 : 0));
    const view = new DataView(bytes.buffer);
    for (let i = 0; i < Math.floor(bytes.length / 4); i++) {
      view.setInt16(i * 4, (i + channel * 8192) % 65536 - 32768, true);
      view.setInt16(i * 4 + 2, 32767 - (i + channel * 8192) % 65536, true);
    }
    const values = fb.WaveformChannel.createValuesVector(b, bytes);
    const offsets = bad === 'offsets' ? fb.WaveformChannel.createMinOffsetsVector(b, [0]) : 0;
    return fb.WaveformChannel.createWaveformChannel(b, bad === 'duplicate' ? 0 : channel, count, bad === 'scale' ? 2 : 1, offsets, 0, values);
  });
  const vector = fb.WaveformPreview.createChannelsVector(b, rows);
  const body = fb.WaveformPreview.createWaveformPreview(b, vector);
  const node = b.createString('node-a'), instance = b.createString('instance-a');
  const f = fb.PreviewFrame.createPreviewFrame(b, node, instance, fb.Leg.Output,
    b.createLong(bad === 'contract' ? 1 : 4, 0), 1, b.createLong(17, 0), b.createLong(23, 0),
    channels, count, channels, bad === 'dimensions' ? count - 1 : count,
    fb.ValueEncoding.ComplexInt16, 0, fb.PreviewBody.WaveformPreview, body);
  const m = fb.PreviewMessage.createPreviewMessage(b, 1, fb.MessagePayload.PreviewFrame, f);
  fb.PreviewMessage.finishPreviewMessageBuffer(b, m);
  return b.asUint8Array();
}
describe('complete RawIQ waveform', () => {
  it.each([1, 8])('retains every signed I/Q sample in %i channels', channels => {
    const result = decodePreviewMessage(frame(channels));
    expect(result.kind).toBe('waveform');
    if (result.kind !== 'waveform') throw new Error('not waveform');
    expect(result.channels?.length).toBe(channels);
    result.channels!.forEach((row, channel) => {
      expect(row.minimum).toHaveLength(0);
      expect(row.maximum).toHaveLength(8192);
      row.maximum.forEach((point, i) => {
        expect([point.x, point.i, point.q]).toEqual([i, (i + channel * 8192) % 65536 - 32768, 32767 - (i + channel * 8192) % 65536]);
        expect(point.magnitude).toBe(Math.hypot(point.i, point.q));
      });
    });
  });
  it.each(['length', 'offsets', 'scale', 'contract', 'dimensions', 'duplicate'])('rejects invalid %s', bad => {
    expect(() => decodePreviewMessage(frame(2, bad))).toThrow();
  });
});
