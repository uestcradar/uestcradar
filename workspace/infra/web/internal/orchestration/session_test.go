package orchestration

import (
	"testing"
	"time"
)

func TestSessionCancellationAndRenewal(t *testing.T) {
	for _, mode := range []string{"get-expiry", "timer-expiry", "delete", "renew"} {
		t.Run(mode, func(t *testing.T) {
			store := NewSessionStore()
			now := time.Unix(100, 0)
			store.now = func() time.Time { return now }
			secret := []byte("test-only")
			session, err := store.Create(Credentials{Password: secret})
			if err != nil {
				t.Fatal(err)
			}
			defer store.Delete(session.ID)
			switch mode {
			case "get-expiry":
				now = now.Add(sessionTTL + time.Second)
				if _, ok := store.Get(session.ID); ok {
					t.Fatal("expired session retained")
				}
			case "timer-expiry":
				now = now.Add(sessionTTL + time.Second)
				store.expire(session.ID)
			case "renew":
				now = now.Add(sessionTTL / 2)
				if _, ok := store.Get(session.ID); !ok {
					t.Fatal("session not renewed")
				}
				now = now.Add(sessionTTL/2 + time.Second)
				store.expire(session.ID)
				if session.ctx.Err() != nil {
					t.Fatal("renewed session canceled")
				}
				store.Delete(session.ID)
			default:
				store.Delete(session.ID)
			}
			select {
			case <-session.ctx.Done():
			default:
				t.Fatal("session did not cancel subscribers")
			}
			for _, b := range secret {
				if b != 0 {
					t.Fatal("secret not zeroed")
				}
			}
			store.Delete(session.ID)
		})
	}
}

func TestSessionExpiresAndZerosCredentials(t *testing.T) {
	store := NewSessionStore()
	now := time.Unix(100, 0)
	store.now = func() time.Time { return now }
	password := []byte("secret")
	session, err := store.Create(Credentials{Username: "root", Password: password})
	if err != nil {
		t.Fatal(err)
	}
	now = now.Add(sessionTTL + time.Second)
	if _, ok := store.Get(session.ID); ok {
		t.Fatal("expired session returned")
	}
	for _, value := range password {
		if value != 0 {
			t.Fatal("credential bytes were not cleared")
		}
	}
}

func TestSessionTimerPathZerosCredentials(t *testing.T) {
	store := NewSessionStore()
	now := time.Unix(100, 0)
	store.now = func() time.Time { return now }
	password := []byte("secret")
	session, err := store.Create(Credentials{Username: "root", Password: password})
	if err != nil {
		t.Fatal(err)
	}
	now = now.Add(sessionTTL + time.Second)
	store.expire(session.ID)
	if _, ok := store.Get(session.ID); ok {
		t.Fatal("timer-expired session returned")
	}
	for _, value := range password {
		if value != 0 {
			t.Fatal("timer path did not clear credential bytes")
		}
	}
}
