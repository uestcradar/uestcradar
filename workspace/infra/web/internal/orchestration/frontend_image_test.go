package orchestration

import (
	"crypto/tls"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"regexp"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
	webassets "uestcradar/telemetry/web"
)

// Run with the pinned Frontend image on HTTPS 127.0.0.1:8081, NODE_ID=smoke-node,
// using the operator-selected unverified node TLS mode. Browser TLS is separate.
func TestPinnedFrontendImageHTTPS(t *testing.T) {
	if os.Getenv("FRONTEND_IMAGE_SMOKE") != "1" {
		t.Skip("requires running ARM64 Frontend image on HTTPS")
	}
	service := &Service{remote: &frontendBrowserRemote{}, sessions: NewSessionStore(), discovered: map[string]NodeInspection{"127.0.0.1": {IP: "127.0.0.1"}}}
	session, err := service.sessions.Create(Credentials{Username: "root", Password: []byte("test-only")})
	if err != nil {
		t.Fatal(err)
	}
	defer service.sessions.Delete(session.ID)
	session.Nodes["127.0.0.1"] = NodeInspection{IP: "127.0.0.1", NodeID: "smoke-node", Reachable: true, InspectedAt: time.Now()}
	session.TrustedKeys["127.0.0.1"] = "test-only"
	web := httptest.NewServer(service)
	defer web.Close()
	base := web.URL + "/api/v1/nodes/127.0.0.1/frontend/"
	get := func(resource string) []byte {
		t.Helper()
		response := frontendGet(t, session, base+resource, web.URL)
		defer response.Body.Close()
		body, err := io.ReadAll(response.Body)
		if err != nil || response.StatusCode != http.StatusOK {
			t.Fatalf("%s: %d %v %s", resource, response.StatusCode, err, body)
		}
		return body
	}
	page := get("")
	assets := regexp.MustCompile(`(?:src|href)="\./(assets/[^"]+)"`).FindAllSubmatch(page, -1)
	if len(assets) < 2 {
		t.Fatalf("missing relative JS/CSS assets: %s", page)
	}
	for _, asset := range assets {
		get(string(asset[1]))
	}
	var node struct {
		NodeID string `json:"node_id"`
	}
	if err := json.Unmarshal(get("api/node"), &node); err != nil || node.NodeID != "smoke-node" {
		t.Fatalf("node: %+v %v", node, err)
	}
	get("healthz")
	conn, _, err := websocket.DefaultDialer.Dial("ws"+strings.TrimPrefix(base, "http")+"ws/frames", http.Header{"Origin": []string{web.URL}, "Cookie": []string{sessionCookieName + "=" + session.ID}})
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Close()
	if info := os.Getenv("FRONTEND_BROWSER_INFO"); info != "" {
		serveFrontendBrowserFixture(t, service, session, info)
	}
	session.cancel()
	conn.SetReadDeadline(time.Now().Add(3 * time.Second))
	for {
		_, _, err = conn.ReadMessage()
		if err != nil {
			break
		}
	}
	if timeout, ok := err.(interface{ Timeout() bool }); ok && timeout.Timeout() {
		t.Fatal("real Frontend WS did not close with session")
	}
}

type frontendBrowserRemote struct{ fakeRemote }

func (*frontendBrowserRemote) Inspect(session *Session, ip string, output CommandOutput) (NodeInspection, error) {
	if output != nil {
		output("stdout", "browser fixture inspection (not real SSH)\n")
	}
	session.mu.Lock()
	defer session.mu.Unlock()
	return session.Nodes[ip], nil
}

// Browser-only fixture: real Web assets/proxy and pinned Frontend, synthetic
// session/inspection and empty telemetry. It cannot stand in for managed-chain acceptance.
func serveFrontendBrowserFixture(t *testing.T, service *Service, session *Session, info string) {
	t.Helper()
	empty := []byte(`{"generated_at":"","nodes":[]}`)
	telemetry := func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/api/snapshot" {
			w.Header().Set("Content-Type", "application/json")
			w.Write(empty)
			return
		}
		upgrader := websocket.Upgrader{}
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			return
		}
		defer conn.Close()
		conn.WriteMessage(websocket.TextMessage, empty)
		for {
			if _, _, err := conn.ReadMessage(); err != nil {
				return
			}
		}
	}
	service.nodeTelemetry = func(w http.ResponseWriter, r *http.Request, _ func() string) { telemetry(w, r) }
	mux := http.NewServeMux()
	mux.Handle("/api/v1/", service)
	mux.HandleFunc("/api/snapshot", telemetry)
	mux.HandleFunc("/ws", telemetry)
	mux.Handle("/", http.FileServer(http.FS(webassets.Files())))
	server := httptest.NewUnstartedServer(mux)
	server.Listener.Close()
	listener, err := net.Listen("tcp", os.Getenv("FRONTEND_BROWSER_ADDR"))
	if err != nil {
		t.Fatal(err)
	}
	server.Listener = listener
	certificate, err := tls.LoadX509KeyPair("/tls/server.crt", "/tls/server.key")
	if err != nil {
		listener.Close()
		t.Fatal(err)
	}
	server.TLS = &tls.Config{Certificates: []tls.Certificate{certificate}, MinVersion: tls.VersionTLS12}
	server.StartTLS()
	defer server.Close()
	metadata, err := json.Marshal(map[string]string{"url": server.URL, "session": session.ID})
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(info, metadata, 0600); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(2 * time.Minute)
	for time.Now().Before(deadline) {
		if result, err := os.ReadFile(info + ".done"); err == nil {
			if string(result) != "PASS" {
				t.Fatal("browser checks failed")
			}
			return
		}
		time.Sleep(100 * time.Millisecond)
	}
	t.Fatal("browser verification not acknowledged")
}
