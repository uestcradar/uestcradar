package orchestration

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

type hostKeyRemote struct{ fakeRemote }

func (f *hostKeyRemote) Inspect(_ *Session, ip string, _ CommandOutput) (NodeInspection, error) {
	return NodeInspection{IP: ip}, &HostKeyError{IP: ip, Fingerprint: "SHA256:test"}
}
func TestInspectionRequiresHostKeyConfirmation(t *testing.T) {
	service := testService()
	service.remote = &hostKeyRemote{}
	session, _ := service.sessions.Create(Credentials{Username: "root", Password: []byte("test")})
	defer service.sessions.Delete(session.ID)
	session.CustomIPs = []string{"10.0.0.1"}
	request := httptest.NewRequest(http.MethodPost, "/api/v1/orchestration/inspections", strings.NewReader(`{"ips":["10.0.0.1"]}`))
	response := httptest.NewRecorder()
	service.handleInspectionTask(response, request, session)
	if response.Code != http.StatusAccepted {
		t.Fatalf("status %d: %s", response.Code, response.Body.String())
	}
	var task Task
	if err := json.Unmarshal(response.Body.Bytes(), &task); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		session.mu.Lock()
		status := session.Tasks[task.ID].Status
		completed := len(session.Tasks[task.ID].Completed)
		session.mu.Unlock()
		if status == "confirmation_required" {
			if completed != 0 {
				t.Fatal("unconfirmed nodes reported as completed")
			}
			return
		}
		if status == "completed" {
			t.Fatal("inspection incorrectly reported success")
		}
		time.Sleep(time.Millisecond)
	}
	t.Fatal("inspection did not finish")
}
