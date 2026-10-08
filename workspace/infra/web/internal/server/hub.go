package server

import (
	"context"
	"encoding/json"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

const (
	broadcastInterval = 100 * time.Millisecond
	websocketWriteTTL = 2 * time.Second
)

type websocketClient struct {
	send   chan []byte
	nodeID func() string
}

type Hub struct {
	store   *Store
	dirty   chan struct{}
	mu      sync.Mutex
	clients map[*websocketClient]struct{}
}

func NewHub(store *Store) *Hub {
	return &Hub{
		store:   store,
		dirty:   make(chan struct{}, 1),
		clients: make(map[*websocketClient]struct{}),
	}
}

func (h *Hub) Notify() {
	select {
	case h.dirty <- struct{}{}:
	default:
	}
}

func (h *Hub) Run(ctx context.Context) {
	ticker := time.NewTicker(broadcastInterval)
	defer ticker.Stop()
	dirty := false
	for {
		select {
		case <-h.dirty:
			dirty = true
		case now := <-ticker.C:
			if dirty {
				h.broadcastSnapshot(now)
				dirty = false
			}
		case <-ctx.Done():
			h.closeClients()
			return
		}
	}
}

func (h *Hub) ServeWebSocket(writer http.ResponseWriter, request *http.Request) {
	h.serveWebSocket(writer, request, nil)
}

func (h *Hub) ServeNodeTelemetry(writer http.ResponseWriter, request *http.Request, nodeID func() string) {
	if request.URL.Path == "/api/snapshot" {
		writeJSON(writer, filterSnapshot(h.store.Snapshot(time.Now()), nodeID()))
		return
	}
	h.serveWebSocket(writer, request, nodeID)
}

func (h *Hub) serveWebSocket(writer http.ResponseWriter, request *http.Request, nodeID func() string) {
	upgrader := websocket.Upgrader{
		ReadBufferSize:  1024,
		WriteBufferSize: 1024,
	}
	connection, err := upgrader.Upgrade(writer, request, nil)
	if err != nil {
		return
	}
	defer connection.Close()

	client := h.subscribe(nodeID)
	closed := make(chan struct{})
	connection.SetReadLimit(1024)
	go func() {
		defer close(closed)
		for {
			if _, _, err := connection.ReadMessage(); err != nil {
				return
			}
		}
	}()
	defer h.unsubscribe(client)
	h.mu.Lock()
	if _, ok := h.clients[client]; ok {
		h.enqueue(client, snapshotJSON(h.store.Snapshot(time.Now()), nodeID))
	}
	h.mu.Unlock()

	for {
		var message []byte
		select {
		case <-request.Context().Done():
			return
		case <-closed:
			return
		case next, ok := <-client.send:
			if !ok {
				return
			}
			message = next
		}
		if err := connection.SetWriteDeadline(
			time.Now().Add(websocketWriteTTL),
		); err != nil {
			return
		}
		if err := connection.WriteMessage(websocket.TextMessage, message); err != nil {
			return
		}
	}
}

func (h *Hub) broadcastSnapshot(now time.Time) {
	snapshot := h.store.Snapshot(now)
	message := snapshotJSON(snapshot, nil)
	h.mu.Lock()
	defer h.mu.Unlock()
	for client := range h.clients {
		if client.nodeID != nil {
			h.enqueue(client, snapshotJSON(snapshot, client.nodeID))
		} else {
			h.enqueue(client, message)
		}
	}
}

func (h *Hub) subscribe(nodeID func() string) *websocketClient {
	client := &websocketClient{send: make(chan []byte, 1), nodeID: nodeID}
	h.mu.Lock()
	h.clients[client] = struct{}{}
	h.mu.Unlock()
	return client
}

func (h *Hub) unsubscribe(client *websocketClient) {
	h.mu.Lock()
	if _, ok := h.clients[client]; ok {
		delete(h.clients, client)
		close(client.send)
	}
	h.mu.Unlock()
}

func (h *Hub) closeClients() {
	h.mu.Lock()
	defer h.mu.Unlock()
	for client := range h.clients {
		delete(h.clients, client)
		close(client.send)
	}
}

func (h *Hub) enqueue(client *websocketClient, message []byte) {
	select {
	case client.send <- message:
		return
	default:
	}
	select {
	case <-client.send:
	default:
	}
	select {
	case client.send <- message:
	default:
	}
}

func filterSnapshot(snapshot ClusterSnapshot, nodeID string) ClusterSnapshot {
	filtered := ClusterSnapshot{GeneratedAt: snapshot.GeneratedAt, Nodes: []NodeSnapshot{}}
	// ponytail: scan nodes per viewer; index snapshots if viewer counts grow.
	for _, node := range snapshot.Nodes {
		if nodeID != "" && node.NodeID == nodeID {
			filtered.Nodes = append(filtered.Nodes, node)
			break
		}
	}
	return filtered
}

func snapshotJSON(snapshot ClusterSnapshot, nodeID func() string) []byte {
	if nodeID != nil {
		snapshot = filterSnapshot(snapshot, nodeID())
	}
	message, err := json.Marshal(snapshot)
	if err != nil {
		return []byte(`{"generated_at":"","nodes":[]}`)
	}
	return message
}
