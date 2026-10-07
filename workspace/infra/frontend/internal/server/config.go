package server

import (
	"fmt"
	"net"
	"os"
	"strconv"
	"strings"
)

type Config struct {
	NodeID            string
	HTTPAddress       string
	UDPAddress        string
	PreviewTCPAddress string
	TLSCertFile       string
	TLSKeyFile        string
	AllowInsecureHTTP bool
}

func ConfigFromEnv() (Config, error) {
	c := Config{
		NodeID:            os.Getenv("NODE_ID"),
		HTTPAddress:       envOr("TELEMETRY_HTTP_ADDR", "127.0.0.1:8081"),
		UDPAddress:        envOr("TELEMETRY_UDP_ADDR", "127.0.0.1:9902"),
		PreviewTCPAddress: envOr("PREVIEW_TCP_ADDR", "127.0.0.1:9903"),
		TLSCertFile:       os.Getenv("TELEMETRY_TLS_CERT_FILE"),
		TLSKeyFile:        os.Getenv("TELEMETRY_TLS_KEY_FILE"),
		AllowInsecureHTTP: os.Getenv("TELEMETRY_ALLOW_INSECURE_HTTP") == "true",
	}
	return c, c.Validate()
}

func (c Config) Validate() error {
	if c.NodeID == "" || len(c.NodeID) > 128 || strings.TrimSpace(c.NodeID) != c.NodeID || strings.ContainsAny(c.NodeID, "\x00\r\n") {
		return fmt.Errorf("NODE_ID must be a nonempty identifier of at most 128 bytes")
	}
	for _, address := range []string{c.HTTPAddress, c.UDPAddress, c.PreviewTCPAddress} {
		_, port, err := net.SplitHostPort(address)
		if err != nil {
			return fmt.Errorf("invalid listener %q: %w", address, err)
		}
		n, err := strconv.Atoi(port)
		if err != nil || n < 1 || n > 65535 {
			return fmt.Errorf("invalid listener port %q", port)
		}
	}
	if (c.TLSCertFile == "") != (c.TLSKeyFile == "") {
		return fmt.Errorf("TLS certificate and key must be configured together")
	}
	host, _, _ := net.SplitHostPort(c.HTTPAddress)
	if c.TLSCertFile == "" && !c.AllowInsecureHTTP && host != "localhost" && !net.ParseIP(host).IsLoopback() {
		return fmt.Errorf("TLS certificate and key are required for non-loopback HTTP")
	}
	return nil
}

func envOr(key, fallback string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return fallback
}
