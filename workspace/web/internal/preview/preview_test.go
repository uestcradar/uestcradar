package preview

import (
	"bytes"
	"context"
	"encoding/binary"
	"net"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	flatbuffers "github.com/google/flatbuffers/go"
	"github.com/gorilla/websocket"

	fb "uestcradar/telemetry/internal/previewfb"
)

func TestSubscriptionSupportsMultipleNodesAndRoutes(t *testing.T) {
	values := []selector{
		{subscriptionID: 1, routeKey: routeKey{"node-a", fb.LegInput, 1, 2}, requestedFPS: 15},
		{subscriptionID: 2, routeKey: routeKey{"node-b", fb.LegOutput, 2, 2}, requestedFPS: 30},
	}
	encoded := buildSubscription(7, values)
	parsed, err := parseSubscription(encoded)
	if err != nil {
		t.Fatal(err)
	}
	if len(parsed) != 2 || parsed[values[0].routeKey].requestedFPS != 15 ||
		parsed[values[1].routeKey].subscriptionID != 2 {
		t.Fatalf("unexpected selectors: %#v", parsed)
	}
}

func TestHeatmapEncodingsAndDimensions(t *testing.T) {
	for _, tc := range []struct {
		name          string
		encoding      fb.ValueEncoding
		size, offsets int
		stride        uint32
		valid         bool
	}{
		{"float32", fb.ValueEncodingFloat32, 24, 0, 1, true},
		{"legacy", fb.ValueEncodingFloat16, 12, 6, 1, true},
		{"truncated", fb.ValueEncodingFloat32, 23, 0, 1, false},
		{"bad stride", fb.ValueEncodingFloat32, 24, 0, 2, false},
		{"legacy offsets", fb.ValueEncodingFloat16, 12, 0, 1, false},
	} {
		t.Run(tc.name, func(t *testing.T) {
			b := flatbuffers.NewBuilder(256)
			values := b.CreateByteVector(make([]byte, tc.size))
			offsets := b.CreateByteVector(make([]byte, tc.offsets))
			fb.HeatmapPreviewStart(b)
			fb.HeatmapPreviewAddRows(b, 3)
			fb.HeatmapPreviewAddColumns(b, 2)
			fb.HeatmapPreviewAddValues(b, values)
			fb.HeatmapPreviewAddMaxOffsets(b, offsets)
			fb.HeatmapPreviewAddRangeStride(b, tc.stride)
			body := fb.HeatmapPreviewEnd(b)
			node := b.CreateString("node-a")
			fb.PreviewFrameStart(b)
			fb.PreviewFrameAddNodeId(b, node)
			fb.PreviewFrameAddLeg(b, fb.LegOutput)
			fb.PreviewFrameAddFrameTypeId(b, 3)
			fb.PreviewFrameAddFrameTypeVersion(b, 2)
			fb.PreviewFrameAddOriginalRows(b, 3)
			fb.PreviewFrameAddOriginalColumns(b, 2)
			fb.PreviewFrameAddPoolRows(b, 3)
			fb.PreviewFrameAddPoolColumns(b, 2)
			fb.PreviewFrameAddEncoding(b, tc.encoding)
			fb.PreviewFrameAddBodyType(b, fb.PreviewBodyHeatmapPreview)
			fb.PreviewFrameAddBody(b, body)
			frame := fb.PreviewFrameEnd(b)
			message := fb.GetRootAsPreviewMessage(finishTestMessage(b, fb.MessagePayloadPreviewFrame, frame), 0)
			_, valid := validatePreviewFrame(message, "node-a")
			if valid != tc.valid {
				t.Fatalf("valid=%v, want %v", valid, tc.valid)
			}
		})
	}
}

func TestLatestFrameIsIndependentPerRoute(t *testing.T) {
	client := newWebsocketClient()
	first := routeKey{"node-a", fb.LegInput, 1, 2}
	second := routeKey{"node-b", fb.LegOutput, 1, 2}
	client.selectors[first] = selector{routeKey: first}
	client.selectors[second] = selector{routeKey: second}
	client.enqueue(first, []byte("old-a"))
	client.enqueue(second, []byte("new-b"))
	client.enqueue(first, []byte("new-a"))
	messages := client.drain()
	if len(messages) != 2 {
		t.Fatalf("message count = %d", len(messages))
	}
	joined := string(bytes.Join(messages, []byte("|")))
	if joined != "new-a|new-b" {
		t.Fatalf("latest routing = %q", joined)
	}
}

func TestFramedReaderRejectsOversizedMessage(t *testing.T) {
	var header [4]byte
	binary.BigEndian.PutUint32(header[:], maxWireBytes+1)
	if _, err := readFramed(bytes.NewReader(header[:])); err == nil {
		t.Fatal("oversized preview message was accepted")
	}
}

func TestMalformedFlatBufferIsRejected(t *testing.T) {
	if _, err := parseMessage([]byte{0, 0, 0, 0, 'U', 'P', 'R', 'V'}); err == nil {
		t.Fatal("malformed FlatBuffer was accepted")
	}
}

func finishTestMessage(
	builder *flatbuffers.Builder,
	payloadType fb.MessagePayload,
	payload flatbuffers.UOffsetT,
) []byte {
	fb.PreviewMessageStart(builder)
	fb.PreviewMessageAddProtocolVersion(builder, protocolVersion)
	fb.PreviewMessageAddPayloadType(builder, payloadType)
	fb.PreviewMessageAddPayload(builder, payload)
	root := fb.PreviewMessageEnd(builder)
	builder.FinishWithFileIdentifier(root, []byte("UPRV"))
	return append([]byte(nil), builder.FinishedBytes()...)
}

