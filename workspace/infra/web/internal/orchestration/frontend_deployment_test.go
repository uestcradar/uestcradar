package orchestration

import (
	"crypto/tls"
	"crypto/x509"
	"encoding/pem"
	"github.com/pkg/sftp"
	"net"
	"os"
	"syscall"
	"testing"
	"time"
)

func TestAutomaticFrontendTLSInstall(t *testing.T) {
	left, right := net.Pipe()
	defer left.Close()
	defer right.Close()
	server, err := sftp.NewServer(left)
	if err != nil {
		t.Fatal(err)
	}
	defer server.Close()
	go server.Serve()
	client, err := sftp.NewClientPipe(right, right)
	if err != nil {
		t.Fatal(err)
	}
	defer client.Close()
	revision, err := installFrontendTLS(client, "127.0.0.1")
	if err != nil {
		t.Fatal(err)
	}
	dir := remoteDirectory + "/frontend-tls/" + revision
	defer os.RemoveAll(dir) // This test runs only inside the isolated ARM test container.
	certificate, err := os.ReadFile(dir + "/server.crt")
	if err != nil {
		t.Fatal(err)
	}
	key, err := os.ReadFile(dir + "/server.key")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := tls.X509KeyPair(certificate, key); err != nil {
		t.Fatal(err)
	}
	info, err := os.Stat(dir + "/server.key")
	if err != nil {
		t.Fatal(err)
	}
	owner := info.Sys().(*syscall.Stat_t)
	if info.Mode().Perm() != 0400 || owner.Uid != 65532 || owner.Gid != 65532 {
		t.Fatal("wrong runtime key ownership/permissions")
	}
	if len(revision) != 64 {
		t.Fatal("invalid certificate revision")
	}
}

func TestAutomaticFrontendCertificate(t *testing.T) {
	now := time.Now()
	cert, key, err := frontendCertificate("192.162.2.64", now)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := tls.X509KeyPair(cert, key); err != nil {
		t.Fatal(err)
	}
	block, _ := pem.Decode(cert)
	leaf, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		t.Fatal(err)
	}
	roots := x509.NewCertPool()
	roots.AppendCertsFromPEM(cert)
	if _, err := leaf.Verify(x509.VerifyOptions{DNSName: "192.162.2.64", Roots: roots, CurrentTime: now}); err != nil {
		t.Fatalf("self-healthcheck trust: %v", err)
	}
	if leaf.IsCA {
		t.Fatal("node must not receive a CA signing key")
	}
	next, nextKey, err := frontendCertificate("192.162.2.64", now)
	if err != nil || string(next) == string(cert) || string(nextKey) == string(key) {
		t.Fatal("deployment must get an independent key/revision")
	}
	if _, _, err := frontendCertificate("not-an-ip", now); err == nil {
		t.Fatal("invalid endpoint accepted")
	}
	if frontendHTTPTransport.TLSClientConfig == nil || !frontendHTTPTransport.TLSClientConfig.InsecureSkipVerify {
		t.Fatal("operator-selected LAN policy not enabled")
	}
}
