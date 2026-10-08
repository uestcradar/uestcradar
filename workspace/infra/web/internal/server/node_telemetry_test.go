package server

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/gorilla/websocket"
	pb "uestcradar/telemetry/internal/telemetrypb"
)

func TestNodeTelemetryIsolationAndLifetime(t *testing.T) {
	store := NewStore()
	for _, id := range []string{"a", "b"} {
		store.UpdateHeartbeat(&pb.NodeHeartbeat{NodeId: id, InstanceId: "instance-" + id, Sequence: 1}, time.Now())
	}
	hub := NewHub(store)
	var binding atomic.Value
	binding.Store("a")
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/global" {
			hub.ServeWebSocket(w, r)
			return
		}
		hub.ServeNodeTelemetry(w, r.WithContext(ctx), func() string { return binding.Load().(string) })
	}))
	defer server.Close()
	read := func(conn *websocket.Conn, ids ...string) {
		t.Helper()
		conn.SetReadDeadline(time.Now().Add(2 * time.Second))
		var snapshot ClusterSnapshot
		if err := conn.ReadJSON(&snapshot); err != nil {
			t.Fatal(err)
		}
		if len(snapshot.Nodes) != len(ids) {
			t.Fatalf("nodes %#v, want %v", snapshot.Nodes, ids)
		}
		for i, id := range ids {
			if snapshot.Nodes[i].NodeID != id {
				t.Fatalf("foreign node %s", snapshot.Nodes[i].NodeID)
			}
		}
	}
	dial := func(path string) *websocket.Conn {
		t.Helper()
		c, _, err := websocket.DefaultDialer.Dial("ws"+strings.TrimPrefix(server.URL, "http")+path, nil)
		if err != nil {
			t.Fatal(err)
		}
		return c
	}
	global := dial("/global")
	defer global.Close()
	node := dial("/ws")
	defer node.Close()
	read(global, "a", "b")
	read(node, "a")
	response, err := http.Get(server.URL + "/api/snapshot")
	if err != nil {
		t.Fatal(err)
	}
	var snapshot ClusterSnapshot
	err = json.NewDecoder(response.Body).Decode(&snapshot)
	response.Body.Close()
	if err != nil || len(snapshot.Nodes) != 1 || snapshot.Nodes[0].NodeID != "a" {
		t.Fatalf("snapshot %#v %v", snapshot, err)
	}
	binding.Store("b")
	hub.broadcastSnapshot(time.Now())
	read(node, "b")
	read(global, "a", "b")
	binding.Store("")
	hub.broadcastSnapshot(time.Now())
	read(node)
	read(global, "a", "b")
	cancel()
	node.SetReadDeadline(time.Now().Add(time.Second))
	_, _, err = node.ReadMessage()
	if err == nil {
		t.Fatal("canceled subscription retained")
	}
	if timeout, ok := err.(interface{ Timeout() bool }); ok && timeout.Timeout() {
		t.Fatal("cancellation did not close socket")
	}
	global.Close()
	deadline := time.Now().Add(2 * time.Second)
	for {
		hub.mu.Lock()
		count := len(hub.clients)
		hub.mu.Unlock()
		if count == 0 {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("browser close retained subscriptions")
		}
		time.Sleep(5 * time.Millisecond)
	}
}
