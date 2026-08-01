package server

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
	pb "uestcradar/telemetry/internal/telemetrypb"
)

func TestSnapshotEndpointAndWebSocketInitialState(t *testing.T) {
	store := NewStore()
	seenAt := time.Now()
	store.UpdateHeartbeat(
		heartbeat(
			"node-b",
			"instance-1",
			1,
			0,
			pb.LinkConnectionState_LINK_CONNECTION_STATE_DISCONNECTED,
			0,
		),
		seenAt,
	)
	hub := NewHub(store)
	handler := newHTTPHandler(store, hub)

	response := httptest.NewRecorder()
	handler.ServeHTTP(
		response,
		httptest.NewRequest(http.MethodGet, "/api/snapshot", nil),
	)
	if response.Code != http.StatusOK {
		t.Fatalf("snapshot status = %d", response.Code)
	}
	var snapshot ClusterSnapshot
	if err := json.NewDecoder(response.Body).Decode(&snapshot); err != nil {
		t.Fatalf("decode snapshot: %v", err)
	}
	if len(snapshot.Nodes) != 1 || snapshot.Nodes[0].Status != NodeNormal ||
		snapshot.Nodes[0].Links[0].Status != LinkDisconnected {
		t.Fatalf("snapshot = %#v", snapshot)
	}

	server := httptest.NewServer(handler)
	defer server.Close()
	websocketURL := "ws" + strings.TrimPrefix(server.URL, "http") + "/ws"
	connection, _, err := websocket.DefaultDialer.Dial(websocketURL, nil)
	if err != nil {
		t.Fatalf("dial websocket: %v", err)
	}
	defer connection.Close()
	if err := connection.SetReadDeadline(time.Now().Add(time.Second)); err != nil {
		t.Fatal(err)
	}
	if err := connection.ReadJSON(&snapshot); err != nil {
		t.Fatalf("read websocket snapshot: %v", err)
	}
	if len(snapshot.Nodes) != 1 || snapshot.Nodes[0].NodeID != "node-b" {
		t.Fatalf("websocket snapshot = %#v", snapshot)
	}
}
