package orchestration

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"

	"golang.org/x/crypto/ssh"
)

type SignalSinkStatus struct {
	OK                 bool    `json:"ok"`
	State              string  `json:"state"`
	RecordingID        string  `json:"recording_id"`
	Directory          string  `json:"directory"`
	Error              string  `json:"error"`
	AcceptedFrames     string  `json:"accepted_frames"`
	WrittenFrames      string  `json:"written_frames"`
	WrittenBytes       string  `json:"written_bytes"`
	QueueUsedBytes     string  `json:"queue_used_bytes"`
	QueueCapacityBytes string  `json:"queue_capacity_bytes"`
	ElapsedMS          string  `json:"elapsed_ms"`
	SampleContinuity   string  `json:"sample_continuity"`
	DiskTotalBytes     *string `json:"disk_total_bytes"`
	DiskUsedBytes      *string `json:"disk_used_bytes"`
	DiskAvailableBytes *string `json:"disk_available_bytes"`
}

var containerIDPattern = regexp.MustCompile(`^[0-9a-f]{64}$`)
var recordingIDPattern = regexp.MustCompile(`^[0-9a-f]{32}$`)
var captureDirectoryPattern = regexp.MustCompile(`^[A-Za-z0-9_-][A-Za-z0-9_.-]*(/[A-Za-z0-9_-][A-Za-z0-9_.-]*)*$`)
var decimalCounterPattern = regexp.MustCompile(`^(0|[1-9][0-9]{0,19})$`)

// stdout and stderr are copied concurrently by x/crypto/ssh.
type recordingOutput struct {
	mu       sync.Mutex
	data     bytes.Buffer
	overflow bool
}

func (b *recordingOutput) Write(p []byte) (int, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	count := len(p)
	available := 65536 - b.data.Len()
	if len(p) > available {
		b.overflow = true
		p = p[:available]
	}
	_, _ = b.data.Write(p)
	return count, nil
}
func boundedRecordingCommand(client *ssh.Client, command string) (string, error) {
	timer := time.AfterFunc(5*time.Second, func() { _ = client.Close() })
	defer timer.Stop()
	remote, err := client.NewSession()
	if err != nil {
		return "", err
	}
	defer remote.Close()
	var output recordingOutput
	remote.Stdout = &output
	remote.Stderr = &output
	err = remote.Run(command)
	if output.overflow {
		return "", fmt.Errorf("SignalSink response exceeds 64 KiB")
	}
	if err != nil {
		return "", fmt.Errorf("SignalSink remote command failed")
	}
	return cleanSSHCommandOutput(output.data.String()), nil
}

func runningSignalSink(client *ssh.Client) (string, string, error) {
	ids, err := boundedRecordingCommand(client, "docker ps -q --no-trunc --filter label=com.docker.compose.project=uestcradar-cascade --filter label=com.docker.compose.service=worker-node")
	if err != nil || !containerIDPattern.MatchString(ids) {
		return "", "", fmt.Errorf("unique running Worker unavailable")
	}
	text, err := boundedRecordingCommand(client, "docker inspect "+shellQuote(ids))
	if err != nil {
		return "", "", err
	}
	var containers []struct {
		ID    string `json:"Id"`
		Image string `json:"Image"`
		State struct {
			Running bool `json:"Running"`
		} `json:"State"`
		Config struct {
			Labels     map[string]string `json:"Labels"`
			Entrypoint []string          `json:"Entrypoint"`
		} `json:"Config"`
	}
	if err := json.Unmarshal([]byte(text), &containers); err != nil || len(containers) != 1 {
		return "", "", fmt.Errorf("invalid running Worker inspection")
	}
	node := containers[0]
	contract, err := ParseWorkerContract(node.Config.Labels)
	if err != nil || contract.Component != "signalsink" || !node.State.Running || node.ID != ids ||
		len(node.Config.Entrypoint) != 1 || node.Config.Entrypoint[0] != "/app/signalsink" {
		return "", "", fmt.Errorf("running Worker is not SignalSink")
	}
	return ids, node.Image, nil
}

func knownSignalSinkImage(node NodeInspection, image string) bool {
	for _, worker := range node.Workers {
		if worker.ID == image && worker.Contract.Component == "signalsink" &&
			worker.Contract.Input == "any" && worker.Contract.Output == "none" &&
			strings.HasPrefix(worker.DigestReference, "registry.chengyistudio.com/cxx/worker@") && harborDigestPattern.MatchString(worker.DigestReference) {
			return true
		}
	}
	return false
}

func (b *SSHBackend) SignalSink(ctx context.Context, session *Session, ip, operation, argument string) (SignalSinkStatus, error) {
	return b.signalSinkAt(ctx, session, ip, net.JoinHostPort(ip, "22"), operation, argument)
}

