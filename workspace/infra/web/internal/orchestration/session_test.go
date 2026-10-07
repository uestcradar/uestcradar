package orchestration

import (
	"testing"
	"time"
)

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
