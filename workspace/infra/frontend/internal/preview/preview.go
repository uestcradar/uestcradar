package preview

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"math"
	"net"
	"net/http"
	"sort"
	"strconv"
	"sync"
	"time"

	flatbuffers "github.com/google/flatbuffers/go"
	"github.com/gorilla/websocket"

	fb "uestcradar/frontend/internal/previewfb"
)

const (
	protocolVersion = 1
	maxWireBytes    = 8 * 1024 * 1024
	maxSelectors    = 128
	writeTimeout    = 2 * time.Second
)

type routeKey struct {
	nodeID      string
	leg         fb.Leg
	typeID      uint64
	typeVersion uint32
}

type selector struct {
	subscriptionID uint64
	routeKey
	requestedFPS float32
}

type Stream struct {
	Leg         string `json:"leg"`
	TypeID      string `json:"type_id"`
	TypeVersion uint32 `json:"type_version"`
}

type Node struct {
	NodeID     string   `json:"node_id"`
	InstanceID string   `json:"instance_id"`
	Connected  bool     `json:"connected"`
	Streams    []Stream `json:"streams"`
}

type sidecarClient struct {
	nodeID string
	node   Node
	conn   net.Conn
	mu     sync.Mutex
}

func (client *sidecarClient) send(message []byte) error {
	client.mu.Lock()
	defer client.mu.Unlock()
	if err := client.conn.SetWriteDeadline(time.Now().Add(writeTimeout)); err != nil {
		return err
	}
	var header [4]byte
	binary.BigEndian.PutUint32(header[:], uint32(len(message)))
	if err := writeFull(client.conn, header[:]); err != nil {
		return err
	}
	return writeFull(client.conn, message)
}

func writeFull(writer io.Writer, data []byte) error {
	for len(data) > 0 {
		written, err := writer.Write(data)
		if err != nil {
			return err
		}
		if written == 0 {
			return io.ErrShortWrite
		}
		data = data[written:]
	}
	return nil
}

type websocketClient struct {
	conn      *websocket.Conn
	selectors map[routeKey]selector
	pending   map[routeKey][]byte
	notify    chan struct{}
	done      chan struct{}
	mu        sync.Mutex
}

func newWebsocketClient() *websocketClient {
	return &websocketClient{
		selectors: make(map[routeKey]selector),
		pending:   make(map[routeKey][]byte),
		notify:    make(chan struct{}, 1),
		done:      make(chan struct{}),
	}
}

func (client *websocketClient) enqueue(key routeKey, message []byte) {
	client.mu.Lock()
	if _, subscribed := client.selectors[key]; subscribed {
		client.pending[key] = append(client.pending[key][:0], message...)
	}
	client.mu.Unlock()
	select {
	case client.notify <- struct{}{}:
	default:
	}
}

func (client *websocketClient) drain() [][]byte {
	client.mu.Lock()
	keys := make([]routeKey, 0, len(client.pending))
	for key := range client.pending {
		keys = append(keys, key)
	}
	sort.Slice(keys, func(i, j int) bool {
		if keys[i].nodeID != keys[j].nodeID {
			return keys[i].nodeID < keys[j].nodeID
		}
		if keys[i].leg != keys[j].leg {
			return keys[i].leg < keys[j].leg
		}
		return keys[i].typeID < keys[j].typeID
	})
	messages := make([][]byte, 0, len(keys))
	for _, key := range keys {
		messages = append(messages, client.pending[key])
		delete(client.pending, key)
	}
	client.mu.Unlock()
	return messages
}

// Service routes opaque, validated FlatBuffers frames between Sidecars and browsers.
type Service struct {
	nodeID   string
	mu       sync.Mutex
	sidecars map[string]*sidecarClient
	web      map[*websocketClient]struct{}
	version  uint64
}

