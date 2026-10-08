package server

import "testing"

func TestConfig(t *testing.T) {
	for _, key := range []string{"NODE_ID", "TELEMETRY_HTTP_ADDR", "TELEMETRY_UDP_ADDR", "PREVIEW_TCP_ADDR", "TELEMETRY_TLS_CERT_FILE", "TELEMETRY_TLS_KEY_FILE", "TELEMETRY_ALLOW_INSECURE_HTTP"} {
		t.Setenv(key, "")
	}
	if _, err := ConfigFromEnv(); err == nil {
		t.Fatal("missing NODE_ID accepted")
	}
	t.Setenv("NODE_ID", "local-source")
	c, err := ConfigFromEnv()
	if err != nil || c.HTTPAddress != "127.0.0.1:8081" {
		t.Fatalf("defaults: %+v %v", c, err)
	}
	for _, address := range []string{"localhost", "127.0.0.1:0", ":65536", ":bad", "0.0.0.0:8081"} {
		bad := c
		bad.HTTPAddress = address
		if bad.Validate() == nil {
			t.Fatalf("accepted %q", address)
		}
	}
	c.TLSCertFile = "cert.pem"
	if c.Validate() == nil {
		t.Fatal("partial TLS config accepted")
	}
}
