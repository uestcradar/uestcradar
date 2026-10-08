package server

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"time"

	"google.golang.org/protobuf/proto"
	"uestcradar/frontend/internal/preview"
	pb "uestcradar/frontend/internal/telemetrypb"
	webassets "uestcradar/frontend/web"
)

func Run(parent context.Context, config Config) error {
	if err := config.Validate(); err != nil {
		return err
	}
	ctx, cancel := context.WithCancel(parent)
	defer cancel()
	httpListener, err := net.Listen("tcp", config.HTTPAddress)
	if err != nil {
		return fmt.Errorf("listen HTTP: %w", err)
	}
	defer httpListener.Close()
	udp, err := net.ListenPacket("udp", config.UDPAddress)
	if err != nil {
		return fmt.Errorf("listen telemetry: %w", err)
	}
	defer udp.Close()
	tcp, err := net.Listen("tcp", config.PreviewTCPAddress)
	if err != nil {
		return fmt.Errorf("listen preview: %w", err)
	}
	defer tcp.Close()
	store := NewStore()
	hub := NewHub(store)
	frames := preview.NewService(config.NodeID)
	defer frames.Close()
	server := &http.Server{Handler: newHTTPHandler(store, hub, frames), ReadHeaderTimeout: 5 * time.Second}
	go hub.Run(ctx)
	go func() {
		ticker := time.NewTicker(100 * time.Millisecond)
		defer ticker.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case now := <-ticker.C:
				if store.MarkOffline(now, 3*time.Second) {
					hub.Notify()
				}
			}
		}
	}()
	errorsCh := make(chan error, 3)
	go func() { errorsCh <- frames.ServeTCP(ctx, tcp) }()
	go func() { errorsCh <- receiveUDP(ctx, udp, config.NodeID, store, hub) }()
	go func() {
		if config.TLSCertFile != "" {
			errorsCh <- server.ServeTLS(httpListener, config.TLSCertFile, config.TLSKeyFile)
		} else {
			errorsCh <- server.Serve(httpListener)
		}
	}()
	select {
	case <-parent.Done():
		err = nil
	case err = <-errorsCh:
	}
	cancel()
	shutdown, stop := context.WithTimeout(context.Background(), 3*time.Second)
	defer stop()
	if shutdownErr := server.Shutdown(shutdown); shutdownErr != nil {
		_ = server.Close()
		if err == nil {
			err = shutdownErr
		}
	}
	if errors.Is(err, http.ErrServerClosed) || errors.Is(err, net.ErrClosed) {
		return nil
	}
	return err
}

func newHTTPHandler(store *Store, hub *Hub, frames *preview.Service) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /api/node", func(w http.ResponseWriter, r *http.Request) { writeJSON(w, frames.Node()) })
	mux.HandleFunc("GET /api/snapshot", func(w http.ResponseWriter, r *http.Request) { writeJSON(w, store.Snapshot(time.Now())) })
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) { writeJSON(w, map[string]string{"status": "ready"}) })
	mux.HandleFunc("/ws", hub.ServeWebSocket)
	mux.Handle("/ws/frames", frames)
	mux.Handle("/", http.FileServer(http.FS(webassets.Files())))
	return mux
}

func writeJSON(w http.ResponseWriter, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	if err := json.NewEncoder(w).Encode(value); err != nil {
		http.Error(w, "encode response", http.StatusInternalServerError)
	}
}

func receiveUDP(ctx context.Context, connection net.PacketConn, nodeID string, store *Store, hub *Hub) error {
	stop := context.AfterFunc(ctx, func() { _ = connection.Close() })
	defer stop()
	buffer := make([]byte, 64*1024)
	for {
		size, _, err := connection.ReadFrom(buffer)
		if err != nil {
			if ctx.Err() != nil {
				return nil
			}
			return err
		}
		packet := &pb.TelemetryPacket{}
		if proto.Unmarshal(buffer[:size], packet) != nil || packet.Heartbeat == nil || packet.Heartbeat.NodeId != nodeID {
			continue
		}
		if store.UpdateHeartbeat(packet.Heartbeat, time.Now()) {
			hub.Notify()
		}
	}
}