func (b *SSHBackend) signalSinkAt(ctx context.Context, session *Session, ip, address, operation, argument string) (SignalSinkStatus, error) {
	var result SignalSinkStatus
	client, err := b.clientContext(ctx, session, ip, address)
	if err != nil {
		return result, err
	}
	defer client.Close()
	cancel := context.AfterFunc(ctx, func() { _ = client.Close() })
	defer cancel()
	if err := ctx.Err(); err != nil {
		return result, err
	}
	id, image, err := runningSignalSink(client)
	if err != nil {
		return result, err
	}
	session.mu.Lock()
	node := session.Nodes[ip]
	session.mu.Unlock()
	if !knownSignalSinkImage(node, image) {
		return result, fmt.Errorf("running SignalSink image was not inspected; inspect node again")
	}
	command := "docker exec " + shellQuote(id) + " /app/signalsink control "
	switch operation {
	case "status":
		command += "status"
	case "start":
		if len(argument) > 240 || !captureDirectoryPattern.MatchString(argument) {
			return result, fmt.Errorf("invalid relative capture directory")
		}
		command += "start --directory " + shellQuote(argument)
	case "stop":
		if !recordingIDPattern.MatchString(argument) {
			return result, fmt.Errorf("invalid recording ID")
		}
		command += "stop --recording-id " + shellQuote(argument)
	default:
		return result, fmt.Errorf("invalid SignalSink operation")
	}
	if err := ctx.Err(); err != nil {
		return result, err
	}
	text, err := boundedRecordingCommand(client, command)
	if err != nil {
		return result, err
	}
	if err = json.Unmarshal([]byte(text), &result); err != nil {
		return result, fmt.Errorf("invalid SignalSink status")
	}
	if !result.OK {
		return result, fmt.Errorf("SignalSink: %s", result.Error)
	}
	switch result.State {
	case "idle", "starting", "recording", "stopping", "failed":
	default:
		return result, fmt.Errorf("invalid recording state")
	}
	counters := []string{result.AcceptedFrames, result.WrittenFrames, result.WrittenBytes, result.QueueUsedBytes, result.QueueCapacityBytes, result.ElapsedMS}
	for _, value := range []*string{result.DiskTotalBytes, result.DiskUsedBytes, result.DiskAvailableBytes} {
		if value != nil {
			counters = append(counters, *value)
		}
	}
	for _, value := range counters {
		_, parseErr := strconv.ParseUint(value, 10, 64)
		if !decimalCounterPattern.MatchString(value) || parseErr != nil {
			return result, fmt.Errorf("invalid recording counter")
		}
	}
	if len(result.Directory) > 240 || !captureDirectoryPattern.MatchString(result.Directory) ||
		(result.RecordingID != "" && !recordingIDPattern.MatchString(result.RecordingID)) ||
		(result.State == "recording" && result.RecordingID == "") {
		return result, fmt.Errorf("invalid recording identity")
	}
	if result.SampleContinuity != "unverified" {
		return result, fmt.Errorf("unsupported continuity claim")
	}
	return result, nil
}

func (s *Service) handleSignalSink(w http.ResponseWriter, r *http.Request, session *Session, operation string) {
	var body struct {
		IP          string `json:"ip"`
		Directory   string `json:"directory"`
		RecordingID string `json:"recording_id"`
	}
	switch operation {
	case "status":
		if r.Method != http.MethodGet {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		body.IP = r.URL.Query().Get("ip")
	case "start", "stop":
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		decoder := json.NewDecoder(http.MaxBytesReader(w, r.Body, 2048))
		decoder.DisallowUnknownFields()
		if err := decoder.Decode(&body); err != nil {
			http.Error(w, "invalid request", http.StatusBadRequest)
			return
		}
		var extra any
		if decoder.Decode(&extra) != io.EOF {
			http.Error(w, "invalid request", http.StatusBadRequest)
			return
		}
		if operation == "start" && (len(body.Directory) > 240 || !captureDirectoryPattern.MatchString(body.Directory)) ||
			operation == "stop" && !recordingIDPattern.MatchString(body.RecordingID) {
			http.Error(w, "invalid recording argument", http.StatusBadRequest)
			return
		}
	default:
		http.NotFound(w, r)
		return
	}
	ip := net.ParseIP(body.IP)
	if !sameOrigin(r) || ip == nil || ip.To4() == nil || ip.String() != body.IP || s.validateNodeIPs(session, []string{body.IP}) != nil {
		http.Error(w, "node unavailable", http.StatusForbidden)
		return
	}
	session.mu.Lock()
	node, found := session.Nodes[body.IP]
	trusted := session.TrustedKeys[body.IP] != ""
	session.mu.Unlock()
	if !found || !trusted || !node.Reachable || node.InspectedAt.IsZero() || node.Error != "" || node.HostKeyRequired {
		http.Error(w, "node unavailable", http.StatusForbidden)
		return
	}
	s.mu.Lock()
	if s.recordingBusy == nil {
		s.recordingBusy = make(map[string]bool)
	}
	busy := s.recordingBusy[body.IP]
	if !busy {
		s.recordingBusy[body.IP] = true
	}
	s.mu.Unlock()
	if busy {
		http.Error(w, "SignalSink request already in progress", http.StatusConflict)
		return
	}
	defer func() { s.mu.Lock(); delete(s.recordingBusy, body.IP); s.mu.Unlock() }()
	ctx, cancel := context.WithTimeout(r.Context(), 5*time.Second)
	defer cancel()
	stop := context.AfterFunc(session.ctx, cancel)
	defer stop()
	argument := body.Directory
	if operation == "stop" {
		argument = body.RecordingID
	}
	result, err := s.remote.SignalSink(ctx, session, body.IP, operation, argument)
	if err != nil {
		http.Error(w, "SignalSink state unknown: "+err.Error(), http.StatusBadGateway)
		return
	}
	writeJSON(w, http.StatusOK, result)
}