func NewService(nodeID string) *Service {
	return &Service{
		nodeID:   nodeID,
		sidecars: make(map[string]*sidecarClient),
		web:      make(map[*websocketClient]struct{}),
	}
}

func (service *Service) Node() Node {
	service.mu.Lock()
	defer service.mu.Unlock()
	if client := service.sidecars[service.nodeID]; client != nil {
		return client.node
	}
	return Node{NodeID: service.nodeID, Streams: []Stream{}}
}

func (service *Service) Close() {
	service.mu.Lock()
	defer service.mu.Unlock()
	for _, client := range service.sidecars {
		_ = client.conn.Close()
	}
	for client := range service.web {
		if client.conn != nil {
			_ = client.conn.Close()
		}
	}
}

func parseHello(message *fb.PreviewMessage, expected string) (node Node, err error) {
	defer func() {
		if recover() != nil {
			err = fmt.Errorf("malformed SidecarHello")
		}
	}()
	var table flatbuffers.Table
	if !message.Payload(&table) {
		return node, fmt.Errorf("missing SidecarHello")
	}
	hello := &fb.SidecarHello{}
	hello.Init(table.Bytes, table.Pos)
	node = Node{NodeID: string(hello.NodeId()), InstanceID: string(hello.InstanceId()), Connected: true, Streams: []Stream{}}
	if node.NodeID != expected || expected == "" || len(expected) > 128 || node.InstanceID == "" || len(node.InstanceID) > 256 || hello.StreamsLength() > 2 {
		return node, fmt.Errorf("invalid Sidecar identity or streams")
	}
	seen := map[fb.Leg]bool{}
	for i := 0; i < hello.StreamsLength(); i++ {
		var stream fb.StreamDescriptor
		if !hello.Streams(&stream, i) || seen[stream.Leg()] || (stream.Leg() != fb.LegInput && stream.Leg() != fb.LegOutput) || stream.FrameTypeId() == 0 || stream.FrameTypeVersion() == 0 {
			return node, fmt.Errorf("invalid stream descriptor")
		}
		seen[stream.Leg()] = true
		leg := "input"
		if stream.Leg() == fb.LegOutput {
			leg = "output"
		}
		node.Streams = append(node.Streams, Stream{leg, strconv.FormatUint(stream.FrameTypeId(), 10), stream.FrameTypeVersion()})
	}
	return node, nil
}

func (client *sidecarClient) accepts(key routeKey) bool {
	for _, stream := range client.node.Streams {
		leg := fb.LegInput
		if stream.Leg == "output" {
			leg = fb.LegOutput
		}
		if key.nodeID == client.nodeID && key.leg == leg && strconv.FormatUint(key.typeID, 10) == stream.TypeID && key.typeVersion == stream.TypeVersion {
			return true
		}
	}
	return false
}

func (service *Service) ServeHTTP(writer http.ResponseWriter, request *http.Request) {
	service.ServeWebSocket(writer, request)
}

func (service *Service) RunTCP(ctx context.Context, address string) error {
	listener, err := net.Listen("tcp", address)
	if err != nil {
		return fmt.Errorf("listen preview TCP: %w", err)
	}
	return service.ServeTCP(ctx, listener)
}

func (service *Service) ServeTCP(ctx context.Context, listener net.Listener) error {
	defer listener.Close()
	go func() {
		<-ctx.Done()
		_ = listener.Close()
	}()
	for {
		connection, err := listener.Accept()
		if err != nil {
			if ctx.Err() != nil || errors.Is(err, net.ErrClosed) {
				return nil
			}
			return fmt.Errorf("accept preview TCP: %w", err)
		}
		go service.handleSidecar(ctx, connection)
	}
}

