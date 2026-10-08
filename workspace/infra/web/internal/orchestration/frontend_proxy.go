package orchestration

import (
	"bufio"
	"context"
	"crypto/tls"
	"net"
	"net/http"
	"net/http/httputil"
	"net/url"
	"path"
	"strings"
	"time"
)

// No environment proxy: node data must only go to the approved management IP.
var frontendHTTPTransport = &http.Transport{
	DialContext: (&net.Dialer{Timeout: 5 * time.Second, KeepAlive: 30 * time.Second}).DialContext,
	// Operator-selected trusted-LAN mode: encrypt traffic without verifying node identity.
	TLSClientConfig:        &tls.Config{MinVersion: tls.VersionTLS12, InsecureSkipVerify: true},
	TLSHandshakeTimeout:    5 * time.Second,
	ResponseHeaderTimeout:  5 * time.Second,
	IdleConnTimeout:        30 * time.Second,
	MaxIdleConns:           32,
	MaxResponseHeaderBytes: 1 << 20,
}

func (s *Service) handleFrontend(w http.ResponseWriter, r *http.Request, session *Session) {
	if r.Method != http.MethodGet && r.Method != http.MethodHead {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	if !sameOrigin(r) {
		http.Error(w, "origin not allowed", http.StatusForbidden)
		return
	}
	// Reject escaped or noncanonical paths before stripping the trusted prefix.
	if r.URL.RawPath != "" || strings.TrimSuffix(r.URL.Path, "/") != path.Clean(r.URL.Path) {
		http.Error(w, "invalid frontend path", http.StatusBadRequest)
		return
	}
	parts := strings.SplitN(strings.TrimPrefix(r.URL.Path, "/api/v1/nodes/"), "/", 3)
	if len(parts) < 2 || parts[1] != "frontend" {
		http.NotFound(w, r)
		return
	}
	ip := net.ParseIP(parts[0])
	if ip == nil || ip.To4() == nil || ip.String() != parts[0] || s.validateNodeIPs(session, []string{parts[0]}) != nil {
		http.Error(w, "node unavailable", http.StatusForbidden)
		return
	}
	session.mu.Lock()
	node, found := session.Nodes[parts[0]]
	trusted := session.TrustedKeys[parts[0]] != ""
	session.mu.Unlock()
	if !found || !trusted || !node.Reachable || node.InspectedAt.IsZero() || node.Error != "" || node.HostKeyRequired {
		http.Error(w, "node unavailable", http.StatusForbidden)
		return
	}
	if len(parts) == 2 {
		http.Redirect(w, r, r.URL.Path+"/", http.StatusPermanentRedirect)
		return
	}
	resource := parts[2]
	if resource != "" && resource != "index.html" && resource != "api/node" && resource != "healthz" && resource != "api/snapshot" && resource != "ws" && resource != "ws/frames" && !strings.HasPrefix(resource, "assets/") {
		http.NotFound(w, r)
		return
	}
	ctx, cancel := context.WithCancel(r.Context())
	defer cancel()
	stop := context.AfterFunc(session.ctx, cancel)
	defer stop()
	if resource != "ws/frames" && resource != "ws" {
		var end context.CancelFunc
		ctx, end = context.WithTimeout(ctx, 15*time.Second)
		defer end()
	}
	request := r.Clone(ctx)
	request.URL.Path = "/" + resource
	request.URL.RawPath = ""
	if resource == "api/snapshot" || resource == "ws" {
		resolveNode := func() string {
			session.mu.Lock()
			defer session.mu.Unlock()
			return session.Nodes[parts[0]].NodeID
		}
		if s.nodeTelemetry == nil || resolveNode() == "" {
			http.Error(w, "node telemetry binding unavailable; inspect the deployment", http.StatusServiceUnavailable)
			return
		}
		if resource == "api/snapshot" {
			defer http.NewResponseController(w).SetWriteDeadline(time.Time{})
			s.nodeTelemetry(frontendWriter{w}, request, resolveNode)
		} else {
			s.nodeTelemetry(w, request, resolveNode)
		}
		return
	}
	target := &url.URL{Scheme: "https", Host: net.JoinHostPort(parts[0], "8081")}
	transport := s.frontendTransport
	if transport == nil {
		transport = frontendHTTPTransport
	}
	proxy := &httputil.ReverseProxy{
		Transport: transport,
		Rewrite: func(p *httputil.ProxyRequest) {
			p.SetURL(target)
			// Preserve the public Host for Frontend's existing Origin validation.
			// TLS certificate verification still uses the fixed target URL host.
			p.Out.Host = p.In.Host
			for _, header := range []string{"Cookie", "Authorization", "Proxy-Authorization", "X-CSRF-Token"} {
				p.Out.Header.Del(header)
			}
		},
		ModifyResponse: func(response *http.Response) error {
			response.Header.Del("Set-Cookie")
			return nil
		},
		ErrorHandler: func(w http.ResponseWriter, _ *http.Request, _ error) {
			http.Error(w, "node frontend unavailable", http.StatusBadGateway)
		},
	}
	defer http.NewResponseController(w).SetWriteDeadline(time.Time{})
	proxy.ServeHTTP(frontendWriter{w}, request)
}

// Bound each write, not the lifetime of a healthy streaming connection.
type frontendConn struct{ net.Conn }

func (c frontendConn) Write(p []byte) (int, error) {
	if err := c.SetWriteDeadline(time.Now().Add(5 * time.Second)); err != nil {
		return 0, err
	}
	return c.Conn.Write(p)
}

type frontendWriter struct{ http.ResponseWriter }

func (w frontendWriter) Unwrap() http.ResponseWriter { return w.ResponseWriter }
func (w frontendWriter) Write(p []byte) (int, error) {
	if err := http.NewResponseController(w.ResponseWriter).SetWriteDeadline(time.Now().Add(5 * time.Second)); err != nil {
		return 0, err
	}
	return w.ResponseWriter.Write(p)
}
func (w frontendWriter) Hijack() (net.Conn, *bufio.ReadWriter, error) {
	conn, buffered, err := http.NewResponseController(w.ResponseWriter).Hijack()
	if err != nil {
		return nil, nil, err
	}
	bounded := frontendConn{conn}
	if err := bounded.SetWriteDeadline(time.Now().Add(5 * time.Second)); err != nil {
		conn.Close()
		return nil, nil, err
	}
	if err := buffered.Flush(); err != nil {
		conn.Close()
		return nil, nil, err
	}
	buffered.Writer = bufio.NewWriter(bounded)
	return bounded, buffered, nil
}
