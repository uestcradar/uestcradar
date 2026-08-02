package orchestration

import (
	"fmt"
	"net"
	"regexp"
	"strconv"
	"strings"
	"time"
)

const distributedCompose = `version: "2.4"

services:
  sidecar-node:
    image: ${SIDECAR_IMAGE:?set SIDECAR_IMAGE}
    platform: linux/arm64
    network_mode: host
    ipc: shareable
    shm_size: ${SIDECAR_SHM_SIZE:-256m}
    restart: unless-stopped
    devices:
      - /dev/infiniband:/dev/infiniband
    cap_add: [IPC_LOCK]
    ulimits:
      memlock: {soft: -1, hard: -1}
    environment:
      NODE_ID: ${NODE_ID}
      SIDECAR_UPSTREAM_ROLE: ${UPSTREAM_ROLE}
      SIDECAR_UPSTREAM_PEER_NODE_ID: ${UPSTREAM_PEER_NODE_ID:-}
      SIDECAR_UPSTREAM_BIND_HOST: ${UPSTREAM_BIND_HOST:-0.0.0.0}
      SIDECAR_UPSTREAM_PEER_HOST: ${UPSTREAM_PEER_HOST:-127.0.0.1}
      SIDECAR_UPSTREAM_PORT: ${UPSTREAM_PORT:-13337}
      SIDECAR_UPSTREAM_CONNECT_TIMEOUT_MS: ${CONNECT_TIMEOUT_MS:-1000}
      SIDECAR_UPSTREAM_DATA_PATH: strict-rdma
      SIDECAR_DOWNSTREAM_ROLE: ${DOWNSTREAM_ROLE}
      SIDECAR_DOWNSTREAM_PEER_NODE_ID: ${DOWNSTREAM_PEER_NODE_ID:-}
      SIDECAR_DOWNSTREAM_BIND_HOST: ${DOWNSTREAM_BIND_HOST:-0.0.0.0}
      SIDECAR_DOWNSTREAM_PEER_HOST: ${DOWNSTREAM_PEER_HOST:-127.0.0.1}
      SIDECAR_DOWNSTREAM_PORT: ${DOWNSTREAM_PORT:-13337}
      SIDECAR_DOWNSTREAM_CONNECT_TIMEOUT_MS: ${CONNECT_TIMEOUT_MS:-1000}
      SIDECAR_DOWNSTREAM_DATA_PATH: strict-rdma
      SIDECAR_UPSTREAM_SHM_NAME: /uestcradar_cascade_upstream
      SIDECAR_DOWNSTREAM_SHM_NAME: /uestcradar_cascade_downstream
      SIDECAR_UPSTREAM_SLOT_COUNT: ${SLOT_COUNT:-64}
      SIDECAR_UPSTREAM_MAX_PAYLOAD_BYTES: ${MAX_PAYLOAD_BYTES:-1048576}
      SIDECAR_UPSTREAM_TYPE_ID: ${UPSTREAM_TYPE_ID:-1}
      SIDECAR_UPSTREAM_TYPE_VERSION: ${UPSTREAM_TYPE_VERSION:-1}
      SIDECAR_DOWNSTREAM_SLOT_COUNT: ${SLOT_COUNT:-64}
      SIDECAR_DOWNSTREAM_MAX_PAYLOAD_BYTES: ${MAX_PAYLOAD_BYTES:-1048576}
      SIDECAR_DOWNSTREAM_TYPE_ID: ${DOWNSTREAM_TYPE_ID:-1}
      SIDECAR_DOWNSTREAM_TYPE_VERSION: ${DOWNSTREAM_TYPE_VERSION:-1}
      UCX_TLS: ${UCX_TLS:-rc_verbs,tcp}
      UCX_NET_DEVICES: ${UCX_NET_DEVICES}
      UCX_SOCKADDR_TLS_PRIORITY: tcp
      UCX_RC_VERBS_TX_MIN_SGE: "2"
      UCX_RC_VERBS_TX_MIN_INLINE: "0"
      UCX_RNDV_THRESH: "64"
      UCX_ZCOPY_THRESH: "64"
      UCX_RNDV_SCHEME: put_zcopy
      UCX_PROTO_INFO: "n"
      UCX_LOG_LEVEL: warn
      TELEMETRY_HOST: ${TELEMETRY_HOST}
      TELEMETRY_PORT: ${TELEMETRY_PORT:-9900}
      SAMPLE_INTERVAL: ${SAMPLE_INTERVAL:-100}

  worker-node:
    image: ${WORKER_IMAGE:?set WORKER_IMAGE}
    platform: linux/arm64
    network_mode: host
    ipc: service:sidecar-node
    restart: unless-stopped
    environment:
      CASCADE_ROLE: ${CASCADE_ROLE}
      UESTCRADAR_UPSTREAM_SHM_NAME: /uestcradar_cascade_upstream
      UESTCRADAR_DOWNSTREAM_SHM_NAME: /uestcradar_cascade_downstream
    depends_on: [sidecar-node]
`

