package orchestration

import (
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"io"
	"math/big"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
)

func frontendFixture(t *testing.T, certificateMode string, handler http.Handler) (*Service, *Session, string) {
	t.Helper()
	pub, key, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	cert := &x509.Certificate{SerialNumber: big.NewInt(1), Subject: pkix.Name{CommonName: "test-only"}, NotBefore: time.Now().Add(-time.Hour), NotAfter: time.Now().Add(time.Hour), IPAddresses: []net.IP{net.ParseIP("127.0.0.1")}, KeyUsage: x509.KeyUsageDigitalSignature | x509.KeyUsageCertSign, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}, IsCA: true, BasicConstraintsValid: true}
	if certificateMode == "expired" {
		cert.NotAfter = time.Now().Add(-time.Minute)
	}
	if certificateMode == "wrong-san" {
		cert.IPAddresses = []net.IP{net.ParseIP("127.0.0.2")}
	}
	der, err := x509.CreateCertificate(rand.Reader, cert, cert, pub, key)
	if err != nil {
		t.Fatal(err)
	}
	backend := httptest.NewUnstartedServer(handler)
	backend.TLS = &tls.Config{Certificates: []tls.Certificate{{Certificate: [][]byte{der}, PrivateKey: key}}}
	backend.StartTLS()
	t.Cleanup(backend.Close)
	tr := frontendHTTPTransport.Clone()
	tr.TLSClientConfig.RootCAs = x509.NewCertPool()
	parsed, err := x509.ParseCertificate(der)
	if err != nil {
		t.Fatal(err)
	}
	if certificateMode != "untrusted" {
		tr.TLSClientConfig.RootCAs.AddCert(parsed)
	}
	tr.DialContext = func(ctx context.Context, network, address string) (net.Conn, error) {
		if address != "127.0.0.1:8081" {
			t.Errorf("unexpected target %s", address)
		}
		return (&net.Dialer{Timeout: time.Second}).DialContext(ctx, network, backend.Listener.Addr().String())
	}
	s := &Service{sessions: NewSessionStore(), frontendTransport: tr, discovered: map[string]NodeInspection{"127.0.0.1": {IP: "127.0.0.1"}}}
	session, err := s.sessions.Create(Credentials{Password: []byte("test-only")})
	if err != nil {
		t.Fatal(err)
	}
	session.Nodes["127.0.0.1"] = NodeInspection{IP: "127.0.0.1", Reachable: true, InspectedAt: time.Now()}
	session.TrustedKeys["127.0.0.1"] = "test-only-fingerprint"
	web := httptest.NewServer(s)
	t.Cleanup(web.Close)
	t.Cleanup(func() { s.sessions.Delete(session.ID); tr.CloseIdleConnections() })
	return s, session, web.URL
}

func frontendGet(t *testing.T, session *Session, address, origin string) *http.Response {
	t.Helper()
	req, err := http.NewRequest(http.MethodGet, address, nil)
	if err != nil {
		t.Fatal(err)
	}
	if session != nil {
		req.AddCookie(&http.Cookie{Name: sessionCookieName, Value: session.ID})
	}
	req.Header.Set("Origin", origin)
	req.Header.Set("Authorization", "Bearer test-only")
	req.Header.Set("X-CSRF-Token", "test-only")
	req.Header.Set("Proxy-Authorization", "Basic test-only")
	req.Header.Set("Forwarded", "for=192.0.2.1")
	req.Header.Set("X-Forwarded-Host", "foreign.example")
	client := &http.Client{Timeout: 5 * time.Second, CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}
	resp, err := client.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { resp.Body.Close() })
	return resp
}

