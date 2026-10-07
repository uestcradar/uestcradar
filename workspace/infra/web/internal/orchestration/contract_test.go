package orchestration

import "testing"

func TestParseWorkerContractUsesMinimalV2Labels(t *testing.T) {
	contract, err := ParseWorkerContract(map[string]string{
		"io.uestcradar.contract": "worker/v2", "io.uestcradar.roles": "source,operator,sink",
		"io.uestcradar.input": "1:2", "io.uestcradar.output": "2:2",
	})
	if err != nil || !supportsRole(contract, "operator") || contract.Output != "2:2" {
		t.Fatalf("unexpected contract: %#v %v", contract, err)
	}
}

func TestParseWorkerContractRejectsRingABIV1Worker(t *testing.T) {
	_, err := ParseWorkerContract(map[string]string{
		"io.uestcradar.contract": "worker/v1", "io.uestcradar.roles": "operator",
		"io.uestcradar.input": "1:1", "io.uestcradar.output": "1:1",
	})
	if err == nil {
		t.Fatal("worker/v1 must not be mixed with the Ring ABI v6 control plane")
	}
}

func TestParseWorkerContractRejectsObsoleteAliases(t *testing.T) {
	_, err := ParseWorkerContract(map[string]string{
		"io.uestcradar.kind": "worker", "io.uestcradar.worker.roles": "operator",
		"io.uestcradar.worker.input.type-id": "1", "io.uestcradar.worker.output.type-id": "1",
	})
	if err == nil {
		t.Fatal("obsolete labels must not be accepted")
	}
}

func TestParseWorkerContractRejectsInvalidType(t *testing.T) {
	_, err := ParseWorkerContract(map[string]string{
		"io.uestcradar.contract": "worker/v2", "io.uestcradar.roles": "operator",
		"io.uestcradar.input": "1", "io.uestcradar.output": "1:2",
	})
	if err == nil {
		t.Fatal("invalid type must be rejected")
	}
}
