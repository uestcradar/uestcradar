package orchestration

import "testing"

func TestParseWorkerContractUsesMinimalV1Labels(t *testing.T) {
	contract, err := ParseWorkerContract(map[string]string{
		"io.uestcradar.contract": "worker/v1", "io.uestcradar.roles": "source,operator,sink",
		"io.uestcradar.input": "1:1", "io.uestcradar.output": "2:1",
	})
	if err != nil || !supportsRole(contract, "operator") || contract.Output != "2:1" {
		t.Fatalf("unexpected contract: %#v %v", contract, err)
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
		"io.uestcradar.contract": "worker/v1", "io.uestcradar.roles": "operator",
		"io.uestcradar.input": "1", "io.uestcradar.output": "1:1",
	})
	if err == nil {
		t.Fatal("invalid type must be rejected")
	}
}
