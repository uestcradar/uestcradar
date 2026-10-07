package orchestration

import (
	"context"
	"errors"
	"net"
	"testing"
	"time"
)

func TestDiscoverProbesNodesConcurrently(t *testing.T) {
	started := time.Now()
	results := Discover(context.Background(), []string{"10.0.0.1", "10.0.0.2", "10.0.0.3"}, func(context.Context, string, string) (net.Conn, error) {
		time.Sleep(50 * time.Millisecond)
		return nil, errors.New("unreachable")
	})
	if elapsed := time.Since(started); elapsed >= 130*time.Millisecond {
		t.Fatalf("discovery was not concurrent: %s", elapsed)
	}
	if len(results) != 3 {
		t.Fatalf("unexpected result count: %d", len(results))
	}
}
