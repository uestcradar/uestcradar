package orchestration

import (
	"crypto/ed25519"
	"crypto/rand"
	"errors"
	"golang.org/x/crypto/ssh"
	"net"
	"testing"
)

func TestSSHAuthenticationClassification(t *testing.T) {
	_, key, _ := ed25519.GenerateKey(rand.Reader)
	signer, _ := ssh.NewSignerFromKey(key)
	config := &ssh.ServerConfig{PasswordCallback: func(c ssh.ConnMetadata, password []byte) (*ssh.Permissions, error) {
		if c.User() == "root" && string(password) == "correct" {
			return nil, nil
		}
		return nil, errors.New("bad credentials")
	}}
	config.AddHostKey(signer)
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	go func() {
		for {
			conn, err := listener.Accept()
			if err != nil {
				return
			}
			go func() {
				defer conn.Close()
				server, channels, requests, err := ssh.NewServerConn(conn, config)
				if err != nil {
					return
				}
				defer server.Close()
				go ssh.DiscardRequests(requests)
				for channel := range channels {
					channel.Reject(ssh.Prohibited, "test server")
				}
			}()
		}
	}()
	fingerprint := ssh.FingerprintSHA256(signer.PublicKey())
	for _, tc := range []struct{ name, trusted, password, kind string }{
		{"unknown key", "", "correct", "host"},
		{"changed key", "SHA256:other", "correct", "host"},
		{"wrong password", fingerprint, "wrong", "auth"},
		{"correct password", fingerprint, "correct", "ok"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			session := &Session{Credentials: Credentials{Username: "root", Password: []byte(tc.password)}, TrustedKeys: map[string]string{"127.0.0.1": tc.trusted}}
			client, err := NewSSHBackend().clientAt(session, "127.0.0.1", listener.Addr().String())
			if client != nil {
				client.Close()
			}
			var host *HostKeyError
			switch tc.kind {
			case "host":
				if !errors.As(err, &host) || host.Fingerprint != fingerprint {
					t.Fatalf("expected host key error, got %v", err)
				}
			case "auth":
				if errors.As(err, &host) || sshErrorCode(err) != "ssh_auth_failed" {
					t.Fatalf("expected auth error, got %v", err)
				}
			case "ok":
				if err != nil {
					t.Fatal(err)
				}
			}
		})
	}
	listener.Close()
	session := &Session{Credentials: Credentials{Username: "root", Password: []byte("correct")}, TrustedKeys: map[string]string{"127.0.0.1": fingerprint}}
	_, err = NewSSHBackend().clientAt(session, "127.0.0.1", listener.Addr().String())
	var host *HostKeyError
	if err == nil || errors.As(err, &host) || sshErrorCode(err) != "" {
		t.Fatalf("connection error misclassified: %v", err)
	}
}

type authFailureRemote struct{ fakeRemote }

func (*authFailureRemote) Inspect(_ *Session, ip string, _ CommandOutput) (NodeInspection, error) {
	return NodeInspection{IP: ip}, &SSHAuthError{Cause: errors.New("test authentication failure")}
}
func (*authFailureRemote) Down(_ *Session, _ string, _ CommandOutput) error {
	return &SSHAuthError{Cause: errors.New("test authentication failure")}
}
func TestSSHAuthErrorsInInspectionAndStopTask(t *testing.T) {
	service := testService()
	service.remote = &authFailureRemote{}
	session, _ := service.sessions.Create(Credentials{Username: "root", Password: []byte("test")})
	defer service.sessions.Delete(session.ID)
	results := service.inspectNodes(session, []string{"10.0.0.1"}, "")
	if results[0].ErrorCode != "ssh_auth_failed" || results[0].HostKeyRequired {
		t.Fatalf("wrong result: %#v", results[0])
	}
	task := service.newTask(session, "deployment-stop")
	service.stopDeployment(session, []string{"10.0.0.1"}, task.ID)
	result := session.Tasks[task.ID]
	if result.Status != "failed" || result.ErrorCode != "ssh_auth_failed" || result.CurrentIP != "10.0.0.1" {
		t.Fatalf("wrong task: %#v", result)
	}
}
