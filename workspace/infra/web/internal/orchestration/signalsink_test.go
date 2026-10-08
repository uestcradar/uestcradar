package orchestration

import (
	"context"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func (f *fakeRemote) SignalSink(context.Context, *Session, string, string, string) (SignalSinkStatus, error) {
	return SignalSinkStatus{OK: true, State: "idle", SampleContinuity: "unverified"}, nil
}

func TestSignalSinkContractAndPlan(t *testing.T) {
	labels := map[string]string{"io.uestcradar.contract": "worker/v2", "io.uestcradar.roles": "sink", "io.uestcradar.input": "any", "io.uestcradar.output": "none", "io.uestcradar.component": "signalsink"}
	contract, err := ParseWorkerContract(labels)
	if err != nil {
		t.Fatal(err)
	}
	delete(labels, "io.uestcradar.component")
	if _, err := ParseWorkerContract(labels); err == nil {
		t.Fatal("generic wildcard Worker accepted")
	}
	source := ImageInfo{Reference: workerRepository + "source", ID: "source", Architecture: "arm64", Contract: WorkerContract{Roles: []string{"source"}, Input: "none", Output: "3:2"}}
	sink := ImageInfo{Reference: workerRepository + "signalsink", ID: "sink", Architecture: "arm64", Contract: contract}
	first, last := inspectedNode("10.0.0.1", source), inspectedNode("10.0.0.2", sink)
	request := PlanRequest{Transport: "tcp", Chain: []ChainEntry{{IP: first.IP, WorkerImage: source.Reference}, {IP: last.IP, WorkerImage: sink.Reference}}}
	plan, err := BuildPlan(request, map[string]NodeInspection{first.IP: first, last.IP: last}, "10.0.0.99", time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if plan.Nodes[1].Input != "3:2" || !strings.Contains(plan.Nodes[1].env, "SIGNALSINK_INPUT=3:2") || !strings.Contains(plan.Nodes[1].compose, "/root/workspace/captures:/captures") {
		t.Fatal("missing concrete input or persistent mount")
	}
	if strings.Contains(plan.Nodes[0].compose, "/captures") || strings.Contains(plan.Nodes[0].env, "SIGNALSINK") {
		t.Fatal("changed ordinary Worker")
	}
	if strings.Contains(plan.Nodes[1].compose, "/dev/infiniband") {
		t.Fatal("explicit TCP diagnostic regression")
	}
	if !knownSignalSinkImage(last, sink.ID) || knownSignalSinkImage(last, "other") {
		t.Fatal("runtime identity validation")
	}
}

type sinkFailureRemote struct{ fakeRemote }

func (*sinkFailureRemote) SignalSink(context.Context, *Session, string, string, string) (SignalSinkStatus, error) {
	return SignalSinkStatus{}, fmt.Errorf("test remote unavailable")
}

func TestSignalSinkHTTPGuards(t *testing.T) {
	service := testService()
	session, err := service.sessions.Create(Credentials{Username: "root", Password: []byte("test")})
	if err != nil {
		t.Fatal(err)
	}
	ip := "192.162.2.16"
	session.Nodes[ip] = NodeInspection{IP: ip, Reachable: true, InspectedAt: time.Now()}
	service.discovered[ip] = session.Nodes[ip]
	session.TrustedKeys[ip] = "trusted"
	tests := []struct {
		name, method, path, body string
		login, csrf              bool
		want                     int
	}{
		{"status", "GET", "status?ip=" + ip, "", true, false, 200},
		{"no login", "GET", "status?ip=" + ip, "", false, false, 401},
		{"start", "POST", "start", `{"ip":"` + ip + `","directory":"run-a"}`, true, true, 200},
		{"no csrf", "POST", "start", `{"ip":"` + ip + `","directory":"run-a"}`, true, false, 403},
		{"escape", "POST", "start", `{"ip":"` + ip + `","directory":"../escape"}`, true, true, 400},
		{"injection", "POST", "start", `{"ip":"` + ip + `","directory":"a;id"}`, true, true, 400},
		{"unknown field", "POST", "start", `{"ip":"` + ip + `","directory":"a","command":"id"}`, true, true, 400},
		{"unknown node", "GET", "status?ip=8.8.8.8", "", true, false, 403},
		{"wrong method", "GET", "stop", "", true, false, 405},
		{"invalid id", "POST", "stop", `{"ip":"` + ip + `","recording_id":"bad"}`, true, true, 400},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			r := httptest.NewRequest(test.method, "https://controller/api/v1/orchestration/signalsink/"+test.path, strings.NewReader(test.body))
			if test.login {
				r.AddCookie(&http.Cookie{Name: sessionCookieName, Value: session.ID})
			}
			if test.csrf {
				r.Header.Set("X-CSRF-Token", session.CSRF)
			}
			r.Header.Set("Origin", "https://controller")
			w := httptest.NewRecorder()
			service.ServeHTTP(w, r)
			if w.Code != test.want {
				t.Fatalf("%d: %s", w.Code, w.Body.String())
			}
		})
	}
	service.remote = &sinkFailureRemote{}
	r := httptest.NewRequest("GET", "https://controller/api/v1/orchestration/signalsink/status?ip="+ip, nil)
	r.AddCookie(&http.Cookie{Name: sessionCookieName, Value: session.ID})
	w := httptest.NewRecorder()
	service.ServeHTTP(w, r)
	if w.Code != 502 || !strings.Contains(w.Body.String(), "unknown") {
		t.Fatal("remote failure reported as known state")
	}
}

func TestRecordingOutputBound(t *testing.T) {
	var output recordingOutput
	text := []byte(strings.Repeat("x", 70000))
	count, err := output.Write(text)
	if err != nil || count != len(text) || output.data.Len() != 65536 || !output.overflow {
		t.Fatal("unbounded remote output")
	}
}