func TestFrontendProxyHTTPAndBoundaries(t *testing.T) {
	_, session, web := frontendFixture(t, "valid", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		for _, h := range []string{"Cookie", "Authorization", "X-CSRF-Token", "Proxy-Authorization", "Forwarded", "X-Forwarded-Host"} {
			if r.Header.Get(h) != "" {
				t.Errorf("forwarded %s", h)
			}
		}
		if r.Header.Get("Origin") != "http://"+r.Host {
			t.Error("public Host/Origin not preserved")
		}
		w.Header().Set("Set-Cookie", "uestcradar_session=bad")
		io.WriteString(w, r.URL.Path)
	}))
	base := web + "/api/v1/nodes/127.0.0.1/frontend"
	for _, resource := range []string{"/", "/index.html", "/assets/app.js", "/api/node", "/healthz"} {
		resp := frontendGet(t, session, base+resource, web)
		body, _ := io.ReadAll(resp.Body)
		resp.Body.Close()
		if resp.StatusCode != 200 || string(body) != resource || resp.Header.Get("Set-Cookie") != "" {
			t.Fatalf("proxy %s: %d %q", resource, resp.StatusCode, body)
		}
	}
	for _, tc := range []struct {
		path, origin string
		session      *Session
		status       int
	}{
		{base + "/", web, nil, 401},
		{base + "/", "https://foreign.example", session, 403},
		{web + "/api/v1/nodes/192.0.2.1/frontend/", web, session, 403},
		{base + "/api/v1/session", web, session, 404},
		{base + "/assets/%2e%2e/api/node", web, session, 400},
		{base + "/assets//app.js", web, session, 400},
		{base, web, session, 308},
	} {
		resp := frontendGet(t, tc.session, tc.path, tc.origin)
		if resp.StatusCode != tc.status {
			t.Errorf("%s: %d want %d", tc.path, resp.StatusCode, tc.status)
		}
		resp.Body.Close()
	}
}

func TestFrontendRequiresSuccessfulInspection(t *testing.T) {
	for _, mode := range []string{"missing", "unreachable", "unchecked", "untrusted", "host-key", "error"} {
		t.Run(mode, func(t *testing.T) {
			_, session, web := frontendFixture(t, "valid", http.HandlerFunc(func(http.ResponseWriter, *http.Request) { t.Error("unauthorized request reached backend") }))
			session.mu.Lock()
			node := session.Nodes["127.0.0.1"]
			switch mode {
			case "unreachable":
				node.Reachable = false
			case "unchecked":
				node.InspectedAt = time.Time{}
			case "host-key":
				node.HostKeyRequired = true
			case "error":
				node.Error = "inspection failed"
			case "untrusted":
				delete(session.TrustedKeys, "127.0.0.1")
			}
			session.Nodes["127.0.0.1"] = node
			if mode == "missing" {
				delete(session.Nodes, "127.0.0.1")
			}
			session.mu.Unlock()
			resp := frontendGet(t, session, web+"/api/v1/nodes/127.0.0.1/frontend/", web)
			if resp.StatusCode != http.StatusForbidden {
				t.Fatalf("got %d", resp.StatusCode)
			}
		})
	}
}

func TestFrontendLANModeAcceptsUnverifiedCertificates(t *testing.T) {
	for _, mode := range []string{"untrusted", "expired", "wrong-san"} {
		t.Run(mode, func(t *testing.T) {
			_, session, web := frontendFixture(t, mode, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				if r.TLS == nil {
					t.Error("encryption was disabled")
				}
				w.WriteHeader(http.StatusOK)
			}))
			resp := frontendGet(t, session, web+"/api/v1/nodes/127.0.0.1/frontend/", web)
			if resp.StatusCode != 200 {
				t.Fatalf("got %d", resp.StatusCode)
			}
		})
	}
}

func TestFrontendWebSocketEndsWithSession(t *testing.T) {
	for _, mode := range []string{"logout", "expiry", "browser-close"} {
		t.Run(mode, func(t *testing.T) {
			done := make(chan struct{})
			s, session, web := frontendFixture(t, "valid", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				defer close(done)
				upgrader := websocket.Upgrader{}
				conn, err := upgrader.Upgrade(w, r, nil)
				if err != nil {
					t.Error(err)
					return
				}
				defer conn.Close()
				if err = conn.WriteMessage(websocket.BinaryMessage, []byte{1, 2, 3}); err != nil {
					t.Error(err)
					return
				}
				for {
					if _, _, err = conn.ReadMessage(); err != nil {
						return
					}
				}
			}))
			headers := http.Header{"Origin": []string{web}, "Cookie": []string{sessionCookieName + "=" + session.ID}}
			conn, _, err := websocket.DefaultDialer.Dial("ws"+strings.TrimPrefix(web, "http")+"/api/v1/nodes/127.0.0.1/frontend/ws/frames", headers)
			if err != nil {
				t.Fatal(err)
			}
			defer conn.Close()
			conn.SetReadDeadline(time.Now().Add(3 * time.Second))
			kind, data, err := conn.ReadMessage()
			if err != nil || kind != websocket.BinaryMessage || string(data) != string([]byte{1, 2, 3}) {
				t.Fatalf("binary frame %d %v %v", kind, data, err)
			}
			if mode == "browser-close" {
				conn.Close()
			} else if mode == "logout" {
				s.sessions.Delete(session.ID)
			} else {
				s.sessions.mu.Lock()
				session.ExpiresAt = time.Now().Add(-time.Second)
				s.sessions.mu.Unlock()
				s.sessions.expire(session.ID)
			}
			_, _, err = conn.ReadMessage()
			if err == nil {
				t.Fatal("connection retained")
			}
			if timeout, ok := err.(net.Error); ok && timeout.Timeout() {
				t.Fatal("session failed to close websocket")
			}
			select {
			case <-done:
			case <-time.After(3 * time.Second):
				t.Fatal("backend connection leaked")
			}
		})
	}
}

