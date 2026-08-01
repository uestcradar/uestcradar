package orchestration

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"regexp"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/pkg/sftp"
	"golang.org/x/crypto/ssh"
)

const (
	workerRepository = "registry.chengyistudio.com/cxx/worker:"
	sidecarReference = "registry.chengyistudio.com/cxx/sidecar:latest"
	remoteDirectory  = "/root/workspace/docker"
	composeFilename  = "compose.cascade.distributed.yaml"
	envFilename      = "node.env"
)

var safeImageReference = regexp.MustCompile(`^registry\.chengyistudio\.com/cxx/worker:[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$`)

type HostKeyError struct {
	IP          string
	Fingerprint string
}

func (e *HostKeyError) Error() string { return "SSH host key confirmation required" }

type CommandOutput func(stream, text string)

type RemoteBackend interface {
	Inspect(*Session, string, CommandOutput) (NodeInspection, error)
	PullWorker(*Session, string, string, CommandOutput) error
	PullSidecar(*Session, string, CommandOutput) error
	UploadAndValidate(*Session, PlannedNode, CommandOutput) (bool, error)
	Start(*Session, PlannedNode, CommandOutput) error
	Down(*Session, string, CommandOutput) error
}

type SSHBackend struct{}

func NewSSHBackend() *SSHBackend { return &SSHBackend{} }

func (b *SSHBackend) client(session *Session, ip string) (*ssh.Client, error) {
	session.mu.Lock()
	credentials := Credentials{
		Username:   session.Credentials.Username,
		Password:   append([]byte(nil), session.Credentials.Password...),
		PrivateKey: append([]byte(nil), session.Credentials.PrivateKey...),
		Passphrase: append([]byte(nil), session.Credentials.Passphrase...),
	}
	session.mu.Unlock()
	defer zeroCredentials(&credentials)
	auth := make([]ssh.AuthMethod, 0, 2)
	if len(credentials.Password) > 0 {
		auth = append(auth, ssh.Password(string(credentials.Password)))
	}
	if len(credentials.PrivateKey) > 0 {
		var signer ssh.Signer
		var err error
		if len(credentials.Passphrase) > 0 {
			signer, err = ssh.ParsePrivateKeyWithPassphrase(credentials.PrivateKey, credentials.Passphrase)
		} else {
			signer, err = ssh.ParsePrivateKey(credentials.PrivateKey)
		}
		if err != nil {
			return nil, fmt.Errorf("parse private key: %w", err)
		}
		auth = append(auth, ssh.PublicKeys(signer))
	}
	if len(auth) == 0 {
		return nil, fmt.Errorf("no SSH authentication method")
	}

	var observed string
	config := &ssh.ClientConfig{
		User: credentials.Username, Auth: auth, Timeout: 8 * time.Second,
		HostKeyCallback: func(_ string, _ net.Addr, key ssh.PublicKey) error {
			observed = ssh.FingerprintSHA256(key)
			session.mu.Lock()
			trusted := session.TrustedKeys[ip]
			session.mu.Unlock()
			if trusted == "" || trusted != observed {
				return errors.New("untrusted host key")
			}
			return nil
		},
	}
	client, err := ssh.Dial("tcp", net.JoinHostPort(ip, "22"), config)
	if err != nil {
		if observed != "" {
			return nil, &HostKeyError{IP: ip, Fingerprint: observed}
		}
		return nil, err
	}
	return client, nil
}

func runSSH(client *ssh.Client, command string) (string, error) {
	return runSSHOutput(client, command, nil)
}

