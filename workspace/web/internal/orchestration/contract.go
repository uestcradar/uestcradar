package orchestration

import (
	"fmt"
	"regexp"
	"sort"
	"strings"
)

var typeContractPattern = regexp.MustCompile(`^[1-9][0-9]*:[1-9][0-9]*$`)

func ParseWorkerContract(labels map[string]string) (WorkerContract, error) {
	if labels["io.uestcradar.contract"] != "worker/v1" {
		return WorkerContract{}, fmt.Errorf("contract is not worker/v1")
	}
	rolesText := labels["io.uestcradar.roles"]
	input := labels["io.uestcradar.input"]
	output := labels["io.uestcradar.output"]
	if !validTypeContract(input) || !validTypeContract(output) {
		return WorkerContract{}, fmt.Errorf("invalid input/output contract")
	}
	seen := map[string]bool{}
	roles := make([]string, 0, 3)
	for _, role := range strings.Split(rolesText, ",") {
		if role != "source" && role != "operator" && role != "sink" {
			return WorkerContract{}, fmt.Errorf("unsupported role %q", role)
		}
		if seen[role] {
			return WorkerContract{}, fmt.Errorf("duplicate role %q", role)
		}
		seen[role] = true
		roles = append(roles, role)
	}
	if len(roles) == 0 {
		return WorkerContract{}, fmt.Errorf("roles are empty")
	}
	sort.Strings(roles)
	return WorkerContract{Roles: roles, Input: input, Output: output}, nil
}

func validTypeContract(value string) bool {
	return value == "none" || typeContractPattern.MatchString(value)
}

func supportsRole(contract WorkerContract, role string) bool {
	for _, candidate := range contract.Roles {
		if candidate == role {
			return true
		}
	}
	return false
}
