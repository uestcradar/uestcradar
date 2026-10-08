package orchestration

import (
	"context"
	"net"
	"sort"
	"sync"
	"time"
)

type DialContextFunc func(context.Context, string, string) (net.Conn, error)

func Discover(ctx context.Context, ips []string, dial DialContextFunc) []NodeInspection {
	results := make([]NodeInspection, len(ips))
	var group sync.WaitGroup
	for index, ip := range ips {
		group.Add(1)
		go func(index int, ip string) {
			defer group.Done()
			probeContext, cancel := context.WithTimeout(ctx, 800*time.Millisecond)
			defer cancel()
			connection, err := dial(probeContext, "tcp", net.JoinHostPort(ip, "22"))
			results[index] = NodeInspection{IP: ip, Reachable: err == nil}
			if connection != nil {
				_ = connection.Close()
			}
		}(index, ip)
	}
	group.Wait()
	sort.Slice(results, func(i, j int) bool { return results[i].IP < results[j].IP })
	return results
}

func defaultDial(ctx context.Context, network, address string) (net.Conn, error) {
	return (&net.Dialer{}).DialContext(ctx, network, address)
}
