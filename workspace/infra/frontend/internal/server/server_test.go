package server

import (
	"context"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
	"google.golang.org/protobuf/proto"
	"uestcradar/frontend/internal/preview"
	pb "uestcradar/frontend/internal/telemetrypb"
)

func TestNodeHTTPAndProxyPrefix(t *testing.T) {
	store := NewStore()
	frames := preview.NewService("test-node")
	defer frames.Close()
	handler := newHTTPHandler(store, NewHub(store), frames)
	for _, prefix := range []string{"/", "/nodes/test/"} {
		mux := http.NewServeMux()
		mux.Handle(prefix, http.StripPrefix(prefix[:len(prefix)-1], handler))
		server := httptest.NewServer(mux)
		for _, path := range []string{"", "api/node", "api/snapshot", "healthz"} {
			response, err := http.Get(server.URL + prefix + path)
			if err != nil {
				t.Fatal(err)
			}
			if response.StatusCode != 200 {
				t.Fatalf("%s: %s", path, response.Status)
			}
			if path == "" {
				body, err := io.ReadAll(response.Body)
				if err != nil {
					t.Fatal(err)
				}
				assets := strings.Split(string(body), "=\"./assets/")[1:]
				if len(assets) < 2 {
					t.Fatal("UI assets missing; build UI before testing server")
				}
				for _, asset := range assets {
					name := strings.SplitN(asset, "\"", 2)[0]
					res, err := http.Get(server.URL + prefix + "assets/" + name)
					if err != nil {
						t.Fatal(err)
					}
					res.Body.Close()
					if res.StatusCode != 200 {
						t.Fatalf("asset %s: %s", name, res.Status)
					}
				}
			}
			if path == "api/node" {
				var node preview.Node
				if json.NewDecoder(response.Body).Decode(&node) != nil || node.NodeID != "test-node" || node.Connected {
					t.Fatalf("node: %+v", node)
				}
			}
			response.Body.Close()
		}
		_, response, err := websocket.DefaultDialer.Dial("ws"+server.URL[4:]+prefix+"ws/frames", http.Header{"Origin": []string{"https://foreign.invalid"}})
		if err == nil || response == nil || response.StatusCode != 403 {
			t.Fatal("foreign Origin accepted")
		}
		response.Body.Close()
		conn, _, err := websocket.DefaultDialer.Dial("ws"+server.URL[4:]+prefix+"ws/frames", nil)
		if err != nil {
			t.Fatal(err)
		}
		conn.Close()
		server.Close()
	}
}

func TestClosedTelemetryBrowserIsReleasedWithoutNewData(t *testing.T) {
	hub := NewHub(NewStore())
	server := httptest.NewServer(http.HandlerFunc(hub.ServeWebSocket))
	defer server.Close()
	conn, _, err := websocket.DefaultDialer.Dial("ws"+server.URL[4:], nil)
	if err != nil {
		t.Fatal(err)
	}
	if _, _, err := conn.ReadMessage(); err != nil {
		t.Fatal(err)
	}
	_ = conn.Close()
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		hub.mu.Lock()
		count := len(hub.clients)
		hub.mu.Unlock()
		if count == 0 {
			return
		}
		time.Sleep(time.Millisecond)
	}
	t.Fatal("closed telemetry browser was retained")
}

func TestTelemetryBindingAndCancellation(t *testing.T) {
	udp, err := net.ListenPacket("udp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer udp.Close()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	store := NewStore()
	done := make(chan error, 1)
	go func() { done <- receiveUDP(ctx, udp, "ours", store, NewHub(store)) }()
	client, err := net.Dial("udp", udp.LocalAddr().String())
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()
	for _, id := range []string{"foreign", "ours"} {
		data, err := proto.Marshal(&pb.TelemetryPacket{Heartbeat: &pb.NodeHeartbeat{NodeId: id, InstanceId: "instance", Sequence: 1}})
		if err != nil {
			t.Fatal(err)
		}
		if _, err := client.Write(data); err != nil {
			t.Fatal(err)
		}
	}
	deadline := time.Now().Add(time.Second)
	for len(store.Snapshot(time.Now()).Nodes) == 0 && time.Now().Before(deadline) {
		time.Sleep(time.Millisecond)
	}
	nodes := store.Snapshot(time.Now()).Nodes
	if len(nodes) != 1 || nodes[0].NodeID != "ours" {
		t.Fatalf("foreign node leak or missing telemetry: %+v", nodes)
	}
	cancel()
	select {
	case err := <-done:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(time.Second):
		t.Fatal("UDP did not stop")
	}
}