var shmSizePattern = regexp.MustCompile(`^[1-9][0-9]*(m|g)$`)

func BuildPlan(request PlanRequest, nodes map[string]NodeInspection, advertiseHost string, now time.Time) (DeploymentPlan, error) {
	if len(request.Chain) < 2 {
		return DeploymentPlan{}, fmt.Errorf("chain requires a Source and Sink")
	}
	if parsed := net.ParseIP(advertiseHost); parsed == nil || parsed.To4() == nil {
		return DeploymentPlan{}, fmt.Errorf("TELEMETRY_ADVERTISE_HOST must be an IPv4 address")
	}
	if request.SlotCount == 0 {
		request.SlotCount = 64
	}
	if request.SlotCount != 32 && request.SlotCount != 64 && request.SlotCount != 128 && request.SlotCount != 256 {
		return DeploymentPlan{}, fmt.Errorf("slot_count must be one of 32, 64, 128, or 256")
	}
	if request.MaxPayloadBytes == 0 {
		request.MaxPayloadBytes = 1048576
	}
	if request.SHMSize == "" {
		request.SHMSize = "256m"
	}
	if !shmSizePattern.MatchString(request.SHMSize) {
		return DeploymentPlan{}, fmt.Errorf("invalid shm_size")
	}
	if shmSizeBytes(request.SHMSize) < minimumSHMBytes(request.SlotCount, request.MaxPayloadBytes) {
		return DeploymentPlan{}, fmt.Errorf("shm_size is too small for two configured RingBuffers")
	}

	seenIPs := map[string]bool{}
	sidecarImageID := ""
	planned := make([]PlannedNode, len(request.Chain))
	for index, entry := range request.Chain {
		if seenIPs[entry.IP] {
			return DeploymentPlan{}, fmt.Errorf("node %s is repeated", entry.IP)
		}
		seenIPs[entry.IP] = true
		inspection, ok := nodes[entry.IP]
		if !ok || inspection.Error != "" || inspection.HostKeyRequired {
			return DeploymentPlan{}, fmt.Errorf("node %s is not successfully inspected", entry.IP)
		}
		if inspection.Architecture != "aarch64" && inspection.Architecture != "arm64" {
			return DeploymentPlan{}, fmt.Errorf("node %s is not ARM64", entry.IP)
		}
		if inspection.ComposeCLI != "v1" && inspection.ComposeCLI != "v2" {
			return DeploymentPlan{}, fmt.Errorf("node %s has no supported Docker Compose CLI", entry.IP)
		}
		if inspection.SidecarContract != "sidecar/v1" || inspection.SidecarImageID == "" {
			return DeploymentPlan{}, fmt.Errorf("node %s has no local sidecar/v1 image", entry.IP)
		}
		if sidecarImageID == "" {
			sidecarImageID = inspection.SidecarImageID
		} else if inspection.SidecarImageID != sidecarImageID {
			return DeploymentPlan{}, fmt.Errorf("sidecar:latest Image ID differs across selected nodes")
		}
		role := roleAt(index, len(request.Chain))
		worker, ok := findWorker(inspection.Workers, entry.WorkerImage)
		if !ok {
			return DeploymentPlan{}, fmt.Errorf("Worker %s is not local on %s", entry.WorkerImage, entry.IP)
		}
		if !supportsRole(worker.Contract, role) {
			return DeploymentPlan{}, fmt.Errorf("Worker %s does not support %s", entry.WorkerImage, role)
		}
		if worker.Architecture != "arm64" {
			return DeploymentPlan{}, fmt.Errorf("Worker %s is not ARM64", entry.WorkerImage)
		}
		if role == "source" && worker.Contract.Output == "none" {
			return DeploymentPlan{}, fmt.Errorf("Source output cannot be none")
		}
		if role == "operator" && (worker.Contract.Input == "none" || worker.Contract.Output == "none") {
			return DeploymentPlan{}, fmt.Errorf("Operator requires input and output")
		}
		if role == "sink" && worker.Contract.Input == "none" {
			return DeploymentPlan{}, fmt.Errorf("Sink input cannot be none")
		}
		rdma, ok := findRDMA(inspection.RDMA, entry.RDMADevice)
		if !ok || rdma.IPv4 == "" || !strings.EqualFold(rdma.State, "ACTIVE") {
			return DeploymentPlan{}, fmt.Errorf("RDMA interface %s is unavailable on %s", entry.RDMADevice, entry.IP)
		}
		planned[index] = PlannedNode{
			IP: entry.IP, NodeID: fmt.Sprintf("node-%d", index+1), Role: role,
			RDMADevice: ucxDevice(rdma), NetDev: rdma.NetDev, RDMAIP: rdma.IPv4,
			WorkerReference: worker.Reference, WorkerImageID: worker.ID,
			SidecarImageID: inspection.SidecarImageID, Input: worker.Contract.Input,
			Output: worker.Contract.Output, ExistingDeployment: inspection.ExistingDeployment,
			compose: distributedCompose,
		}
	}
	for index := 0; index < len(planned)-1; index++ {
		if planned[index].Output != planned[index+1].Input {
			return DeploymentPlan{}, fmt.Errorf("type mismatch: %s output %s != %s input %s", planned[index].NodeID, planned[index].Output, planned[index+1].NodeID, planned[index+1].Input)
		}
	}
	for index := range planned {
		planned[index].env = renderNodeEnv(planned, index, request, advertiseHost)
		planned[index].EnvPreview = planned[index].env
	}
	id, err := randomToken()
	if err != nil {
		return DeploymentPlan{}, err
	}
	return DeploymentPlan{ID: id, CreatedAt: now, Nodes: planned}, nil
}