func runSSHOutput(client *ssh.Client, command string, output CommandOutput) (string, error) {
	remoteSession, err := client.NewSession()
	if err != nil {
		return "", err
	}
	defer remoteSession.Close()
	if output == nil {
		combined, runErr := remoteSession.CombinedOutput(command)
		if runErr != nil {
			return "", fmt.Errorf("remote command failed: %s", strings.TrimSpace(string(combined)))
		}
		return strings.TrimSpace(string(combined)), nil
	}
	output("system", "$ "+command+"\n")
	stdoutPipe, err := remoteSession.StdoutPipe()
	if err != nil {
		return "", err
	}
	stderrPipe, err := remoteSession.StderrPipe()
	if err != nil {
		return "", err
	}
	if err := remoteSession.Start(command); err != nil {
		return "", err
	}
	var stdoutBuffer, stderrBuffer bytes.Buffer
	var group sync.WaitGroup
	group.Add(2)
	go copyCommandOutput(&group, stdoutPipe, &stdoutBuffer, "stdout", output)
	go copyCommandOutput(&group, stderrPipe, &stderrBuffer, "stderr", output)
	runErr := remoteSession.Wait()
	group.Wait()
	if runErr != nil {
		message := strings.TrimSpace(stderrBuffer.String())
		if message == "" {
			message = strings.TrimSpace(stdoutBuffer.String())
		}
		return "", fmt.Errorf("remote command failed: %s", message)
	}
	return strings.TrimSpace(stdoutBuffer.String()), nil
}

func copyCommandOutput(group *sync.WaitGroup, source io.Reader, destination *bytes.Buffer, stream string, output CommandOutput) {
	defer group.Done()
	buffer := make([]byte, 4096)
	for {
		size, err := source.Read(buffer)
		if size > 0 {
			chunk := append([]byte(nil), buffer[:size]...)
			_, _ = destination.Write(chunk)
			output(stream, string(chunk))
		}
		if err != nil {
			return
		}
	}
}

type dockerInspect struct {
	ID           string `json:"Id"`
	Architecture string `json:"Architecture"`
	Config       struct {
		Labels     map[string]string `json:"Labels"`
		Entrypoint []string          `json:"Entrypoint"`
		Cmd        []string          `json:"Cmd"`
	} `json:"Config"`
}

func inspectImage(client *ssh.Client, reference string, sink CommandOutput) (dockerInspect, error) {
	output, err := runSSHOutput(client, "docker image inspect "+shellQuote(reference), sink)
	if err != nil {
		return dockerInspect{}, err
	}
	var values []dockerInspect
	if err := json.Unmarshal([]byte(output), &values); err != nil || len(values) != 1 {
		return dockerInspect{}, fmt.Errorf("parse docker image inspect")
	}
	return values[0], nil
}

func (b *SSHBackend) Inspect(session *Session, ip string, output CommandOutput) (NodeInspection, error) {
	result := NodeInspection{IP: ip, Reachable: true, InspectedAt: time.Now()}
	client, err := b.client(session, ip)
	if err != nil {
		return result, err
	}
	defer client.Close()

	if result.Hostname, err = runSSHOutput(client, "hostname", output); err != nil {
		return result, err
	}
	if result.Architecture, err = runSSHOutput(client, "uname -m", output); err != nil {
		return result, err
	}
	result.DockerVersion, err = runSSHOutput(client, "docker version --format '{{.Server.Version}}'", output)
	if err != nil {
		return result, fmt.Errorf("Docker unavailable: %w", err)
	}
	result.ComposeCLI = detectCompose(client, output)
	result.CPUs, _ = runSSHOutput(client, "docker info --format '{{.NCPU}}'", output)
	result.MemoryBytes, _ = runSSHOutput(client, "docker info --format '{{.MemTotal}}'", output)
	result.DockerDisk, _ = runSSHOutput(client, "df -B1 --output=avail /var/lib/docker 2>/dev/null | tail -n 1 | tr -d ' '", output)
	rdmaText, _ := runSSHOutput(client, "rdma link show 2>/dev/null || true", output)
	ipText, _ := runSSHOutput(client, "ip -o -4 addr show 2>/dev/null || true", output)
	result.RDMA = parseRDMA(rdmaText, ipText)

	sidecar, err := inspectImage(client, sidecarReference, output)
	if err == nil {
		result.SidecarImageID = sidecar.ID
		result.SidecarContract = sidecar.Config.Labels["io.uestcradar.contract"]
	}
	refs, _ := runSSHOutput(client, "docker image ls --filter label=io.uestcradar.contract=worker/v1 --format '{{.Repository}}:{{.Tag}}'", output)
	seen := map[string]bool{}
	for _, reference := range strings.Fields(refs) {
		if !strings.HasPrefix(reference, workerRepository) || strings.HasSuffix(reference, ":<none>") || seen[reference] {
			continue
		}
		seen[reference] = true
		image, inspectErr := inspectImage(client, reference, output)
		if inspectErr != nil {
			continue
		}
		contract, contractErr := ParseWorkerContract(image.Config.Labels)
		if contractErr != nil || (len(image.Config.Entrypoint) == 0 && len(image.Config.Cmd) == 0) {
			continue
		}
		result.Workers = append(result.Workers, ImageInfo{Reference: reference, ID: image.ID, Architecture: image.Architecture, Entrypoint: image.Config.Entrypoint, Command: image.Config.Cmd, Contract: contract})
	}
	sort.Slice(result.Workers, func(i, j int) bool { return result.Workers[i].Reference < result.Workers[j].Reference })
	deployment, _ := runSSHOutput(client, "docker ps -a --filter label=com.docker.compose.project=uestcradar-cascade --format '{{.Label \"com.docker.compose.service\"}}|{{.State}}'", output)
	result.ExistingDeployment, result.DeploymentState = deploymentStatus(deployment)
	return result, nil
}

