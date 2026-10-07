package server

import (
	"math"
	"testing"
	"time"

	pb "uestcradar/telemetry/internal/telemetrypb"
)

func heartbeat(
	nodeID string,
	instanceID string,
	sequence uint64,
	usedSlots uint32,
	connection pb.LinkConnectionState,
	bytes uint64,
) *pb.NodeHeartbeat {
	return &pb.NodeHeartbeat{
		NodeId:     nodeID,
		InstanceId: instanceID,
		Sequence:   sequence,
		Links: []*pb.LinkMetric{
			{
				LinkId:            "upstream",
				PeerNodeId:        "node-a",
				Direction:         pb.LinkDirection_LINK_DIRECTION_INGRESS,
				ConnectionState:   connection,
				Transport:         pb.TransportKind_TRANSPORT_KIND_TCP,
				PayloadBytesTotal: bytes,
				Ring: &pb.RingBufferSnapshot{
					CapacitySlots: 10,
					UsedSlots:     usedSlots,
					WritePosition: uint64(100 + usedSlots),
					ReadPosition:  100,
				},
			},
		},
	}
}

func TestNodeStateDoesNotDependOnDisconnectedLeg(t *testing.T) {
	store := NewStore()
	seenAt := time.Date(2026, time.July, 31, 12, 0, 0, 0, time.UTC)
	store.UpdateHeartbeat(
		heartbeat(
			"node-b",
			"instance-1",
			1,
			7,
			pb.LinkConnectionState_LINK_CONNECTION_STATE_DISCONNECTED,
			0,
		),
		seenAt,
	)

	node := store.Snapshot(seenAt).Nodes[0]
	if node.Status != NodeNormal || node.Links[0].Status != LinkDisconnected {
		t.Fatalf("node/link status = %q/%q", node.Status, node.Links[0].Status)
	}

	store.UpdateHeartbeat(
		heartbeat(
			"node-b",
			"instance-1",
			2,
			8,
			pb.LinkConnectionState_LINK_CONNECTION_STATE_DISCONNECTED,
			0,
		),
		seenAt.Add(100*time.Millisecond),
	)
	node = store.Snapshot(seenAt).Nodes[0]
	if node.Status != NodeWarning || node.Links[0].Status != LinkDisconnected {
		t.Fatalf("warning/link status = %q/%q", node.Status, node.Links[0].Status)
	}
}

func TestHeartbeatLeaseExpiresStrictlyAfterTTLAndMarksLinksStale(t *testing.T) {
	store := NewStore()
	startedAt := time.Date(2026, time.July, 31, 12, 0, 0, 0, time.UTC)
	store.UpdateHeartbeat(
		heartbeat(
			"node-b",
			"instance-1",
			1,
			0,
			pb.LinkConnectionState_LINK_CONNECTION_STATE_CONNECTED,
			0,
		),
		startedAt,
	)

	if store.MarkOffline(startedAt.Add(3*time.Second), 3*time.Second) {
		t.Fatal("node expired at exact TTL")
	}
	if !store.MarkOffline(
		startedAt.Add(3*time.Second+time.Nanosecond),
		3*time.Second,
	) {
		t.Fatal("node did not expire after TTL")
	}
	node := store.Snapshot(startedAt.Add(4 * time.Second)).Nodes[0]
	if node.Status != NodeOffline || !node.Links[0].Stale ||
		node.Links[0].Status != LinkConnected {
		t.Fatalf("offline snapshot = %#v", node)
	}

	store.UpdateHeartbeat(
		heartbeat(
			"node-b",
			"instance-2",
			1,
			0,
			pb.LinkConnectionState_LINK_CONNECTION_STATE_CONNECTED,
			0,
		),
		startedAt.Add(5*time.Second),
	)
	node = store.Snapshot(startedAt.Add(5 * time.Second)).Nodes[0]
	if node.Status != NodeNormal || node.Links[0].Stale {
		t.Fatalf("recovered snapshot = %#v", node)
	}
}

func TestGoodputUsesCumulativeCounterAndResetsWithInstance(t *testing.T) {
	store := NewStore()
	startedAt := time.Date(2026, time.July, 31, 12, 0, 0, 0, time.UTC)
	store.UpdateHeartbeat(
		heartbeat(
			"node-b",
			"instance-1",
			1,
			0,
			pb.LinkConnectionState_LINK_CONNECTION_STATE_CONNECTED,
			1_000,
		),
		startedAt,
	)
	store.UpdateHeartbeat(
		heartbeat(
			"node-b",
			"instance-1",
			2,
			0,
			pb.LinkConnectionState_LINK_CONNECTION_STATE_CONNECTED,
			1_000_001_000,
		),
		startedAt.Add(time.Second),
	)
	got := store.Snapshot(startedAt).Nodes[0].Links[0].GoodputGBPS
	if math.Abs(got-1) > 0.000001 {
		t.Fatalf("goodput = %f, want 1 GB/s", got)
	}

	store.UpdateHeartbeat(
		heartbeat(
			"node-b",
			"instance-2",
			1,
			0,
			pb.LinkConnectionState_LINK_CONNECTION_STATE_CONNECTED,
			5,
		),
		startedAt.Add(2*time.Second),
	)
	if got := store.Snapshot(startedAt).Nodes[0].Links[0].GoodputGBPS; got != 0 {
		t.Fatalf("restart goodput = %f, want 0", got)
	}

	old := heartbeat(
		"node-b",
		"instance-1",
		3,
		0,
		pb.LinkConnectionState_LINK_CONNECTION_STATE_CONNECTED,
		2_000_001_000,
	)
	if store.UpdateHeartbeat(old, startedAt.Add(3*time.Second)) {
		t.Fatal("accepted delayed heartbeat from retired instance")
	}
	if got := store.Snapshot(startedAt).Nodes[0].InstanceID; got != "instance-2" {
		t.Fatalf("instance after delayed packet = %q", got)
	}
}

func TestRejectsMalformedAndOutOfOrderHeartbeat(t *testing.T) {
	store := NewStore()
	now := time.Now()
	if store.UpdateHeartbeat(nil, now) {
		t.Fatal("accepted nil heartbeat")
	}
	valid := heartbeat(
		"node-b",
		"instance-1",
		2,
		0,
		pb.LinkConnectionState_LINK_CONNECTION_STATE_CONNECTED,
		0,
	)
	if !store.UpdateHeartbeat(valid, now) {
		t.Fatal("rejected valid heartbeat")
	}
	valid.Sequence = 1
	if store.UpdateHeartbeat(valid, now.Add(time.Second)) {
		t.Fatal("accepted out-of-order heartbeat")
	}
}
