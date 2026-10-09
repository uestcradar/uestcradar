package preview

import (
	flatbuffers "github.com/google/flatbuffers/go"
	"testing"
	fb "uestcradar/frontend/internal/previewfb"
)

func TestFullRawIQValidation(t *testing.T) {
	for _, bad := range []string{"", "length", "offsets", "scale", "contract", "columns", "rows", "version"} {
		t.Run(bad, func(t *testing.T) {
			b := flatbuffers.NewBuilder(256)
			size := 12
			if bad == "length" {
				size--
			}
			values := b.CreateByteVector(make([]byte, size))
			offsets := flatbuffers.UOffsetT(0)
			if bad == "offsets" {
				offsets = b.CreateByteVector([]byte{0})
			}
			fb.WaveformChannelStart(b)
			fb.WaveformChannelAddBucketCount(b, 3)
			fb.WaveformChannelAddValues(b, values)
			fb.WaveformChannelAddMinOffsets(b, offsets)
			if bad == "scale" {
				fb.WaveformChannelAddScale(b, 2)
			}
			channel := fb.WaveformChannelEnd(b)
			fb.WaveformPreviewStartChannelsVector(b, 1)
			b.PrependUOffsetT(channel)
			vector := b.EndVector(1)
			fb.WaveformPreviewStart(b)
			fb.WaveformPreviewAddChannels(b, vector)
			body := fb.WaveformPreviewEnd(b)
			node := b.CreateString("node-a")
			fb.PreviewFrameStart(b)
			fb.PreviewFrameAddNodeId(b, node)
			fb.PreviewFrameAddLeg(b, fb.LegOutput)
			tid, version := uint64(4), uint32(1)
			if bad == "contract" {
				tid = 1
			}
			if bad == "version" {
				version = 2
			}
			fb.PreviewFrameAddFrameTypeId(b, tid)
			fb.PreviewFrameAddFrameTypeVersion(b, version)
			fb.PreviewFrameAddOriginalRows(b, 1)
			fb.PreviewFrameAddOriginalColumns(b, 3)
			rows, columns := uint32(1), uint32(3)
			if bad == "rows" {
				rows++
			}
			if bad == "columns" {
				columns++
			}
			fb.PreviewFrameAddPoolRows(b, rows)
			fb.PreviewFrameAddPoolColumns(b, columns)
			fb.PreviewFrameAddEncoding(b, fb.ValueEncodingComplexInt16)
			fb.PreviewFrameAddBodyType(b, fb.PreviewBodyWaveformPreview)
			fb.PreviewFrameAddBody(b, body)
			frame := fb.PreviewFrameEnd(b)
			message := fb.GetRootAsPreviewMessage(finishTestMessage(b, fb.MessagePayloadPreviewFrame, frame), 0)
			_, valid := validatePreviewFrame(message, "node-a", "")
			if valid != (bad == "") {
				t.Fatalf("valid=%v for %q", valid, bad)
			}
		})
	}
}