func shmSizeBytes(value string) uint64 {
	amount, _ := strconv.ParseUint(value[:len(value)-1], 10, 64)
	if strings.HasSuffix(value, "g") {
		return amount * 1024 * 1024 * 1024
	}
	return amount * 1024 * 1024
}

func minimumSHMBytes(slotCount, maxPayloadBytes uint32) uint64 {
	// Both rings share /dev/shm. The extra page per slot safely covers fixed
	// headers and alignment without coupling the planner to the C++ layout.
	return 2 * uint64(slotCount) * (uint64(maxPayloadBytes) + 4096)
}

func roleAt(index, total int) string {
	if index == 0 {
		return "source"
	}
	if index == total-1 {
		return "sink"
	}
	return "operator"
}

func findWorker(images []ImageInfo, reference string) (ImageInfo, bool) {
	for _, image := range images {
		if image.Reference == reference {
			return image, true
		}
	}
	return ImageInfo{}, false
}

func findRDMA(interfaces []RDMAInterface, selected string) (RDMAInterface, bool) {
	for _, item := range interfaces {
		if selected == item.Device || selected == ucxDevice(item) {
			return item, true
		}
	}
	return RDMAInterface{}, false
}

func ucxDevice(item RDMAInterface) string {
	if item.Port == "" || strings.Contains(item.Device, ":") {
		return item.Device
	}
	return item.Device + ":" + item.Port
}

func splitType(value string) (string, string) {
	if value == "none" {
		return "1", "1"
	}
	parts := strings.SplitN(value, ":", 2)
	return parts[0], parts[1]
}

func renderNodeEnv(nodes []PlannedNode, index int, request PlanRequest, advertiseHost string) string {
	node := nodes[index]
	upRole, downRole := "listen", "connect"
	upPeerID, upPeerHost, downPeerID, downPeerHost := "", "127.0.0.1", "", "127.0.0.1"
	if index == 0 {
		upRole = "disabled"
	} else {
		upPeerID, upPeerHost = nodes[index-1].NodeID, nodes[index-1].RDMAIP
	}
	if index == len(nodes)-1 {
		downRole = "disabled"
	} else {
		downPeerID, downPeerHost = nodes[index+1].NodeID, nodes[index+1].RDMAIP
	}
	upTypeID, upTypeVersion := splitType(node.Input)
	downTypeID, downTypeVersion := splitType(node.Output)
	lines := []string{
		"SIDECAR_IMAGE=" + node.SidecarImageID,
		"WORKER_IMAGE=" + node.WorkerImageID,
		"NODE_ID=" + node.NodeID,
		"CASCADE_ROLE=" + node.Role,
		"UPSTREAM_ROLE=" + upRole,
		"UPSTREAM_PEER_NODE_ID=" + upPeerID,
		"UPSTREAM_BIND_HOST=" + node.RDMAIP,
		"UPSTREAM_PEER_HOST=" + upPeerHost,
		"UPSTREAM_PORT=13337",
		"DOWNSTREAM_ROLE=" + downRole,
		"DOWNSTREAM_PEER_NODE_ID=" + downPeerID,
		"DOWNSTREAM_BIND_HOST=" + node.RDMAIP,
		"DOWNSTREAM_PEER_HOST=" + downPeerHost,
		"DOWNSTREAM_PORT=13337",
		"CONNECT_TIMEOUT_MS=1000",
		"UPSTREAM_TYPE_ID=" + upTypeID,
		"UPSTREAM_TYPE_VERSION=" + upTypeVersion,
		"DOWNSTREAM_TYPE_ID=" + downTypeID,
		"DOWNSTREAM_TYPE_VERSION=" + downTypeVersion,
		"SLOT_COUNT=" + strconv.FormatUint(uint64(request.SlotCount), 10),
		"MAX_PAYLOAD_BYTES=" + strconv.FormatUint(uint64(request.MaxPayloadBytes), 10),
		"SIDECAR_SHM_SIZE=" + request.SHMSize,
		"UCX_TLS=rc_verbs,tcp",
		"UCX_NET_DEVICES=" + node.RDMADevice + "," + node.NetDev,
		"TELEMETRY_HOST=" + advertiseHost,
		"TELEMETRY_PORT=9900",
		"SAMPLE_INTERVAL=100",
	}
	return strings.Join(lines, "\n") + "\n"
}
