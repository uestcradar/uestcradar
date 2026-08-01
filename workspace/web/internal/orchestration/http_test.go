package orchestration

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
)

type fakeRemote struct {
	mu      sync.Mutex
	started []string
	failIP  string
}

func (f *fakeRemote) Inspect(*Session, string) (NodeInspection, error)      { return NodeInspection{}, nil }
func (f *fakeRemote) PullWorker(*Session, string, string) error             { return nil }
func (f *fakeRemote) UploadAndValidate(*Session, PlannedNode) (bool, error) { return false, nil }
func (f *fakeRemote) Start(_ *Session, node PlannedNode) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	if node.IP == f.failIP {
		return &HostKeyError{IP: node.IP}
	}
	f.started = append(f.started, node.IP)
	return nil
}

func testService() *Service {
	return &Service{sessions: NewSessionStore(), remote: &fakeRemote{}, advertiseHost: "10.0.0.99", secureCookies: true, discovered: map[string]NodeInspection{}}
}

func TestSessionCookieAndCSRF(t *testing.T) {
	service := testService()
	request := httptest.NewRequest(http.MethodPost, "https://controller/api/v1/session", strings.NewReader(`{"username":"root","password":"secret"}`))
	request.Host = "controller"
	response := httptest.NewRecorder()
	service.ServeHTTP(response, request)
	if response.Code != http.StatusCreated {
		t.Fatalf("create session: %d %s", response.Code, response.Body.String())
	}
	cookies := response.Result().Cookies()
	if len(cookies) != 1 || !cookies[0].HttpOnly || !cookies[0].Secure || cookies[0].SameSite != http.SameSiteStrictMode {
		t.Fatalf("insecure cookie: %#v", cookies)
	}

	nodesRequest := httptest.NewRequest(http.MethodPost, "https://controller/api/v1/orchestration/nodes", strings.NewReader(`{"ip":"10.0.0.1"}`))
	nodesRequest.Host = "controller"
	nodesRequest.AddCookie(cookies[0])
	nodesRequest.Header.Set("Origin", "https://controller")
	nodesResponse := httptest.NewRecorder()
	service.ServeHTTP(nodesResponse, nodesRequest)
	if nodesResponse.Code != http.StatusForbidden {
		t.Fatalf("missing CSRF should fail, got %d", nodesResponse.Code)
	}
}

func TestDeploymentStartsSinkToSourceAndStopsOnFailure(t *testing.T) {
	remote := &fakeRemote{failIP: "10.0.0.2"}
	service := testService()
	service.remote = remote
	session, _ := service.sessions.Create(Credentials{Username: "root", Password: []byte("x")})
	plan := DeploymentPlan{Nodes: []PlannedNode{{IP: "10.0.0.1", Role: "source"}, {IP: "10.0.0.2", Role: "operator"}, {IP: "10.0.0.3", Role: "sink"}}}
	task := service.newTask(session, "deployment")
	service.deploy(session, plan, true, task.ID)
	if len(remote.started) != 1 || remote.started[0] != "10.0.0.3" {
		t.Fatalf("unexpected start order: %#v", remote.started)
	}
	session.mu.Lock()
	status := session.Tasks[task.ID].Status
	session.mu.Unlock()
	if status != "failed" {
		t.Fatalf("task status = %s", status)
	}
}

func TestInspectRejectsNodeOutsideDiscoveryAndCustomList(t *testing.T) {
	service := testService()
	service.discovered["10.0.0.1"] = NodeInspection{IP: "10.0.0.1"}
	session, _ := service.sessions.Create(Credentials{Username: "root", Password: []byte("x")})
	request := httptest.NewRequest(http.MethodPost, "https://controller/api/v1/orchestration/inspect", strings.NewReader(`{"ips":["10.0.0.2"]}`))
	request.Host = "controller"
	request.AddCookie(&http.Cookie{Name: sessionCookieName, Value: session.ID})
	request.Header.Set("Origin", "https://controller")
	request.Header.Set("X-CSRF-Token", session.CSRF)
	response := httptest.NewRecorder()
	service.ServeHTTP(response, request)
	if response.Code != http.StatusBadRequest {
		t.Fatalf("unexpected inspect status: %d %s", response.Code, response.Body.String())
	}
}
