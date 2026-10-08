package main

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"uestcradar/frontend/internal/server"
)

func main() {
	config, err := server.ConfigFromEnv()
	if err == nil {
		if len(os.Args) == 2 && os.Args[1] == "--healthcheck" {
			host, port, _ := net.SplitHostPort(config.HTTPAddress)
			if host == "" || host == "0.0.0.0" || host == "::" {
				host = "127.0.0.1"
			}
			scheme := "http"
			if config.TLSCertFile != "" {
				scheme = "https"
			}
			client := &http.Client{Timeout: 2 * time.Second}
			var response *http.Response
			response, err = client.Get(scheme + "://" + net.JoinHostPort(host, port) + "/healthz")
			if err == nil {
				response.Body.Close()
				if response.StatusCode != http.StatusOK {
					err = fmt.Errorf("health status: %s", response.Status)
				}
			}
		} else if len(os.Args) != 1 {
			err = fmt.Errorf("usage: frontend [--healthcheck]")
		} else {
			ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
			defer cancel()
			err = server.Run(ctx, config)
		}
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
