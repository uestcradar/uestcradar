package server

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"os"
	"strings"
	"time"

	"google.golang.org/protobuf/proto"

	"uestcradar/telemetry/internal/orchestration"
	previewserver "uestcradar/telemetry/internal/preview"
	pb "uestcradar/telemetry/internal/telemetrypb"
	webassets "uestcradar/telemetry/web"
)

const (
	nodeLeaseTTL     = 3 * time.Second
	nodeScanInterval = 100 * time.Millisecond
)

// Config controls UDP ingestion and HTTP/WebSocket serving.
type Config struct {
	UDPAddress        string
	PreviewTCPAddress string
	HTTPAddress       string
	TLSCertFile       string
	TLSKeyFile        string
	AdvertiseHost     string
	AllowInsecureHTTP bool
}

// ConfigFromEnv returns server configuration from environment variables.
func ConfigFromEnv() Config {
	return Config{
		UDPAddress:        envOr("TELEMETRY_UDP_ADDR", ":9900"),
		PreviewTCPAddress: envOr("PREVIEW_TCP_ADDR", ":9901"),
		HTTPAddress:       envOr("TELEMETRY_HTTP_ADDR", ":8080"),
		TLSCertFile:       os.Getenv("TELEMETRY_TLS_CERT_FILE"),
		TLSKeyFile:        os.Getenv("TELEMETRY_TLS_KEY_FILE"),
		AdvertiseHost:     os.Getenv("TELEMETRY_ADVERTISE_HOST"),
		AllowInsecureHTTP: os.Getenv("TELEMETRY_ALLOW_INSECURE_HTTP") == "true",
	}
}

// Run receives protobuf datagrams and serves the embedded dashboard.
func Run(parent context.Context, config Config) error {
	ctx, cancel := context.WithCancel(parent)
	defer cancel()

	store := NewStore()
	hub := NewHub(store)
	secureHTTP := config.TLSCertFile != "" && config.TLSKeyFile != ""
	if !secureHTTP && !config.AllowInsecureHTTP && !loopbackAddress(config.HTTPAddress) {
		return fmt.Errorf("TLS certificate and key are required for non-loopback HTTP")
	}
	orchestrator := orchestration.NewService(config.AdvertiseHost, secureHTTP)
	preview := previewserver.NewService()
	go hub.Run(ctx)
	go scanNodeLeases(ctx, store, hub, nodeLeaseTTL, nodeScanInterval)

	httpServer := &http.Server{
		Addr:              config.HTTPAddress,
		Handler:           newHTTPHandler(store, hub, orchestrator, preview),
		ReadHeaderTimeout: 5 * time.Second,
	}
	errorsChannel := make(chan error, 3)
	go func() {
		errorsChannel <- preview.RunTCP(ctx, config.PreviewTCPAddress)
	}()
	go func() {
		errorsChannel <- receiveUDP(ctx, config.UDPAddress, store, hub)
	}()
	go func() {
		var err error
		if secureHTTP {
			err = httpServer.ListenAndServeTLS(config.TLSCertFile, config.TLSKeyFile)
		} else {
			err = httpServer.ListenAndServe()
		}
		if errors.Is(err, http.ErrServerClosed) {
			err = nil
		}
		errorsChannel <- err
	}()

	var runError error
	select {
	case <-parent.Done():
	case err := <-errorsChannel:
		if err != nil {
			runError = err
		}
	}

	cancel()
	shutdownContext, shutdownCancel := context.WithTimeout(
		context.Background(),
		3*time.Second,
	)
	defer shutdownCancel()
	if err := httpServer.Shutdown(shutdownContext); err != nil {
		return fmt.Errorf("shutdown HTTP server: %w", err)
	}
	return runError
}

func newHTTPHandler(store *Store, hub *Hub, orchestrationHandlers ...http.Handler) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/api/snapshot", func(writer http.ResponseWriter, _ *http.Request) {
		writeJSON(writer, store.Snapshot(time.Now()))
	})
	mux.HandleFunc("/api/nodes", func(writer http.ResponseWriter, _ *http.Request) {
		writeJSON(writer, store.Snapshot(time.Now()).Nodes)
	})
	mux.HandleFunc("/ws", hub.ServeWebSocket)
	if len(orchestrationHandlers) > 0 && orchestrationHandlers[0] != nil {
		mux.Handle("/api/v1/", orchestrationHandlers[0])
	}
	if len(orchestrationHandlers) > 1 && orchestrationHandlers[1] != nil {
		mux.Handle("/ws/frames", orchestrationHandlers[1])
	}
	mux.Handle("/", http.FileServer(http.FS(webassets.Files())))
	return mux
}

func loopbackAddress(address string) bool {
	host, _, err := net.SplitHostPort(address)
	if err != nil {
		return false
	}
	host = strings.Trim(host, "[]")
	return host == "localhost" || net.ParseIP(host).IsLoopback()
}

func writeJSON(writer http.ResponseWriter, value any) {
	writer.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(writer).Encode(value); err != nil {
		http.Error(writer, err.Error(), http.StatusInternalServerError)
	}
}

func scanNodeLeases(
	ctx context.Context,
	store *Store,
	hub *Hub,
	ttl time.Duration,
	interval time.Duration,
) {
	ticker := time.NewTicker(interval)
	defer ticker.Stop()
	for {
		select {
		case now := <-ticker.C:
			if store.MarkOffline(now, ttl) {
				hub.Notify()
			}
		case <-ctx.Done():
			return
		}
	}
}

func receiveUDP(
	ctx context.Context,
	address string,
	store *Store,
	hub *Hub,
) error {
	connection, err := net.ListenPacket("udp", address)
	if err != nil {
		return fmt.Errorf("listen UDP: %w", err)
	}
	defer func() {
		if closeErr := connection.Close(); closeErr != nil {
			fmt.Fprintf(os.Stderr, "telemetry server: close UDP: %v\n", closeErr)
		}
	}()

	buffer := make([]byte, 64*1024)
	for {
		if err := connection.SetReadDeadline(time.Now().Add(500 * time.Millisecond)); err != nil {
			return fmt.Errorf("set UDP deadline: %w", err)
		}
		size, _, err := connection.ReadFrom(buffer)
		if err != nil {
			if timeout, ok := err.(net.Error); ok && timeout.Timeout() {
				select {
				case <-ctx.Done():
					return nil
				default:
					continue
				}
			}
			return fmt.Errorf("read UDP: %w", err)
		}

		packet := &pb.TelemetryPacket{}
		if err := proto.Unmarshal(buffer[:size], packet); err != nil {
			continue
		}
		receivedAt := time.Now()
		if store.UpdateHeartbeat(packet.Heartbeat, receivedAt) {
			hub.Notify()
		}
	}
}

func envOr(name string, fallback string) string {
	if value := os.Getenv(name); value != "" {
		return value
	}
	return fallback
}