func TestFrontendPendingRequestDoesNotBlockManagement(t *testing.T) {
	entered, release := make(chan struct{}), make(chan struct{})
	_, session, web := frontendFixture(t, "valid", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		close(entered)
		select {
		case <-release:
		case <-r.Context().Done():
		}
		io.WriteString(w, "ok")
	}))
	request, _ := http.NewRequest(http.MethodGet, web+"/api/v1/nodes/127.0.0.1/frontend/", nil)
	request.AddCookie(&http.Cookie{Name: sessionCookieName, Value: session.ID})
	done := make(chan error, 1)
	go func() {
		response, err := (&http.Client{Timeout: 6 * time.Second}).Do(request)
		if response != nil {
			io.Copy(io.Discard, response.Body)
			response.Body.Close()
		}
		done <- err
	}()
	defer func() {
		close(release)
		select {
		case err := <-done:
			if err != nil {
				t.Error(err)
			}
		case <-time.After(7 * time.Second):
			t.Error("request leaked")
		}
	}()
	select {
	case <-entered:
	case <-time.After(3 * time.Second):
		t.Fatal("backend not reached")
	}
	management, _ := http.NewRequest(http.MethodGet, web+"/api/v1/session", nil)
	management.AddCookie(&http.Cookie{Name: sessionCookieName, Value: session.ID})
	response, err := (&http.Client{Timeout: time.Second}).Do(management)
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		t.Fatalf("management status %d", response.StatusCode)
	}
}

func TestFrontendTelemetryUsesInspectedBinding(t *testing.T) {
	s, session, web := frontendFixture(t, "valid", http.HandlerFunc(func(http.ResponseWriter, *http.Request) { t.Error("telemetry reached Frontend") }))
	s.nodeTelemetry = func(w http.ResponseWriter, r *http.Request, resolve func() string) {
		if r.URL.Path != "/api/snapshot" {
			t.Error(r.URL.Path)
		}
		io.WriteString(w, resolve())
	}
	for _, id := range []string{"", "node-1", "node-2", ""} {
		session.mu.Lock()
		node := session.Nodes["127.0.0.1"]
		node.NodeID = id
		session.Nodes[node.IP] = node
		session.mu.Unlock()
		response := frontendGet(t, session, web+"/api/v1/nodes/127.0.0.1/frontend/api/snapshot?node_id=foreign", web)
		body, _ := io.ReadAll(response.Body)
		response.Body.Close()
		if id == "" {
			if response.StatusCode != 503 {
				t.Fatal("empty binding did not fail closed")
			}
		} else if response.StatusCode != 200 || string(body) != id {
			t.Fatalf("wrong node %q", body)
		}
	}
}

func TestFrontendResponseTimeout(t *testing.T) {
	s, session, web := frontendFixture(t, "valid", http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { <-r.Context().Done() }))
	s.frontendTransport.(*http.Transport).ResponseHeaderTimeout = 100 * time.Millisecond
	resp := frontendGet(t, session, web+"/api/v1/nodes/127.0.0.1/frontend/", web)
	if resp.StatusCode != 502 {
		t.Fatalf("got %d", resp.StatusCode)
	}
}

func TestFrontendSlowWriteIsBounded(t *testing.T) {
	writer, reader := net.Pipe()
	defer writer.Close()
	defer reader.Close()
	done := make(chan error, 1)
	go func() { _, err := (frontendConn{writer}).Write([]byte{1}); done <- err }()
	select {
	case err := <-done:
		timeout, ok := err.(net.Error)
		if !ok || !timeout.Timeout() {
			t.Fatalf("expected timeout, got %v", err)
		}
	case <-time.After(8 * time.Second):
		t.Fatal("slow write did not time out")
	}
}
