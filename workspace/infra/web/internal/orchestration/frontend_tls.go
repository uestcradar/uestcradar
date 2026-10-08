package orchestration

import (
	"crypto/ed25519"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"fmt"
	"math/big"
	"net"
	"time"

	"github.com/pkg/sftp"
)

func frontendCertificate(ip string, now time.Time) ([]byte, []byte, error) {
	address := net.ParseIP(ip)
	if address == nil {
		return nil, nil, fmt.Errorf("invalid Frontend IP")
	}
	public, private, err := ed25519.GenerateKey(rand.Reader)
	if err != nil {
		return nil, nil, err
	}
	serial, err := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if err != nil {
		return nil, nil, err
	}
	serial.SetBit(serial, 0, 1)
	template := &x509.Certificate{SerialNumber: serial, Subject: pkix.Name{CommonName: "uestcradar-node"}, IPAddresses: []net.IP{address}, NotBefore: now.Add(-5 * time.Minute), NotAfter: now.AddDate(10, 0, 0), KeyUsage: x509.KeyUsageDigitalSignature, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}, BasicConstraintsValid: true}
	der, err := x509.CreateCertificate(rand.Reader, template, template, public, private)
	if err != nil {
		return nil, nil, err
	}
	key, err := x509.MarshalPKCS8PrivateKey(private)
	if err != nil {
		return nil, nil, err
	}
	return pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der}), pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: key}), nil
}

func installFrontendTLS(client *sftp.Client, ip string) (string, error) {
	certificate, key, err := frontendCertificate(ip, time.Now())
	if err != nil {
		return "", err
	}
	defer func() {
		for i := range key {
			key[i] = 0
		}
	}()
	revision := fmt.Sprintf("%x", sha256.Sum256(certificate))
	// ponytail: retain revisions while old containers may reference them; prune with deployment retention if needed.
	dir := remoteDirectory + "/frontend-tls/" + revision
	if err := client.MkdirAll(dir); err != nil {
		return "", err
	}
	if err := client.Chmod(dir, 0700); err != nil {
		return "", err
	}
	if err := writeRemoteFile(client, dir+"/server.crt", certificate, 0644); err != nil {
		return "", err
	}
	if err := writeRemoteFile(client, dir+"/server.key", key, 0600); err != nil {
		return "", err
	}
	if err := client.Chown(dir+"/server.key", 65532, 65532); err != nil {
		return "", err
	}
	if err := client.Chmod(dir+"/server.key", 0400); err != nil {
		return "", err
	}
	return revision, nil
}