func detectCompose(client *ssh.Client, output CommandOutput) string {
	if _, err := runSSHOutput(client, "docker-compose version --short", output); err == nil {
		return "v1"
	}
	if _, err := runSSHOutput(client, "docker compose version --short", output); err == nil {
		return "v2"
	}
	return ""
}

func deploymentStatus(text string) (bool, string) {
	services := map[string]string{}
	for _, line := range strings.Split(text, "\n") {
		parts := strings.SplitN(strings.TrimSpace(line), "|", 2)
		if len(parts) == 2 && parts[0] != "" {
			services[parts[0]] = parts[1]
		}
	}
	if len(services) == 0 {
		return false, "absent"
	}
	running := 0
	for _, service := range []string{"sidecar-node", "worker-node"} {
		if services[service] == "running" {
			running++
		}
	}
	if running == 2 {
		return true, "running"
	}
	if running == 0 {
		return true, "stopped"
	}
	return true, "partial"
}

func parseRDMA(rdmaText, ipText string) []RDMAInterface {
	ips := map[string]string{}
	for _, line := range strings.Split(ipText, "\n") {
		fields := strings.Fields(line)
		if len(fields) >= 4 && fields[2] == "inet" {
			ips[strings.TrimSuffix(fields[1], ":")] = strings.SplitN(fields[3], "/", 2)[0]
		}
	}
	var result []RDMAInterface
	for _, line := range strings.Split(rdmaText, "\n") {
		fields := strings.Fields(line)
		if len(fields) < 2 || fields[0] != "link" {
			continue
		}
		devicePort := strings.SplitN(fields[1], "/", 2)
		item := RDMAInterface{Device: devicePort[0]}
		if len(devicePort) == 2 {
			item.Port = devicePort[1]
		}
		for index := 2; index+1 < len(fields); index++ {
			switch fields[index] {
			case "state":
				item.State = fields[index+1]
			case "physical_state":
				item.PhysicalState = fields[index+1]
			case "netdev":
				item.NetDev = fields[index+1]
			}
		}
		item.IPv4 = ips[item.NetDev]
		result = append(result, item)
	}
	return result
}

func (b *SSHBackend) PullWorker(session *Session, ip, reference string, output CommandOutput) error {
	if !safeImageReference.MatchString(reference) {
		return fmt.Errorf("image reference is not allowed")
	}
	client, err := b.client(session, ip)
	if err != nil {
		return err
	}
	defer client.Close()
	_, err = runSSHOutput(client, "docker pull "+shellQuote(reference), output)
	return err
}

