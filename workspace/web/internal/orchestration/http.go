package orchestration

import (
	"context"
	"encoding/json"
	"errors"
	"net"
	"net/http"
	"net/url"
	"regexp"
	"sort"
	"strings"
	"sync"
	"time"
)

const sessionCookieName = "uestcradar_session"

var usernamePattern = regexp.MustCompile(`^[A-Za-z_][A-Za-z0-9_.-]{0,63}$`)

type Service struct {
	sessions      *SessionStore
	remote        RemoteBackend
	advertiseHost string
	secureCookies bool
	mu            sync.RWMutex
	discovered    map[string]NodeInspection
}

func NewService(advertiseHost string, secureCookies bool) *Service {
	service := &Service{sessions: NewSessionStore(), remote: NewSSHBackend(), advertiseHost: advertiseHost, secureCookies: secureCookies, discovered: map[string]NodeInspection{}}
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	for _, node := range Discover(ctx, DefaultNodeIPs, defaultDial) {
		service.discovered[node.IP] = node
	}
	return service
}

func (s *Service) ServeHTTP(writer http.ResponseWriter, request *http.Request) {
	writer.Header().Set("Cache-Control", "no-store")
	path := strings.TrimSuffix(request.URL.Path, "/")
	if path == "/api/v1/session" {
		s.handleSession(writer, request)
		return
	}
	session, ok := s.authorize(writer, request)
	if !ok {
		return
	}
	switch {
	case path == "/api/v1/orchestration/nodes" && request.Method == http.MethodGet:
		s.handleNodes(writer, session)
	case path == "/api/v1/orchestration/nodes" && request.Method == http.MethodPost:
		s.handleAddNode(writer, request, session)
	case path == "/api/v1/orchestration/inspect" && request.Method == http.MethodPost:
		s.handleInspect(writer, request, session)
	case path == "/api/v1/orchestration/host-keys/confirm" && request.Method == http.MethodPost:
		s.handleConfirmHostKey(writer, request, session)
	case path == "/api/v1/orchestration/images/sync" && request.Method == http.MethodPost:
		s.handleImageSync(writer, request, session)
	case path == "/api/v1/orchestration/plans/preview" && request.Method == http.MethodPost:
		s.handlePreview(writer, request, session)
	case path == "/api/v1/orchestration/deployments" && request.Method == http.MethodPost:
		s.handleDeployment(writer, request, session)
	case strings.HasPrefix(path, "/api/v1/orchestration/tasks/") && request.Method == http.MethodGet:
		s.handleTask(writer, session, strings.TrimPrefix(path, "/api/v1/orchestration/tasks/"))
	default:
		http.Error(writer, "not found", http.StatusNotFound)
	}
}

type sessionRequest struct {
	Username   string `json:"username"`
	Password   string `json:"password"`
	PrivateKey string `json:"private_key"`
	Passphrase string `json:"passphrase"`
}

