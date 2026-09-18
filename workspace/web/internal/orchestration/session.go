package orchestration

import (
	"crypto/rand"
	"encoding/hex"
	"sync"
	"time"
)

const sessionTTL = 30 * time.Minute

type Credentials struct {
	Username   string
	Password   []byte
	PrivateKey []byte
	Passphrase []byte
}

type Session struct {
	ID          string
	CSRF        string
	Credentials Credentials
	ExpiresAt   time.Time
	TrustedKeys map[string]string
	CustomIPs   []string
	Nodes       map[string]NodeInspection
	Plans       map[string]DeploymentPlan
	Tasks       map[string]*Task
	timer       *time.Timer
	mu          sync.Mutex
}

type SessionStore struct {
	mu       sync.Mutex
	sessions map[string]*Session
	now      func() time.Time
}

func NewSessionStore() *SessionStore {
	return &SessionStore{sessions: map[string]*Session{}, now: time.Now}
}

func (s *SessionStore) Create(credentials Credentials) (*Session, error) {
	id, err := randomToken()
	if err != nil {
		return nil, err
	}
	csrf, err := randomToken()
	if err != nil {
		return nil, err
	}
	session := &Session{
		ID: id, CSRF: csrf, Credentials: credentials,
		ExpiresAt: s.now().Add(sessionTTL), TrustedKeys: map[string]string{},
		Nodes: map[string]NodeInspection{}, Plans: map[string]DeploymentPlan{},
		Tasks: map[string]*Task{},
	}
	s.mu.Lock()
	s.sessions[id] = session
	session.timer = time.AfterFunc(sessionTTL, func() { s.expire(id) })
	s.mu.Unlock()
	return session, nil
}

func (s *SessionStore) Get(id string) (*Session, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	session, ok := s.sessions[id]
	if !ok {
		return nil, false
	}
	if !s.now().Before(session.ExpiresAt) {
		delete(s.sessions, id)
		if session.timer != nil {
			session.timer.Stop()
		}
		session.mu.Lock()
		zeroCredentials(&session.Credentials)
		session.mu.Unlock()
		return nil, false
	}
	session.ExpiresAt = s.now().Add(sessionTTL)
	if session.timer != nil {
		session.timer.Reset(sessionTTL)
	}
	return session, true
}

func (s *SessionStore) Delete(id string) {
	s.mu.Lock()
	session := s.sessions[id]
	delete(s.sessions, id)
	if session != nil && session.timer != nil {
		session.timer.Stop()
	}
	s.mu.Unlock()
	if session != nil {
		session.mu.Lock()
		zeroCredentials(&session.Credentials)
		session.mu.Unlock()
	}
}

func (s *SessionStore) expire(id string) {
	s.mu.Lock()
	session, ok := s.sessions[id]
	if !ok {
		s.mu.Unlock()
		return
	}
	remaining := session.ExpiresAt.Sub(s.now())
	if remaining > 0 {
		session.timer.Reset(remaining)
		s.mu.Unlock()
		return
	}
	delete(s.sessions, id)
	s.mu.Unlock()
	session.mu.Lock()
	zeroCredentials(&session.Credentials)
	session.mu.Unlock()
}

func randomToken() (string, error) {
	value := make([]byte, 32)
	if _, err := rand.Read(value); err != nil {
		return "", err
	}
	return hex.EncodeToString(value), nil
}

func zeroCredentials(credentials *Credentials) {
	for _, value := range [][]byte{credentials.Password, credentials.PrivateKey, credentials.Passphrase} {
		for index := range value {
			value[index] = 0
		}
	}
}