func (service *Service) handleSidecar(ctx context.Context, connection net.Conn) {
	defer connection.Close()
	stop := context.AfterFunc(ctx, func() { _ = connection.Close() })
	defer stop()
	_ = connection.SetReadDeadline(time.Now().Add(5 * time.Second))
	message, err := readFramed(connection)
	if err != nil {
		return
	}
	parsed, err := parseMessage(message)
	if err != nil || parsed.PayloadType() != fb.MessagePayloadSidecarHello {
		return
	}
	node, err := parseHello(parsed, service.nodeID)
	if err != nil {
		return
	}
	_ = connection.SetReadDeadline(time.Time{})
	nodeID := node.NodeID
	client := &sidecarClient{nodeID: nodeID, node: node, conn: connection}
	service.mu.Lock()
	previous := service.sidecars[nodeID]
	service.sidecars[nodeID] = client
	service.mu.Unlock()
	if previous != nil {
		_ = previous.conn.Close()
	}
	service.pushSubscriptions()
	defer func() {
		service.mu.Lock()
		if service.sidecars[nodeID] == client {
			delete(service.sidecars, nodeID)
		}
		service.mu.Unlock()
	}()

	for ctx.Err() == nil {
		message, err = readFramed(connection)
		if err != nil {
			return
		}
		parsed, err = parseMessage(message)
		if err != nil {
			continue
		}
		service.mu.Lock()
		current := service.sidecars[nodeID] == client
		service.mu.Unlock()
		if !current {
			return
		}
		switch parsed.PayloadType() {
		case fb.MessagePayloadPreviewFrame:
			key, valid := validatePreviewFrame(parsed, nodeID, client.node.InstanceID)
			if valid && client.accepts(key) {
				service.broadcast(key, message)
			}
		case fb.MessagePayloadStreamStatus:
			// Status uses the same route and can be exposed without changing transport.
			service.broadcastStatus(parsed, nodeID, message)
		}
	}
}

func (service *Service) broadcast(key routeKey, message []byte) {
	service.mu.Lock()
	clients := make([]*websocketClient, 0, len(service.web))
	for client := range service.web {
		clients = append(clients, client)
	}
	service.mu.Unlock()
	for _, client := range clients {
		client.enqueue(key, message)
	}
}

func (service *Service) broadcastStatus(
	message *fb.PreviewMessage,
	nodeID string,
	raw []byte,
) {
	defer func() { _ = recover() }() // Malformed status must not kill the observation process.
	var table flatbuffers.Table
	if !message.Payload(&table) {
		return
	}
	status := &fb.StreamStatus{}
	status.Init(table.Bytes, table.Pos)
	if string(status.NodeId()) != nodeID ||
		(status.Leg() != fb.LegInput && status.Leg() != fb.LegOutput) ||
		status.FrameTypeId() == 0 || status.FrameTypeVersion() == 0 ||
		math.IsNaN(float64(status.ActualFps())) ||
		status.ActualFps() < 0 || status.ActualFps() > 30 {
		return
	}
	key := routeKey{nodeID: nodeID, leg: status.Leg(), typeID: status.FrameTypeId(), typeVersion: status.FrameTypeVersion()}
	service.mu.Lock()
	client := service.sidecars[nodeID]
	accepted := client != nil && client.accepts(key)
	service.mu.Unlock()
	if accepted {
		service.broadcast(key, raw)
	}
}

func (service *Service) ServeWebSocket(writer http.ResponseWriter, request *http.Request) {
	upgrader := websocket.Upgrader{
		ReadBufferSize:  4096,
		WriteBufferSize: 4096,
		CheckOrigin: func(req *http.Request) bool {
			origin := req.Header.Get("Origin")
			return origin == "" || origin == "https://"+req.Host ||
				origin == "http://"+req.Host
		},
	}
	connection, err := upgrader.Upgrade(writer, request, nil)
	if err != nil {
		return
	}
	defer connection.Close()
	client := newWebsocketClient()
	client.conn = connection
	service.mu.Lock()
	service.web[client] = struct{}{}
	service.mu.Unlock()
	defer func() {
		service.mu.Lock()
		delete(service.web, client)
		service.mu.Unlock()
		service.pushSubscriptions()
	}()

	go service.readBrowser(connection, client)
	for {
		select {
		case <-client.done:
			return
		case <-client.notify:
			for _, message := range client.drain() {
				if err := connection.SetWriteDeadline(time.Now().Add(writeTimeout)); err != nil {
					return
				}
				if err := connection.WriteMessage(websocket.BinaryMessage, message); err != nil {
					return
				}
			}
		}
	}
}

