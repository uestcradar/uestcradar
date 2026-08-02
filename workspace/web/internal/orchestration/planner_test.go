package orchestration

import (
	"strings"
	"testing"
	"time"
)

func inspectedNode(ip string, worker ImageInfo) NodeInspection {
	return NodeInspection{IP: ip, Reachable: true, Architecture: "aarch64", ComposeCLI: "v1", SidecarImageID: "sha256:sidecar", SidecarContract: "sidecar/v1", Workers: []ImageInfo{worker}, RDMA: []RDMAInterface{{Device: "hns_1", Port: "1", NetDev: "enp1s0", IPv4: ip, State: "ACTIVE"}}}
}

func TestBuildPlanRejectsDifferentSidecarLatestImages(t *testing.T) {
	worker := ImageInfo{Reference: workerRepository + "cascade-worker-latest", ID: "sha256:worker", Architecture: "arm64", Contract: WorkerContract{Roles: []string{"source", "sink"}, Input: "1:1", Output: "1:1"}}
	first := inspectedNode("10.0.0.1", worker)
	second := inspectedNode("10.0.0.2", worker)
	second.SidecarImageID = "sha256:different"
	_, err := BuildPlan(PlanRequest{Chain: []ChainEntry{{IP: first.IP, RDMADevice: "hns_1:1", WorkerImage: worker.Reference}, {IP: second.IP, RDMADevice: "hns_1:1", WorkerImage: worker.Reference}}}, map[string]NodeInspection{first.IP: first, second.IP: second}, "10.0.0.99", time.Now())
	if err == nil || !strings.Contains(err.Error(), "Image ID differs") {
		t.Fatalf("expected Sidecar consistency error, got %v", err)
	}
}

func TestBuildPlanSourceOperatorSink(t *testing.T) {
	worker := ImageInfo{Reference: workerRepository + "cascade-worker-latest", ID: "sha256:worker", Architecture: "arm64", Contract: WorkerContract{Roles: []string{"source", "operator", "sink"}, Input: "1:1", Output: "1:1"}}
	nodes := map[string]NodeInspection{}
	request := PlanRequest{Chain: []ChainEntry{{IP: "10.0.0.1", RDMADevice: "hns_1:1", WorkerImage: worker.Reference}, {IP: "10.0.0.2", RDMADevice: "hns_1:1", WorkerImage: worker.Reference}, {IP: "10.0.0.3", RDMADevice: "hns_1:1", WorkerImage: worker.Reference}}}
	for _, entry := range request.Chain {
		nodes[entry.IP] = inspectedNode(entry.IP, worker)
	}
	plan, err := BuildPlan(request, nodes, "10.0.0.99", time.Unix(1, 0))
	if err != nil {
		t.Fatal(err)
	}
	if plan.Nodes[0].Role != "source" || plan.Nodes[1].Role != "operator" || plan.Nodes[2].Role != "sink" {
		t.Fatalf("roles not inferred: %#v", plan.Nodes)
	}
	if !strings.Contains(plan.Nodes[0].env, "UPSTREAM_ROLE=disabled") || !strings.Contains(plan.Nodes[2].env, "DOWNSTREAM_ROLE=disabled") {
		t.Fatal("terminal legs not disabled")
	}
	if !strings.Contains(plan.Nodes[1].env, "UCX_NET_DEVICES=hns_1:1,enp1s0") || !strings.Contains(plan.Nodes[1].env, "TELEMETRY_HOST=10.0.0.99") {
		t.Fatal("RDMA or telemetry env missing")
	}
	if strings.Contains(plan.Nodes[1].compose, "entrypoint") || strings.Contains(plan.Nodes[1].compose, "docker pull") {
		t.Fatal("generic Compose must preserve Entrypoint and never pull")
	}
}

func TestBuildPlanPropagatesSelectedSlotCountAndRejectsSmallSHM(t *testing.T) {
	worker := ImageInfo{Reference: workerRepository + "cascade-worker-latest", ID: "sha256:worker", Architecture: "arm64", Contract: WorkerContract{Roles: []string{"source", "sink"}, Input: "1:1", Output: "1:1"}}
	nodes := map[string]NodeInspection{
		"10.0.0.1": inspectedNode("10.0.0.1", worker),
		"10.0.0.2": inspectedNode("10.0.0.2", worker),
	}
	request := PlanRequest{Chain: []ChainEntry{{IP: "10.0.0.1", RDMADevice: "hns_1:1", WorkerImage: worker.Reference}, {IP: "10.0.0.2", RDMADevice: "hns_1:1", WorkerImage: worker.Reference}}, SlotCount: 128, MaxPayloadBytes: 1048576, SHMSize: "512m"}
	plan, err := BuildPlan(request, nodes, "10.0.0.99", time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(plan.Nodes[0].env, "SLOT_COUNT=128") || !strings.Contains(plan.Nodes[0].env, "SIDECAR_SHM_SIZE=512m") {
		t.Fatalf("selected RingBuffer configuration missing:\n%s", plan.Nodes[0].env)
	}
	request.SHMSize = "256m"
	if _, err := BuildPlan(request, nodes, "10.0.0.99", time.Now()); err == nil || !strings.Contains(err.Error(), "too small") {
		t.Fatalf("expected small shm_size rejection, got %v", err)
	}
}

func TestBuildPlanRejectsTypeMismatch(t *testing.T) {
	first := ImageInfo{Reference: workerRepository + "source-latest", ID: "sha256:a", Architecture: "arm64", Contract: WorkerContract{Roles: []string{"source"}, Input: "none", Output: "1:1"}}
	second := ImageInfo{Reference: workerRepository + "sink-latest", ID: "sha256:b", Architecture: "arm64", Contract: WorkerContract{Roles: []string{"sink"}, Input: "2:1", Output: "none"}}
	nodes := map[string]NodeInspection{"10.0.0.1": inspectedNode("10.0.0.1", first), "10.0.0.2": inspectedNode("10.0.0.2", second)}
	_, err := BuildPlan(PlanRequest{Chain: []ChainEntry{{IP: "10.0.0.1", RDMADevice: "hns_1:1", WorkerImage: first.Reference}, {IP: "10.0.0.2", RDMADevice: "hns_1:1", WorkerImage: second.Reference}}}, nodes, "10.0.0.99", time.Now())
	if err == nil || !strings.Contains(err.Error(), "type mismatch") {
		t.Fatalf("expected type mismatch, got %v", err)
	}
}