func (b *SSHBackend) PullSidecar(session *Session, ip string, output CommandOutput) error {
	client, err := b.client(session, ip)
	if err != nil {
		return err
	}
	defer client.Close()
	_, err = runSSHOutput(client, "docker pull "+shellQuote(sidecarReference), output)
	return err
}

func (b *SSHBackend) UploadAndValidate(session *Session, node PlannedNode, output CommandOutput) (bool, error) {
	client, err := b.client(session, node.IP)
	if err != nil {
		return false, err
	}
	defer client.Close()
	existing, _ := runSSHOutput(client, "docker ps -a --filter label=com.docker.compose.project=uestcradar-cascade --format '{{.ID}}' | head -n 1", output)
	if _, err := runSSHOutput(client, "mkdir -p "+shellQuote(remoteDirectory), output); err != nil {
		return existing != "", err
	}
	sftpClient, err := sftp.NewClient(client)
	if err != nil {
		return existing != "", err
	}
	defer sftpClient.Close()
	composeTemp := remoteDirectory + "/." + composeFilename + ".tmp"
	envTemp := remoteDirectory + "/." + envFilename + ".tmp"
	if err := writeRemoteFile(sftpClient, composeTemp, []byte(node.compose), 0644); err != nil {
		return existing != "", err
	}
	if err := writeRemoteFile(sftpClient, envTemp, []byte(node.env), 0600); err != nil {
		return existing != "", err
	}
	compose := composeCommandFor(node, composeTemp, envTemp, "config")
	if _, err := runSSHOutput(client, compose, output); err != nil {
		return existing != "", err
	}
	if err := sftpClient.PosixRename(composeTemp, remoteDirectory+"/"+composeFilename); err != nil {
		return existing != "", err
	}
	if err := sftpClient.PosixRename(envTemp, remoteDirectory+"/"+envFilename); err != nil {
		return existing != "", err
	}
	return existing != "", nil
}

func writeRemoteFile(client *sftp.Client, path string, content []byte, mode uint32) error {
	file, err := client.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_TRUNC)
	if err != nil {
		return err
	}
	if err = client.Chmod(path, os.FileMode(mode)); err != nil {
		_ = file.Close()
		return err
	}
	if _, err = bytes.NewReader(content).WriteTo(file); err != nil {
		_ = file.Close()
		return err
	}
	return file.Close()
}

func (b *SSHBackend) Start(session *Session, node PlannedNode, output CommandOutput) error {
	client, err := b.client(session, node.IP)
	if err != nil {
		return err
	}
	defer client.Close()
	command := composeCommandFor(node, remoteDirectory+"/"+composeFilename, remoteDirectory+"/"+envFilename, "up -d --no-build")
	_, err = runSSHOutput(client, command, output)
	return err
}

func (b *SSHBackend) Down(session *Session, ip string, output CommandOutput) error {
	client, err := b.client(session, ip)
	if err != nil {
		return err
	}
	defer client.Close()
	command := composeCommandFor(PlannedNode{}, remoteDirectory+"/"+composeFilename, remoteDirectory+"/"+envFilename, "down --remove-orphans")
	_, err = runSSHOutput(client, command, output)
	return err
}

func composeCommandFor(_ PlannedNode, compose, env, action string) string {
	// Compose capability was validated during inspection. Prefer v1 at execution time.
	arguments := " --env-file " + shellQuote(env) + " -p uestcradar-cascade -f " + shellQuote(compose) + " " + action
	return "cd " + shellQuote(remoteDirectory) + " && if docker-compose version --short >/dev/null 2>&1; then docker-compose" + arguments + "; elif docker compose version --short >/dev/null 2>&1; then docker compose" + arguments + "; else echo 'Docker Compose is unavailable' >&2; exit 127; fi"
}

func shellQuote(value string) string { return "'" + strings.ReplaceAll(value, "'", "'\\''") + "'" }
