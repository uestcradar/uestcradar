package orchestration

import "time"

var DefaultNodeIPs = []string{
	"192.162.2.16", "192.162.2.32", "192.162.2.64",
	"192.162.2.80", "192.162.2.128", "192.162.2.144",
	"192.162.2.160", "192.162.2.176", "192.162.2.192",
}

type WorkerContract struct {
	Roles  []string `json:"roles"`
	Input  string   `json:"input"`
	Output string   `json:"output"`
}

type ImageInfo struct {
	Reference    string         `json:"reference"`
	ID           string         `json:"id"`
	Architecture string         `json:"architecture"`
	Entrypoint   []string       `json:"entrypoint,omitempty"`
	Command      []string       `json:"command,omitempty"`
	Contract     WorkerContract `json:"contract"`
}

type RDMAInterface struct {
	Device        string `json:"device"`
	Port          string `json:"port"`
	NetDev        string `json:"netdev"`
	IPv4          string `json:"ipv4"`
	State         string `json:"state"`
	PhysicalState string `json:"physical_state"`
}

type NodeInspection struct {
	IP                 string          `json:"ip"`
	Reachable          bool            `json:"reachable"`
	Hostname           string          `json:"hostname,omitempty"`
	Architecture       string          `json:"architecture,omitempty"`
	DockerVersion      string          `json:"docker_version,omitempty"`
	ComposeCLI         string          `json:"compose_cli,omitempty"`
	CPUs               string          `json:"cpus,omitempty"`
	MemoryBytes        string          `json:"memory_bytes,omitempty"`
	DockerDisk         string          `json:"docker_disk,omitempty"`
	RDMA               []RDMAInterface `json:"rdma"`
	SidecarImageID     string          `json:"sidecar_image_id,omitempty"`
	SidecarContract    string          `json:"sidecar_contract,omitempty"`
	Workers            []ImageInfo     `json:"workers"`
	ExistingDeployment bool            `json:"existing_deployment"`
	DeploymentState    string          `json:"deployment_state,omitempty"`
	HostKeyFingerprint string          `json:"host_key_fingerprint,omitempty"`
	HostKeyRequired    bool            `json:"host_key_required,omitempty"`
	Error              string          `json:"error,omitempty"`
	InspectedAt        time.Time       `json:"inspected_at,omitempty"`
}

type TaskOutputChunk struct {
	Sequence uint64    `json:"sequence"`
	At       time.Time `json:"at"`
	IP       string    `json:"ip,omitempty"`
	Stream   string    `json:"stream"`
	Text     string    `json:"text"`
}

type ChainEntry struct {
	IP          string `json:"ip"`
	RDMADevice  string `json:"rdma_device"`
	WorkerImage string `json:"worker_image"`
}

type PlanRequest struct {
	Chain           []ChainEntry `json:"chain"`
	SlotCount       uint32       `json:"slot_count"`
	MaxPayloadBytes uint32       `json:"max_payload_bytes"`
	SHMSize         string       `json:"shm_size"`
}

type PlannedNode struct {
	IP                 string `json:"ip"`
	NodeID             string `json:"node_id"`
	Role               string `json:"role"`
	RDMADevice         string `json:"rdma_device"`
	NetDev             string `json:"netdev"`
	RDMAIP             string `json:"rdma_ip"`
	WorkerReference    string `json:"worker_reference"`
	WorkerImageID      string `json:"worker_image_id"`
	SidecarImageID     string `json:"sidecar_image_id"`
	Input              string `json:"input"`
	Output             string `json:"output"`
	ExistingDeployment bool   `json:"existing_deployment"`
	EnvPreview         string `json:"env_preview"`
	compose            string
	env                string
}

type DeploymentPlan struct {
	ID        string        `json:"id"`
	CreatedAt time.Time     `json:"created_at"`
	Nodes     []PlannedNode `json:"nodes"`
}

type Task struct {
	ID                 string            `json:"id"`
	Kind               string            `json:"kind"`
	Status             string            `json:"status"`
	Message            string            `json:"message,omitempty"`
	CurrentIP          string            `json:"current_ip,omitempty"`
	Completed          []string          `json:"completed,omitempty"`
	CreatedAt          time.Time         `json:"created_at"`
	UpdatedAt          time.Time         `json:"updated_at"`
	Output             []TaskOutputChunk `json:"output,omitempty"`
	OutputTruncated    bool              `json:"output_truncated,omitempty"`
	outputBytes        int
	nextOutputSequence uint64
}
