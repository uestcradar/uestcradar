package server

import (
	"context"
	"testing"
	"time"
)

func TestHubClientQueueKeepsLatestSnapshot(t *testing.T) {
	store := NewStore()
	hub := NewHub(store)
	client := hub.subscribe()
	defer hub.unsubscribe(client)

	hub.mu.Lock()
	hub.enqueue(client, []byte("old"))
	hub.enqueue(client, []byte("new"))
	hub.mu.Unlock()

	select {
	case got := <-client.send:
		if string(got) != "new" {
			t.Fatalf("queued message = %q", got)
		}
	case <-time.After(time.Second):
		t.Fatal("no queued message")
	}
}

func TestHubNotifyIsNonBlockingAndCoalesced(t *testing.T) {
	hub := NewHub(NewStore())
	for index := 0; index < 10_000; index++ {
		hub.Notify()
	}
	if len(hub.dirty) != 1 {
		t.Fatalf("dirty queue length = %d", len(hub.dirty))
	}

	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() {
		hub.Run(ctx)
		close(done)
	}()
	cancel()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("hub did not stop")
	}
}
