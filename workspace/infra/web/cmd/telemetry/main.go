package main

import (
	"context"
	"fmt"
	"os"
	"os/signal"
	"syscall"

	"uestcradar/telemetry/internal/server"
)

func main() {
	ctx, stop := signal.NotifyContext(
		context.Background(),
		syscall.SIGINT,
		syscall.SIGTERM,
	)
	defer stop()

	if err := server.Run(ctx, server.ConfigFromEnv()); err != nil {
		fmt.Fprintf(os.Stderr, "telemetry: %v\n", err)
		os.Exit(1)
	}
}
