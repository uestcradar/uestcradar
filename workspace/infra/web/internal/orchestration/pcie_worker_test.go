package orchestration

import (
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// Literal release identity: changing an allowlist must not silently change this fixture.
func approvedPCIeFixture() ImageInfo {
	return ImageInfo{Reference: workerRepository + "pcie-source-latest", DigestReference: "registry.chengyistudio.com/cxx/worker@sha256:6e9b3da9aa6cd0960cdf3e81b68e4f368ea85a797fda15622eeae61be16b0f59", ID: "sha256:pcie", Architecture: "arm64", Entrypoint: []string{"/app/pcie_source"}, Contract: WorkerContract{Roles: []string{"source"}, Input: "none", Output: "4:1"}}
}

func pciePlan(worker ImageInfo, transport string) (DeploymentPlan, error) {
	sink := ImageInfo{Reference: workerRepository + "signalsink-latest", ID: "sha256:sink", Architecture: "arm64", Contract: WorkerContract{Component: "signalsink", Roles: []string{"sink"}, Input: "any", Output: "none"}}
	return BuildPlan(PlanRequest{Transport: transport, Chain: []ChainEntry{{IP: "10.0.0.1", RDMADevice: "hns_1:1", WorkerImage: worker.Reference}, {IP: "10.0.0.2", RDMADevice: "hns_1:1", WorkerImage: sink.Reference}}}, map[string]NodeInspection{"10.0.0.1": inspectedNode("10.0.0.1", worker), "10.0.0.2": inspectedNode("10.0.0.2", sink)}, "10.0.0.99", time.Now())
}

func TestPCIeWorkerDeviceIsolation(t *testing.T) {
	for _, transport := range []string{"strict-rdma", "tcp"} {
		t.Run(transport, func(t *testing.T) {
			plan, err := pciePlan(approvedPCIeFixture(), transport)
			if err != nil {
				t.Fatal(err)
			}
			src := plan.Nodes[0].compose
			parts := strings.SplitN(src, "  worker-node:", 2)
			if len(parts) != 2 {
				t.Fatal("missing Worker service")
			}
			if strings.Contains(parts[0], "/dev/mem") || strings.Contains(parts[0], "SYS_RAWIO") {
				t.Fatal("hardware access escaped Worker")
			}
			for _, want := range []string{"/dev/mem:/dev/mem:rw", "cap_add: [SYS_RAWIO]", "restart: \"no\""} {
				if !strings.Contains(parts[1], want) {
					t.Fatalf("missing %s", want)
				}
			}
			if strings.Count(src, "/dev/mem:/dev/mem:rw") != 1 || strings.Contains(src, "privileged:") || strings.Contains(parts[1], "entrypoint:") {
				t.Fatal("overbroad device authorization")
			}
			if strings.Contains(plan.Nodes[1].compose, "/dev/mem") || strings.Contains(plan.Nodes[1].compose, "SYS_RAWIO") {
				t.Fatal("Sink received hardware access")
			}
			if transport == "strict-rdma" && !strings.Contains(parts[0], "/dev/infiniband") {
				t.Fatal("RDMA setup lost")
			}
			if transport == "tcp" && strings.Contains(parts[0], "/dev/infiniband") {
				t.Fatal("TCP setup changed")
			}
		})
	}
	ordinary := approvedPCIeFixture()
	ordinary.DigestReference = ""
	ordinary.Reference = workerRepository + "ordinary-source"
	ordinary.Entrypoint = []string{"/app/source"}
	plan, err := pciePlan(ordinary, "strict-rdma")
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(plan.Nodes[0].compose, "SYS_RAWIO") || strings.Contains(plan.Nodes[0].compose, "/dev/mem") {
		t.Fatal("ordinary Source received hardware access")
	}
}

func TestPCIeDevicePreflight(t *testing.T) {
	root := t.TempDir()
	script := strings.ReplaceAll(pcieDevicePreflight, "/sys/bus/pci/devices/0000:04:00.0", root)
	script = strings.ReplaceAll(script, "/dev/mem", "/dev/null") // Character device without hardware access.
	good := map[string]string{"vendor": "0x10ee\n", "device": "0x7038\n", "resource": "0x00000000ef000000 0x00000000ef000fff 0x0000000000040200\n"}
	for _, bad := range []string{"", "vendor", "device", "resource", "missing-device"} {
		t.Run(bad, func(t *testing.T) {
			for name, value := range good {
				if name == bad {
					value = "0x0\n"
				}
				if err := os.WriteFile(filepath.Join(root, name), []byte(value), 0600); err != nil {
					t.Fatal(err)
				}
			}
			command := script
			if bad == "missing-device" {
				command = strings.ReplaceAll(command, "/dev/null", filepath.Join(root, "absent"))
			}
			output, err := exec.Command("sh", "-c", command).CombinedOutput()
			if (err == nil) != (bad == "") {
				t.Fatalf("preflight %q: %v %s", bad, err, output)
			}
		})
	}
}

func TestPCIeWorkerRejectsUnapprovedIdentity(t *testing.T) {
	cases := map[string]func(*ImageInfo){
		"unapproved digest": func(w *ImageInfo) {
			w.DigestReference = "registry.chengyistudio.com/cxx/worker@sha256:" + strings.Repeat("a", 64)
		},
		"wrong entrypoint":           func(w *ImageInfo) { w.Entrypoint = []string{"/bin/sh"} },
		"extra entrypoint arguments": func(w *ImageInfo) { w.Entrypoint = append(w.Entrypoint, "--capture-only") },
		"command override":           func(w *ImageInfo) { w.Command = []string{"--capture-only"} },
		"input":                      func(w *ImageInfo) { w.Contract.Input = "4:1" },
		"output":                     func(w *ImageInfo) { w.Contract.Output = "1:3" },
		"extra role":                 func(w *ImageInfo) { w.Contract.Roles = append(w.Contract.Roles, "sink") },
		"architecture":               func(w *ImageInfo) { w.Architecture = "amd64" },
	}
	for name, change := range cases {
		t.Run(name, func(t *testing.T) {
			w := approvedPCIeFixture()
			change(&w)
			if _, err := pciePlan(w, "strict-rdma"); err == nil {
				t.Fatal("accepted unapproved hardware Worker")
			}
		})
	}
}