func (s *Service) handleSession(writer http.ResponseWriter, request *http.Request) {
	if request.Method == http.MethodDelete {
		cookie, err := request.Cookie(sessionCookieName)
		if err != nil {
			http.Error(writer, "SSH session required", http.StatusUnauthorized)
			return
		}
		session, ok := s.sessions.Get(cookie.Value)
		if !ok || request.Header.Get("X-CSRF-Token") != session.CSRF || !sameOrigin(request) {
			http.Error(writer, "CSRF validation failed", http.StatusForbidden)
			return
		}
		s.sessions.Delete(cookie.Value)
		http.SetCookie(writer, &http.Cookie{Name: sessionCookieName, Value: "", Path: "/", MaxAge: -1, HttpOnly: true, Secure: s.secureCookies, SameSite: http.SameSiteStrictMode})
		writer.WriteHeader(http.StatusNoContent)
		return
	}
	if request.Method != http.MethodPost {
		http.Error(writer, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	var body sessionRequest
	if err := decodeJSON(writer, request, &body); err != nil {
		return
	}
	if !usernamePattern.MatchString(body.Username) || (body.Password == "" && body.PrivateKey == "") || (body.Password != "" && body.PrivateKey != "") {
		http.Error(writer, "provide a valid username and exactly one authentication method", http.StatusBadRequest)
		return
	}
	session, err := s.sessions.Create(Credentials{Username: body.Username, Password: []byte(body.Password), PrivateKey: []byte(body.PrivateKey), Passphrase: []byte(body.Passphrase)})
	if err != nil {
		http.Error(writer, "create session", http.StatusInternalServerError)
		return
	}
	http.SetCookie(writer, &http.Cookie{Name: sessionCookieName, Value: session.ID, Path: "/", HttpOnly: true, Secure: s.secureCookies, SameSite: http.SameSiteStrictMode, MaxAge: int(sessionTTL.Seconds())})
	writeJSON(writer, http.StatusCreated, map[string]any{"csrf_token": session.CSRF, "expires_at": session.ExpiresAt})
}

func (s *Service) authorize(writer http.ResponseWriter, request *http.Request) (*Session, bool) {
	cookie, err := request.Cookie(sessionCookieName)
	if err != nil {
		http.Error(writer, "SSH session required", http.StatusUnauthorized)
		return nil, false
	}
	session, ok := s.sessions.Get(cookie.Value)
	if !ok {
		http.Error(writer, "SSH session expired", http.StatusUnauthorized)
		return nil, false
	}
	if request.Method != http.MethodGet {
		if request.Header.Get("X-CSRF-Token") != session.CSRF || !sameOrigin(request) {
			http.Error(writer, "CSRF validation failed", http.StatusForbidden)
			return nil, false
		}
	}
	return session, true
}

func sameOrigin(request *http.Request) bool {
	origin := request.Header.Get("Origin")
	if origin == "" {
		return true
	}
	parsed, err := url.Parse(origin)
	expectedScheme := "http"
	if request.TLS != nil {
		expectedScheme = "https"
	}
	return err == nil && parsed.Host == request.Host && parsed.Scheme == expectedScheme
}

func (s *Service) handleNodes(writer http.ResponseWriter, session *Session) {
	s.mu.RLock()
	result := make([]NodeInspection, 0, len(s.discovered)+len(session.CustomIPs))
	for _, node := range s.discovered {
		result = append(result, node)
	}
	s.mu.RUnlock()
	session.mu.Lock()
	for _, ip := range session.CustomIPs {
		if node, ok := session.Nodes[ip]; ok {
			result = append(result, node)
		} else {
			result = append(result, NodeInspection{IP: ip})
		}
	}
	for index := range result {
		if inspected, ok := session.Nodes[result[index].IP]; ok {
			result[index] = inspected
		}
	}
	session.mu.Unlock()
	sort.Slice(result, func(i, j int) bool { return result[i].IP < result[j].IP })
	writeJSON(writer, http.StatusOK, result)
}

func (s *Service) handleAddNode(writer http.ResponseWriter, request *http.Request, session *Session) {
	var body struct {
		IP string `json:"ip"`
	}
	if err := decodeJSON(writer, request, &body); err != nil {
		return
	}
	parsed := net.ParseIP(body.IP)
	if parsed == nil || parsed.To4() == nil {
		http.Error(writer, "custom node must be an IPv4 address", http.StatusBadRequest)
		return
	}
	s.mu.RLock()
	defaultNode, isDefault := s.discovered[body.IP]
	s.mu.RUnlock()
	if isDefault {
		writeJSON(writer, http.StatusOK, defaultNode)
		return
	}
	session.mu.Lock()
	for _, ip := range session.CustomIPs {
		if ip == body.IP {
			session.mu.Unlock()
			writeJSON(writer, http.StatusOK, map[string]string{"ip": body.IP})
			return
		}
	}
	session.CustomIPs = append(session.CustomIPs, body.IP)
	session.mu.Unlock()
	ctx, cancel := context.WithTimeout(request.Context(), time.Second)
	defer cancel()
	probe := Discover(ctx, []string{body.IP}, defaultDial)[0]
	session.mu.Lock()
	session.Nodes[body.IP] = probe
	session.mu.Unlock()
	writeJSON(writer, http.StatusCreated, probe)
}

func (s *Service) handleInspect(writer http.ResponseWriter, request *http.Request, session *Session) {
	var body struct {
		IPs []string `json:"ips"`
	}
	if err := decodeJSON(writer, request, &body); err != nil {
		return
	}
	if len(body.IPs) == 0 || len(body.IPs) > 32 {
		http.Error(writer, "inspect 1 to 32 nodes", http.StatusBadRequest)
		return
	}
	allowed := make(map[string]bool, len(s.discovered)+len(session.CustomIPs))
	s.mu.RLock()
	for ip := range s.discovered {
		allowed[ip] = true
	}
	s.mu.RUnlock()
	session.mu.Lock()
	for _, ip := range session.CustomIPs {
		allowed[ip] = true
	}
	session.mu.Unlock()
	seen := make(map[string]bool, len(body.IPs))
	results := make([]NodeInspection, len(body.IPs))
	var group sync.WaitGroup
	for index, ip := range body.IPs {
		parsed := net.ParseIP(ip)
		if parsed == nil || parsed.To4() == nil || !allowed[ip] || seen[ip] {
			http.Error(writer, "inspect requires unique discovered or custom IPv4 nodes", http.StatusBadRequest)
			return
		}
		seen[ip] = true
		group.Add(1)
		go func(index int, ip string) {
			defer group.Done()
			inspection, err := s.remote.Inspect(session, ip)
			if err != nil {
				var hostKey *HostKeyError
				if errors.As(err, &hostKey) {
					inspection.HostKeyRequired = true
					inspection.HostKeyFingerprint = hostKey.Fingerprint
				} else {
					inspection.Error = err.Error()
				}
			}
			results[index] = inspection
		}(index, ip)
	}
	group.Wait()
	session.mu.Lock()
	for _, result := range results {
		session.Nodes[result.IP] = result
	}
	session.mu.Unlock()
	writeJSON(writer, http.StatusOK, results)
}

func (s *Service) handleConfirmHostKey(writer http.ResponseWriter, request *http.Request, session *Session) {
	var body struct{ IP, Fingerprint string }
	if err := decodeJSON(writer, request, &body); err != nil {
		return
	}
	session.mu.Lock()
	node, ok := session.Nodes[body.IP]
	if !ok || !node.HostKeyRequired || node.HostKeyFingerprint != body.Fingerprint {
		session.mu.Unlock()
		http.Error(writer, "host key does not match observed fingerprint", http.StatusConflict)
		return
	}
	session.TrustedKeys[body.IP] = body.Fingerprint
	session.mu.Unlock()
	writer.WriteHeader(http.StatusNoContent)
}

func (s *Service) handleImageSync(writer http.ResponseWriter, request *http.Request, session *Session) {
	var body struct{ IP, Image string }
	if err := decodeJSON(writer, request, &body); err != nil {
		return
	}
	session.mu.Lock()
	node, ok := session.Nodes[body.IP]
	_, local := findWorker(node.Workers, body.Image)
	session.mu.Unlock()
	if !ok || !local || !safeImageReference.MatchString(body.Image) {
		http.Error(writer, "only an inspected local Worker tag may be synchronized", http.StatusBadRequest)
		return
	}
	task := s.newTask(session, "image-sync")
	go func() {
		s.updateTask(session, task.ID, "running", body.IP, "pulling Worker image", nil)
		if err := s.remote.PullWorker(session, body.IP, body.Image); err != nil {
			s.updateTask(session, task.ID, "failed", body.IP, err.Error(), nil)
			return
		}
		inspection, err := s.remote.Inspect(session, body.IP)
		if err != nil {
			s.updateTask(session, task.ID, "failed", body.IP, err.Error(), nil)
			return
		}
		session.mu.Lock()
		session.Nodes[body.IP] = inspection
		session.mu.Unlock()
		s.updateTask(session, task.ID, "completed", body.IP, "image synchronized", []string{body.IP})
	}()
	writeJSON(writer, http.StatusAccepted, task)
}

func (s *Service) handlePreview(writer http.ResponseWriter, request *http.Request, session *Session) {
	var body PlanRequest
	if err := decodeJSON(writer, request, &body); err != nil {
		return
	}
	session.mu.Lock()
	nodes := make(map[string]NodeInspection, len(session.Nodes))
	for key, value := range session.Nodes {
		nodes[key] = value
	}
	session.mu.Unlock()
	plan, err := BuildPlan(body, nodes, s.advertiseHost, time.Now())
	if err != nil {
		http.Error(writer, err.Error(), http.StatusUnprocessableEntity)
		return
	}
	session.mu.Lock()
	session.Plans[plan.ID] = plan
	session.mu.Unlock()
	writeJSON(writer, http.StatusOK, plan)
}

func (s *Service) handleDeployment(writer http.ResponseWriter, request *http.Request, session *Session) {
	var body struct {
		PlanID         string `json:"plan_id"`
		ConfirmReplace bool   `json:"confirm_replace"`
	}
	if err := decodeJSON(writer, request, &body); err != nil {
		return
	}
	session.mu.Lock()
	plan, ok := session.Plans[body.PlanID]
	for _, running := range session.Tasks {
		if running.Kind == "deployment" && (running.Status == "pending" || running.Status == "running") {
			session.mu.Unlock()
			http.Error(writer, "another deployment is already running", http.StatusConflict)
			return
		}
	}
	session.mu.Unlock()
	if !ok {
		http.Error(writer, "deployment plan not found", http.StatusNotFound)
		return
	}
	for _, node := range plan.Nodes {
		if node.ExistingDeployment && !body.ConfirmReplace {
			http.Error(writer, "existing deployment requires confirmation", http.StatusConflict)
			return
		}
	}
	task := s.newTask(session, "deployment")
	go s.deploy(session, plan, body.ConfirmReplace, task.ID)
	writeJSON(writer, http.StatusAccepted, task)
}

func (s *Service) deploy(session *Session, plan DeploymentPlan, confirmed bool, taskID string) {
	s.updateTask(session, taskID, "running", "", "uploading and validating configuration", nil)
	for _, node := range plan.Nodes {
		existing, err := s.remote.UploadAndValidate(session, node)
		if err != nil {
			s.updateTask(session, taskID, "failed", node.IP, err.Error(), nil)
			return
		}
		if existing && !confirmed {
			s.updateTask(session, taskID, "failed", node.IP, "existing deployment requires confirmation", nil)
			return
		}
	}
	completed := []string{}
	for index := len(plan.Nodes) - 1; index >= 0; index-- {
		node := plan.Nodes[index]
		s.updateTask(session, taskID, "running", node.IP, "starting "+node.Role, completed)
		if err := s.remote.Start(session, node); err != nil {
			s.updateTask(session, taskID, "failed", node.IP, err.Error(), completed)
			return
		}
		completed = append(completed, node.IP)
	}
	s.updateTask(session, taskID, "completed", "", "deployment completed", completed)
}

func (s *Service) newTask(session *Session, kind string) Task {
	id, _ := randomToken()
	now := time.Now()
	task := &Task{ID: id, Kind: kind, Status: "pending", CreatedAt: now, UpdatedAt: now}
	session.mu.Lock()
	session.Tasks[id] = task
	session.mu.Unlock()
	return *task
}

func (s *Service) updateTask(session *Session, id, status, ip, message string, completed []string) {
	session.mu.Lock()
	defer session.mu.Unlock()
	task := session.Tasks[id]
	if task == nil {
		return
	}
	task.Status, task.CurrentIP, task.Message, task.UpdatedAt = status, ip, message, time.Now()
	if completed != nil {
		task.Completed = append([]string(nil), completed...)
	}
}

func (s *Service) handleTask(writer http.ResponseWriter, session *Session, id string) {
	session.mu.Lock()
	task, ok := session.Tasks[id]
	var copy Task
	if ok {
		copy = *task
		copy.Completed = append([]string(nil), task.Completed...)
	}
	session.mu.Unlock()
	if !ok {
		http.Error(writer, "task not found", http.StatusNotFound)
		return
	}
	writeJSON(writer, http.StatusOK, copy)
}

func decodeJSON(writer http.ResponseWriter, request *http.Request, destination any) error {
	request.Body = http.MaxBytesReader(writer, request.Body, 1024*1024)
	decoder := json.NewDecoder(request.Body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(destination); err != nil {
		http.Error(writer, "invalid JSON", http.StatusBadRequest)
		return err
	}
	return nil
}

func writeJSON(writer http.ResponseWriter, status int, value any) {
	writer.Header().Set("Content-Type", "application/json")
	writer.WriteHeader(status)
	_ = json.NewEncoder(writer).Encode(value)
}
