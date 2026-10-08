package orchestration

import (
	"context"
	"crypto/ed25519"
	"crypto/rand"
	"encoding/json"
	"fmt"
	"net"
	"strings"
	"sync"
	"testing"
	"time"

	"golang.org/x/crypto/ssh"
)

func TestSignalSinkSSHIdentityAndCancellation(t *testing.T) {
	for _, scenario := range []string{"status", "start", "stop", "wrong-image", "hang", "oversize"} {
		t.Run(scenario, func(t *testing.T) {
			_, key, err := ed25519.GenerateKey(rand.Reader)
			if err != nil {
				t.Fatal(err)
			}
			signer, err := ssh.NewSignerFromKey(key)
			if err != nil {
				t.Fatal(err)
			}
			config := &ssh.ServerConfig{NoClientAuth: true}
			config.AddHostKey(signer)
			listener, err := net.Listen("tcp", "127.0.0.1:0")
			if err != nil {
				t.Fatal(err)
			}
			defer listener.Close()
			var group sync.WaitGroup
			group.Add(1)
			id := strings.Repeat("a", 64)
			image := "sha256:" + strings.Repeat("b", 64)
			var commands []string
			go func() {
				defer group.Done()
				connection, err := listener.Accept()
				if err != nil {
					return
				}
				defer connection.Close()
				server, channels, requests, err := ssh.NewServerConn(connection, config)
				if err != nil {
					return
				}
				defer server.Close()
				go ssh.DiscardRequests(requests)
				for incoming := range channels {
					channel, requests, err := incoming.Accept()
					if err != nil {
						return
					}
					for request := range requests {
						if request.Type != "exec" {
							request.Reply(false, nil)
							continue
						}
						var execution struct{ Command string }
						if ssh.Unmarshal(request.Payload, &execution) != nil {
							channel.Close()
							break
						}
						commands = append(commands, execution.Command)
						request.Reply(true, nil)
						switch {
						case strings.HasPrefix(execution.Command, "docker ps "):
							fmt.Fprintln(channel, id)
						case strings.HasPrefix(execution.Command, "docker inspect "):
							actual := image
							if scenario == "wrong-image" {
								actual = "sha256:" + strings.Repeat("c", 64)
							}
							fmt.Fprintf(channel, `[{"Id":%q,"Image":%q,"State":{"Running":true},"Config":{"Entrypoint":["/app/signalsink"],"Labels":{"io.uestcradar.contract":"worker/v2","io.uestcradar.component":"signalsink","io.uestcradar.roles":"sink","io.uestcradar.input":"any","io.uestcradar.output":"none"}}}]`, id, actual)
						default:
							if scenario == "hang" {
								for range requests {
								}
								channel.Close()
								return
							}
							if scenario == "oversize" {
								fmt.Fprint(channel, strings.Repeat("x", 70000))
								break
							}
							status := SignalSinkStatus{OK: true, State: "recording", RecordingID: strings.Repeat("d", 32), Directory: "run-a", SampleContinuity: "unverified", AcceptedFrames: "1", WrittenFrames: "1", WrittenBytes: "77", QueueUsedBytes: "0", QueueCapacityBytes: "1024", ElapsedMS: "50"}
							encoded, _ := json.Marshal(status)
							channel.Write(encoded)
						}
						channel.SendRequest("exit-status", false, ssh.Marshal(struct{ Status uint32 }{0}))
						channel.Close()
						break
					}
				}
			}()
			session := &Session{Credentials: Credentials{Username: "test", Password: []byte("fixture")}, TrustedKeys: map[string]string{"127.0.0.1": ssh.FingerprintSHA256(signer.PublicKey())}, Nodes: map[string]NodeInspection{}}
			session.Nodes["127.0.0.1"] = NodeInspection{Workers: []ImageInfo{{ID: image, DigestReference: "registry.chengyistudio.com/cxx/worker@sha256:" + strings.Repeat("b", 64), Contract: WorkerContract{Component: "signalsink", Input: "any", Output: "none"}}}}
			operation, argument := scenario, ""
			if operation == "start" {
				argument = "run-a"
			}
			if operation == "stop" {
				argument = strings.Repeat("d", 32)
			}
			if operation != "start" && operation != "stop" {
				operation = "status"
			}
			ctx, cancel := context.WithTimeout(context.Background(), 500*time.Millisecond)
			result, err := NewSSHBackend().signalSinkAt(ctx, session, "127.0.0.1", listener.Addr().String(), operation, argument)
			cancel()
			listener.Close()
			group.Wait()
			if scenario == "wrong-image" || scenario == "hang" || scenario == "oversize" {
				if err == nil {
					t.Fatalf("accepted %s: %#v", scenario, result)
				}
				if scenario == "wrong-image" && len(commands) != 2 {
					t.Fatal("executed control on wrong runtime image")
				}
			} else {
				if err != nil {
					t.Fatal(err)
				}
				expected := "docker exec '" + id + "' /app/signalsink control " + operation
				if operation == "start" {
					expected += " --directory 'run-a'"
				}
				if operation == "stop" {
					expected += " --recording-id '" + argument + "'"
				}
				if len(commands) != 3 || commands[2] != expected {
					t.Fatalf("unexpected command: %q", commands)
				}
			}
		})
	}
}
