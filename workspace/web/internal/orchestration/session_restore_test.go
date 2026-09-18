package orchestration

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestRestoreSession(t *testing.T) {
	service := testService()
	session, err := service.sessions.Create(Credentials{Username: "root", Password: []byte("private-secret")})
	if err != nil {
		t.Fatal(err)
	}
	defer service.sessions.Delete(session.ID)
	session.CustomIPs = []string{"10.0.0.1"}
	session.Nodes["10.0.0.1"] = NodeInspection{IP: "10.0.0.1", Hostname: "saved-node"}
	request := httptest.NewRequest(http.MethodGet, "https://controller/api/v1/session", nil)
	request.AddCookie(&http.Cookie{Name: sessionCookieName, Value: session.ID})
	response := httptest.NewRecorder()
	service.ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("restore: %d %s", response.Code, response.Body.String())
	}
	var body map[string]any
	if err := json.Unmarshal(response.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	if len(body) != 3 || body["csrf_token"] != session.CSRF || body["username"] != "root" || body["expires_at"] == nil {
		t.Fatalf("unexpected response: %v", body)
	}
	if strings.Contains(response.Body.String(), "private-secret") {
		t.Fatal("credentials leaked")
	}
	if response.Header().Get("Cache-Control") != "no-store" {
		t.Fatal("session response must not be cached")
	}
	restored, ok := service.sessions.Get(session.ID)
	if !ok || restored != session || len(restored.CustomIPs) != 1 || restored.Nodes["10.0.0.1"].Hostname != "saved-node" {
		t.Fatal("restore replaced session or lost nodes")
	}
	mutation := httptest.NewRequest(http.MethodPost, "https://controller/api/v1/orchestration/nodes", strings.NewReader(`{"ip":"10.0.0.2"}`))
	mutation.AddCookie(&http.Cookie{Name: sessionCookieName, Value: session.ID})
	mutation.Header.Set("Origin", "https://controller")
	mutation.Header.Set("X-CSRF-Token", body["csrf_token"].(string))
	result := httptest.NewRecorder()
	service.ServeHTTP(result, mutation)
	if result.Code != http.StatusCreated {
		t.Fatalf("restored CSRF mutation: %d %s", result.Code, result.Body.String())
	}
}

func TestRestoreSessionUnauthorized(t *testing.T) {
	for _, kind := range []string{"missing", "unknown", "expired"} {
		t.Run(kind, func(t *testing.T) {
			service := testService()
			request := httptest.NewRequest(http.MethodGet, "https://controller/api/v1/session", nil)
			if kind == "unknown" {
				request.AddCookie(&http.Cookie{Name: sessionCookieName, Value: "unknown"})
			}
			if kind == "expired" {
				session, _ := service.sessions.Create(Credentials{Username: "root", Password: []byte("secret")})
				defer service.sessions.Delete(session.ID)
				service.sessions.now = func() time.Time { return time.Now().Add(sessionTTL + time.Minute) }
				request.AddCookie(&http.Cookie{Name: sessionCookieName, Value: session.ID})
			}
			response := httptest.NewRecorder()
			service.ServeHTTP(response, request)
			if response.Code != http.StatusUnauthorized {
				t.Fatalf("got %d", response.Code)
			}
		})
	}
}
