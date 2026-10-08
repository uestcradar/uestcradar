package orchestration

import (
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

type frontendSyncRemote struct {
	fakeRemote
	failure error
	calls   atomic.Int32
}

func (r *frontendSyncRemote) PullFrontend(*Session, string, CommandOutput) error {
	r.calls.Add(1)
	return r.failure
}
func (r *frontendSyncRemote) Inspect(_ *Session, ip string, _ CommandOutput) (NodeInspection, error) {
	return NodeInspection{IP: ip, NodeID: "node-1"}, nil
}

func TestFrontendImageSync(t *testing.T) {
	for _, scenario := range []string{"success", "pull-failure", "uninspected", "unknown-node", "no-csrf"} {
		t.Run(scenario, func(t *testing.T) {
			service := testService()
			remote := &frontendSyncRemote{}
			service.remote = remote
			session, err := service.sessions.Create(Credentials{Username: "root", Password: []byte("test-only")})
			if err != nil {
				t.Fatal(err)
			}
			defer service.sessions.Delete(session.ID)
			ip := "10.0.0.1"
			service.discovered[ip] = NodeInspection{IP: ip}
			session.Nodes[ip] = NodeInspection{IP: ip, NodeID: "node-1"}
			status := http.StatusAccepted
			switch scenario {
			case "pull-failure":
				remote.failure = errors.New("pull failed")
			case "uninspected":
				delete(session.Nodes, ip)
				status = http.StatusConflict
			case "unknown-node":
				ip = "10.0.0.2"
				status = http.StatusBadRequest
			case "no-csrf":
				status = http.StatusForbidden
			}
			request := httptest.NewRequest(http.MethodPost, "https://controller/api/v1/orchestration/images/frontend/sync", strings.NewReader(`{"ip":"`+ip+`"}`))
			request.Header.Set("Origin", "https://controller")
			request.AddCookie(&http.Cookie{Name: sessionCookieName, Value: session.ID})
			if scenario != "no-csrf" {
				request.Header.Set("X-CSRF-Token", session.CSRF)
			}
			response := httptest.NewRecorder()
			service.ServeHTTP(response, request)
			if response.Code != status {
				t.Fatalf("status %d: %s", response.Code, response.Body.String())
			}
			if status != http.StatusAccepted {
				if remote.calls.Load() != 0 {
					t.Fatal("unauthorized pull")
				}
				return
			}
			var task Task
			if err := json.Unmarshal(response.Body.Bytes(), &task); err != nil {
				t.Fatal(err)
			}
			deadline := time.Now().Add(2 * time.Second)
			var state, message, binding string
			for time.Now().Before(deadline) {
				session.mu.Lock()
				state = session.Tasks[task.ID].Status
				message = session.Tasks[task.ID].Message
				binding = session.Nodes[ip].NodeID
				session.mu.Unlock()
				if state == "completed" || state == "failed" {
					break
				}
				time.Sleep(time.Millisecond)
			}
			expected := "completed"
			if scenario == "pull-failure" {
				expected = "failed"
			}
			if state != expected || remote.calls.Load() != 1 || task.Kind != "frontend-image-sync" {
				t.Fatalf("task=%s message=%s calls=%d", state, message, remote.calls.Load())
			}
			if binding != "node-1" {
				t.Fatal("lost node telemetry binding")
			}
			if len(remote.started)+len(remote.stopped)+len(remote.uploaded) != 0 {
				t.Fatal("image sync modified running deployment")
			}
		})
	}
}