func testHello() []byte {
	builder := flatbuffers.NewBuilder(256)
	node := builder.CreateString("node-a")
	instance := builder.CreateString("instance-a")
	fb.StreamDescriptorStart(builder)
	fb.StreamDescriptorAddLeg(builder, fb.LegOutput)
	fb.StreamDescriptorAddFrameTypeId(builder, 1)
	fb.StreamDescriptorAddFrameTypeVersion(builder, 2)
	stream := fb.StreamDescriptorEnd(builder)
	fb.SidecarHelloStartStreamsVector(builder, 1)
	builder.PrependUOffsetT(stream)
	streams := builder.EndVector(1)
	fb.SidecarHelloStart(builder)
	fb.SidecarHelloAddNodeId(builder, node)
	fb.SidecarHelloAddInstanceId(builder, instance)
	fb.SidecarHelloAddStreams(builder, streams)
	hello := fb.SidecarHelloEnd(builder)
	return finishTestMessage(builder, fb.MessagePayloadSidecarHello, hello)
}

func testPreviewFrame() []byte {
	builder := flatbuffers.NewBuilder(512)
	minimum := builder.CreateByteVector([]byte{5})
	maximum := builder.CreateByteVector([]byte{9})
	values := builder.CreateByteVector([]byte{1, 2, 3, 4})
	fb.WaveformChannelStart(builder)
	fb.WaveformChannelAddChannelIndex(builder, 0)
	fb.WaveformChannelAddBucketCount(builder, 1)
	fb.WaveformChannelAddScale(builder, 10)
	fb.WaveformChannelAddMinOffsets(builder, minimum)
	fb.WaveformChannelAddMaxOffsets(builder, maximum)
	fb.WaveformChannelAddValues(builder, values)
	channel := fb.WaveformChannelEnd(builder)
	fb.WaveformPreviewStartChannelsVector(builder, 1)
	builder.PrependUOffsetT(channel)
	channels := builder.EndVector(1)
	fb.WaveformPreviewStart(builder)
	fb.WaveformPreviewAddChannels(builder, channels)
	waveform := fb.WaveformPreviewEnd(builder)
	node := builder.CreateString("node-a")
	instance := builder.CreateString("instance-a")
	fb.PreviewFrameStart(builder)
	fb.PreviewFrameAddNodeId(builder, node)
	fb.PreviewFrameAddInstanceId(builder, instance)
	fb.PreviewFrameAddLeg(builder, fb.LegOutput)
	fb.PreviewFrameAddFrameTypeId(builder, 1)
	fb.PreviewFrameAddFrameTypeVersion(builder, 2)
	fb.PreviewFrameAddFrameId(builder, 77)
	fb.PreviewFrameAddTimestampNs(builder, 88)
	fb.PreviewFrameAddOriginalRows(builder, 1)
	fb.PreviewFrameAddOriginalColumns(builder, 128)
	fb.PreviewFrameAddPoolRows(builder, 1)
	fb.PreviewFrameAddPoolColumns(builder, 1)
	fb.PreviewFrameAddEncoding(builder, fb.ValueEncodingComplexInt8)
	fb.PreviewFrameAddBodyType(builder, fb.PreviewBodyWaveformPreview)
	fb.PreviewFrameAddBody(builder, waveform)
	frame := fb.PreviewFrameEnd(builder)
	return finishTestMessage(builder, fb.MessagePayloadPreviewFrame, frame)
}

func writeTestFrame(t *testing.T, connection net.Conn, message []byte) {
	t.Helper()
	var header [4]byte
	binary.BigEndian.PutUint32(header[:], uint32(len(message)))
	if err := writeFull(connection, header[:]); err != nil {
		t.Fatal(err)
	}
	if err := writeFull(connection, message); err != nil {
		t.Fatal(err)
	}
}

func TestTCPToWebSocketSubscriptionAndRouting(t *testing.T) {
	probe, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	address := probe.Addr().String()
	_ = probe.Close()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	service := NewService()
	tcpErrors := make(chan error, 1)
	go func() { tcpErrors <- service.RunTCP(ctx, address) }()

	httpServer := httptest.NewServer(service)
	defer httpServer.Close()
	websocketURL := "ws" + strings.TrimPrefix(httpServer.URL, "http")
	browser, _, err := websocket.DefaultDialer.Dial(websocketURL, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer browser.Close()
	subscription := buildSubscription(1, []selector{{
		subscriptionID: 1,
		routeKey: routeKey{
			nodeID: "node-a", leg: fb.LegOutput,
			typeID: 1, typeVersion: 2,
		},
		requestedFPS: 30,
	}})
	if err := browser.WriteMessage(websocket.BinaryMessage, subscription); err != nil {
		t.Fatal(err)
	}

	var sidecar net.Conn
	for deadline := time.Now().Add(time.Second); time.Now().Before(deadline); {
		sidecar, err = net.DialTimeout("tcp", address, 50*time.Millisecond)
		if err == nil {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	if sidecar == nil {
		t.Fatalf("preview TCP listener was not ready: %v", err)
	}
	defer sidecar.Close()
	writeTestFrame(t, sidecar, testHello())
	if _, err := readFramed(sidecar); err != nil {
		t.Fatalf("sidecar did not receive subscription: %v", err)
	}
	wireFrame := testPreviewFrame()
	writeTestFrame(t, sidecar, wireFrame)
	_ = browser.SetReadDeadline(time.Now().Add(time.Second))
	kind, received, err := browser.ReadMessage()
	if err != nil {
		t.Fatal(err)
	}
	if kind != websocket.BinaryMessage || !bytes.Equal(received, wireFrame) {
		t.Fatal("validated PreviewFrame was not routed unchanged")
	}

	cancel()
	select {
	case err := <-tcpErrors:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(time.Second):
		t.Fatal("preview TCP listener did not stop")
	}
}