func (service *Service) readBrowser(
	connection *websocket.Conn,
	client *websocketClient,
) {
	defer close(client.done)
	connection.SetReadLimit(maxWireBytes)
	for {
		kind, message, err := connection.ReadMessage()
		if err != nil {
			return
		}
		if kind != websocket.BinaryMessage {
			continue
		}
		selectors, err := parseSubscription(message)
		if err != nil {
			continue
		}
		for key := range selectors {
			if key.nodeID != service.nodeID {
				delete(selectors, key)
			}
		}
		client.mu.Lock()
		client.selectors = selectors
		for key := range client.pending {
			if _, keep := selectors[key]; !keep {
				delete(client.pending, key)
			}
		}
		client.mu.Unlock()
		service.pushSubscriptions()
	}
}

func (service *Service) pushSubscriptions() {
	service.mu.Lock()
	service.version++
	version := service.version
	merged := make(map[routeKey]selector)
	for client := range service.web {
		client.mu.Lock()
		for key, value := range client.selectors {
			if current, found := merged[key]; !found || value.requestedFPS > current.requestedFPS {
				merged[key] = value
			}
		}
		client.mu.Unlock()
	}
	sidecars := make([]*sidecarClient, 0, len(service.sidecars))
	for _, client := range service.sidecars {
		sidecars = append(sidecars, client)
	}
	service.mu.Unlock()

	for _, client := range sidecars {
		filtered := make([]selector, 0)
		for key, value := range merged {
			if client.accepts(key) {
				filtered = append(filtered, value)
			}
		}
		message := buildSubscription(version, filtered)
		if err := client.send(message); err != nil {
			_ = client.conn.Close()
		}
	}
}

func readFramed(reader io.Reader) ([]byte, error) {
	var header [4]byte
	if _, err := io.ReadFull(reader, header[:]); err != nil {
		return nil, err
	}
	length := binary.BigEndian.Uint32(header[:])
	if length == 0 || length > maxWireBytes {
		return nil, fmt.Errorf("invalid preview message length")
	}
	message := make([]byte, length)
	_, err := io.ReadFull(reader, message)
	return message, err
}

func parseMessage(data []byte) (message *fb.PreviewMessage, err error) {
	if len(data) < 8 || string(data[4:8]) != "UPRV" {
		return nil, fmt.Errorf("invalid preview identifier")
	}
	defer func() {
		if recovered := recover(); recovered != nil {
			message = nil
			err = fmt.Errorf("malformed preview FlatBuffer")
		}
	}()
	message = fb.GetRootAsPreviewMessage(data, 0)
	if message.ProtocolVersion() != protocolVersion {
		return nil, fmt.Errorf("unsupported preview protocol")
	}
	if message.PayloadType() < fb.MessagePayloadSidecarHello ||
		message.PayloadType() > fb.MessagePayloadStreamStatus {
		return nil, fmt.Errorf("missing or unsupported preview payload")
	}
	var payload flatbuffers.Table
	if !message.Payload(&payload) {
		return nil, fmt.Errorf("missing preview payload")
	}
	return message, nil
}

