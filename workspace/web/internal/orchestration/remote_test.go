package orchestration

import (
	"strings"
	"testing"
)

func TestParseRDMASeparatesDeviceNetdevAndIP(t *testing.T) {
	result := parseRDMA("link hns_1/1 state ACTIVE physical_state LINK_UP netdev enp125s0f1", "2: enp125s0f1 inet 192.162.2.64/24 brd 192.162.2.255 scope global enp125s0f1")
	if len(result) != 1 || result[0].Device != "hns_1" || result[0].Port != "1" || result[0].NetDev != "enp125s0f1" || result[0].IPv4 != "192.162.2.64" {
		t.Fatalf("unexpected RDMA parse: %#v", result)
	}
}

func TestComposeCommandPrefersV1AndNeverPulls(t *testing.T) {
	command := composeCommandFor(PlannedNode{}, "/tmp/compose.yaml", "/tmp/node.env", "up -d --no-build")
	if !strings.Contains(command, "docker-compose version --short") || !strings.Contains(command, "elif docker compose version --short") || strings.Contains(command, "pull") {
		t.Fatalf("unexpected command: %s", command)
	}
}

func TestDeploymentStatus(t *testing.T) {
	tests := []struct {
		input  string
		exists bool
		status string
	}{
		{"", false, "absent"},
		{"sidecar-node|exited\nworker-node|exited", true, "stopped"},
		{"sidecar-node|running\nworker-node|running", true, "running"},
		{"sidecar-node|running\nworker-node|exited", true, "partial"},
	}
	for _, test := range tests {
		exists, status := deploymentStatus(test.input)
		if exists != test.exists || status != test.status {
			t.Fatalf("deploymentStatus(%q) = %v, %q", test.input, exists, status)
		}
	}
}
