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
	send chan []byte
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
	upgrader := websocket.Upgrader{
		ReadBufferSize:  1024,
		WriteBufferSize: 1024,
	}
	connection, err := upgrader.Upgrade(writer, request, nil)
	if err != nil {
		return
	}
	defer connection.Close()

	client := h.subscribe()
	defer h.unsubscribe(client)
	h.mu.Lock()
	if _, ok := h.clients[client]; ok {
		h.enqueue(client, snapshotJSON(h.store, time.Now()))
	}
	h.mu.Unlock()

	for message := range client.send {
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
	message := snapshotJSON(h.store, now)
	h.mu.Lock()
	defer h.mu.Unlock()
	for client := range h.clients {
		h.enqueue(client, message)
	}
}

func (h *Hub) subscribe() *websocketClient {
	client := &websocketClient{send: make(chan []byte, 1)}
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

func snapshotJSON(store *Store, now time.Time) []byte {
	message, err := json.Marshal(store.Snapshot(now))
	if err != nil {
		return []byte(`{"generated_at":"","nodes":[]}`)
	}
	return message
}