func parseSubscription(data []byte) (result map[routeKey]selector, err error) {
	defer func() {
		if recover() != nil {
			result = nil
			err = fmt.Errorf("malformed subscription")
		}
	}()
	message, err := parseMessage(data)
	if err != nil || message.PayloadType() != fb.MessagePayloadSubscriptionUpdate {
		return nil, fmt.Errorf("expected SubscriptionUpdate")
	}
	var table flatbuffers.Table
	if !message.Payload(&table) {
		return nil, fmt.Errorf("missing SubscriptionUpdate")
	}
	update := &fb.SubscriptionUpdate{}
	update.Init(table.Bytes, table.Pos)
	if update.SelectorsLength() > maxSelectors {
		return nil, fmt.Errorf("too many preview selectors")
	}
	selectors := make(map[routeKey]selector, update.SelectorsLength())
	for index := 0; index < update.SelectorsLength(); index++ {
		var item fb.StreamSelector
		if !update.Selectors(&item, index) {
			return nil, fmt.Errorf("invalid preview selector")
		}
		nodeID := string(item.NodeId())
		fps := item.RequestedFps()
		if nodeID == "" || len(nodeID) > 128 ||
			(item.Leg() != fb.LegInput && item.Leg() != fb.LegOutput) ||
			item.FrameTypeId() == 0 || item.FrameTypeVersion() == 0 ||
			math.IsNaN(float64(fps)) || fps <= 0 || fps > 30 {
			return nil, fmt.Errorf("invalid preview selector fields")
		}
		key := routeKey{nodeID, item.Leg(), item.FrameTypeId(), item.FrameTypeVersion()}
		selectors[key] = selector{item.SubscriptionId(), key, fps}
	}
	return selectors, nil
}

func validatePreviewFrame(
	message *fb.PreviewMessage,
	connectionNode string,
	instanceID string,
) (key routeKey, valid bool) {
	defer func() {
		if recover() != nil {
			valid = false
		}
	}()
	var table flatbuffers.Table
	if !message.Payload(&table) {
		return routeKey{}, false
	}
	frame := &fb.PreviewFrame{}
	frame.Init(table.Bytes, table.Pos)
	key = routeKey{
		nodeID: string(frame.NodeId()), leg: frame.Leg(),
		typeID: frame.FrameTypeId(), typeVersion: frame.FrameTypeVersion(),
	}
	if key.nodeID != connectionNode || string(frame.InstanceId()) != instanceID ||
		(key.leg != fb.LegInput && key.leg != fb.LegOutput) ||
		key.typeID == 0 || key.typeVersion == 0 ||
		frame.OriginalRows() == 0 || frame.OriginalColumns() == 0 ||
		frame.PoolRows() == 0 || frame.PoolColumns() == 0 {
		return routeKey{}, false
	}
	if !frame.Body(&table) {
		return routeKey{}, false
	}
	switch frame.BodyType() {
	case fb.PreviewBodyWaveformPreview:
		waveform := &fb.WaveformPreview{}
		waveform.Init(table.Bytes, table.Pos)
		if waveform.ChannelsLength() != int(frame.OriginalRows()) ||
			waveform.ChannelsLength() > 4096 {
			return routeKey{}, false
		}
		full := frame.Encoding() == fb.ValueEncodingComplexInt16
		if full && (key.typeID != 4 || key.typeVersion != 1 ||
			frame.PoolRows() != frame.OriginalRows() || frame.PoolColumns() != frame.OriginalColumns()) {
			return routeKey{}, false
		}
		seen := make(map[uint32]bool, waveform.ChannelsLength())
		for index := 0; index < waveform.ChannelsLength(); index++ {
			var channel fb.WaveformChannel
			if !waveform.Channels(&channel, index) ||
				seen[channel.ChannelIndex()] ||
				channel.ChannelIndex() >= frame.OriginalRows() ||
				channel.BucketCount() != frame.PoolColumns() {
				return routeKey{}, false
			}
			offsets := int(channel.BucketCount())
			if full {
				offsets = 0
				if channel.Scale() != 1 {
					return routeKey{}, false
				}
			}
			if channel.MinOffsetsLength() != offsets || channel.MaxOffsetsLength() != offsets {
				return routeKey{}, false
			}
			bytesPerBucket := 0
			switch frame.Encoding() {
			case fb.ValueEncodingComplexInt8, fb.ValueEncodingComplexInt16:
				bytesPerBucket = 4
			case fb.ValueEncodingComplexFloat16:
				bytesPerBucket = 8
			default:
				return routeKey{}, false
			}
			if channel.ValuesLength() != int(channel.BucketCount())*bytesPerBucket {
				return routeKey{}, false
			}
			seen[channel.ChannelIndex()] = true
		}
	case fb.PreviewBodyHeatmapPreview:
		heatmap := &fb.HeatmapPreview{}
		heatmap.Init(table.Bytes, table.Pos)
		cells := uint64(heatmap.Rows()) * uint64(heatmap.Columns())
		if heatmap.Rows() != frame.PoolRows() ||
			heatmap.Columns() != frame.PoolColumns() ||
			cells > maxWireBytes/4 {
			return routeKey{}, false
		}
		switch frame.Encoding() {
		case fb.ValueEncodingFloat16:
			if heatmap.MaxOffsetsLength() != int(cells) || heatmap.ValuesLength() != int(cells*2) {
				return routeKey{}, false
			}
		case fb.ValueEncodingFloat32:
			stride := heatmap.RangeStride()
			if stride == 0 || stride != 1+(frame.OriginalRows()-1)/1600 ||
				heatmap.Rows() != 1+(frame.OriginalRows()-1)/stride ||
				heatmap.Columns() != frame.OriginalColumns() ||
				heatmap.MaxOffsetsLength() != 0 || heatmap.ValuesLength() != int(cells*4) {
				return routeKey{}, false
			}
		default:
			return routeKey{}, false
		}
	default:
		return routeKey{}, false
	}
	return key, true
}

func buildSubscription(version uint64, selectors []selector) []byte {
	sort.Slice(selectors, func(i, j int) bool {
		if selectors[i].nodeID != selectors[j].nodeID {
			return selectors[i].nodeID < selectors[j].nodeID
		}
		return selectors[i].leg < selectors[j].leg
	})
	builder := flatbuffers.NewBuilder(1024)
	offsets := make([]flatbuffers.UOffsetT, 0, len(selectors))
	for _, item := range selectors {
		node := builder.CreateString(item.nodeID)
		fb.StreamSelectorStart(builder)
		fb.StreamSelectorAddSubscriptionId(builder, item.subscriptionID)
		fb.StreamSelectorAddNodeId(builder, node)
		fb.StreamSelectorAddLeg(builder, item.leg)
		fb.StreamSelectorAddFrameTypeId(builder, item.typeID)
		fb.StreamSelectorAddFrameTypeVersion(builder, item.typeVersion)
		fb.StreamSelectorAddRequestedFps(builder, item.requestedFPS)
		offsets = append(offsets, fb.StreamSelectorEnd(builder))
	}
	fb.SubscriptionUpdateStartSelectorsVector(builder, len(offsets))
	for index := len(offsets) - 1; index >= 0; index-- {
		builder.PrependUOffsetT(offsets[index])
	}
	selectorVector := builder.EndVector(len(offsets))
	fb.SubscriptionUpdateStart(builder)
	fb.SubscriptionUpdateAddVersion(builder, version)
	fb.SubscriptionUpdateAddSelectors(builder, selectorVector)
	update := fb.SubscriptionUpdateEnd(builder)
	fb.PreviewMessageStart(builder)
	fb.PreviewMessageAddProtocolVersion(builder, protocolVersion)
	fb.PreviewMessageAddPayloadType(builder, fb.MessagePayloadSubscriptionUpdate)
	fb.PreviewMessageAddPayload(builder, update)
	message := fb.PreviewMessageEnd(builder)
	builder.FinishWithFileIdentifier(message, []byte("UPRV"))
	return append([]byte(nil), builder.FinishedBytes()...)
}
