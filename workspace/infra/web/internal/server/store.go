package server

import (
	"sort"
	"sync"
	"time"

	pb "uestcradar/telemetry/internal/telemetrypb"
)

const warningWatermarkPct = 70.0

type NodeStatus string

const (
	NodeNormal  NodeStatus = "normal"
	NodeWarning NodeStatus = "warning"
	NodeOffline NodeStatus = "offline"
)

type LinkStatus string

const (
	LinkDisabled     LinkStatus = "disabled"
	LinkDisconnected LinkStatus = "disconnected"
	LinkConnected    LinkStatus = "connected"
)

type RingSnapshot struct {
	CapacitySlots uint32  `json:"capacity_slots"`
	UsedSlots     uint32  `json:"used_slots"`
	WritePosition uint64  `json:"write_position"`
	ReadPosition  uint64  `json:"read_position"`
	WatermarkPct  float64 `json:"watermark_pct"`
	Shutdown      bool    `json:"shutdown"`
}

type LinkSnapshot struct {
	LinkID            string       `json:"link_id"`
	PeerNodeID        string       `json:"peer_node_id"`
	Direction         string       `json:"direction"`
	Status            LinkStatus   `json:"status"`
	Transport         string       `json:"transport"`
	PayloadBytesTotal uint64       `json:"payload_bytes_total"`
	GoodputGBPS       float64      `json:"goodput_gbps"`
	Ring              RingSnapshot `json:"ring"`
	Stale             bool         `json:"stale"`
}

type NodeSnapshot struct {
	NodeID       string         `json:"node_id"`
	InstanceID   string         `json:"instance_id"`
	Status       NodeStatus     `json:"status"`
	LastSeen     time.Time      `json:"last_seen"`
	GoodputGBPS  float64        `json:"goodput_gbps"`
	WatermarkPct float64        `json:"watermark_pct"`
	Links        []LinkSnapshot `json:"links"`
}

type ClusterSnapshot struct {
	GeneratedAt time.Time      `json:"generated_at"`
	Nodes       []NodeSnapshot `json:"nodes"`
}

type linkRecord struct {
	snapshot        LinkSnapshot
	previousBytes   uint64
	previousAt      time.Time
	hasRateBaseline bool
}

type nodeRecord struct {
	instanceID       string
	retiredInstances map[string]struct{}
	sequence         uint64
	lastSeen         time.Time
	status           NodeStatus
	links            map[string]*linkRecord
}

type Store struct {
	mu    sync.RWMutex
	nodes map[string]*nodeRecord
}

func NewStore() *Store {
	return &Store{nodes: make(map[string]*nodeRecord)}
}

func (s *Store) UpdateHeartbeat(
	heartbeat *pb.NodeHeartbeat,
	receivedAt time.Time,
) bool {
	if heartbeat == nil || heartbeat.NodeId == "" ||
		heartbeat.InstanceId == "" || heartbeat.Sequence == 0 {
		return false
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	node := s.nodes[heartbeat.NodeId]
	if node == nil {
		node = &nodeRecord{
			instanceID:       heartbeat.InstanceId,
			retiredInstances: make(map[string]struct{}),
			links:            make(map[string]*linkRecord),
		}
		s.nodes[heartbeat.NodeId] = node
	} else if node.instanceID != heartbeat.InstanceId {
		if _, retired := node.retiredInstances[heartbeat.InstanceId]; retired {
			return false
		}
		node.retiredInstances[node.instanceID] = struct{}{}
		node.instanceID = heartbeat.InstanceId
		node.sequence = 0
		node.links = make(map[string]*linkRecord)
	}
	if heartbeat.Sequence <= node.sequence {
		return false
	}

	node.sequence = heartbeat.Sequence
	node.lastSeen = receivedAt
	seenLinks := make(map[string]struct{}, len(heartbeat.Links))
	maximumWatermark := 0.0
	for _, metric := range heartbeat.Links {
		if metric == nil || metric.LinkId == "" || metric.Ring == nil {
			continue
		}
		seenLinks[metric.LinkId] = struct{}{}
		record := node.links[metric.LinkId]
		if record == nil {
			record = &linkRecord{}
			node.links[metric.LinkId] = record
		}

		watermark := watermarkPct(
			metric.Ring.UsedSlots,
			metric.Ring.CapacitySlots,
		)
		goodput := 0.0
		if record.hasRateBaseline &&
			metric.PayloadBytesTotal >= record.previousBytes {
			seconds := receivedAt.Sub(record.previousAt).Seconds()
			if seconds > 0 {
				goodput = float64(
					metric.PayloadBytesTotal-record.previousBytes,
				) / seconds / 1_000_000_000
			}
		}
		record.previousBytes = metric.PayloadBytesTotal
		record.previousAt = receivedAt
		record.hasRateBaseline = true
		record.snapshot = LinkSnapshot{
			LinkID:            metric.LinkId,
			PeerNodeID:        metric.PeerNodeId,
			Direction:         directionName(metric.Direction),
			Status:            linkStatus(metric.ConnectionState),
			Transport:         transportName(metric.Transport),
			PayloadBytesTotal: metric.PayloadBytesTotal,
			GoodputGBPS:       goodput,
			Ring: RingSnapshot{
				CapacitySlots: metric.Ring.CapacitySlots,
				UsedSlots:     metric.Ring.UsedSlots,
				WritePosition: metric.Ring.WritePosition,
				ReadPosition:  metric.Ring.ReadPosition,
				WatermarkPct:  watermark,
				Shutdown:      metric.Ring.Shutdown,
			},
		}
		if watermark > maximumWatermark {
			maximumWatermark = watermark
		}
	}
	for linkID := range node.links {
		if _, ok := seenLinks[linkID]; !ok {
			delete(node.links, linkID)
		}
	}
	if maximumWatermark > warningWatermarkPct {
		node.status = NodeWarning
	} else {
		node.status = NodeNormal
	}
	return true
}

func (s *Store) MarkOffline(now time.Time, ttl time.Duration) bool {
	s.mu.Lock()
	defer s.mu.Unlock()

	changed := false
	for _, node := range s.nodes {
		if now.Sub(node.lastSeen) > ttl && node.status != NodeOffline {
			node.status = NodeOffline
			changed = true
		}
	}
	return changed
}

func (s *Store) Snapshot(now time.Time) ClusterSnapshot {
	s.mu.RLock()
	defer s.mu.RUnlock()

	result := ClusterSnapshot{
		GeneratedAt: now,
		Nodes:       make([]NodeSnapshot, 0, len(s.nodes)),
	}
	for nodeID, node := range s.nodes {
		links := make([]LinkSnapshot, 0, len(node.links))
		goodput := 0.0
		maximumWatermark := 0.0
		for _, record := range node.links {
			link := record.snapshot
			link.Stale = node.status == NodeOffline
			links = append(links, link)
			goodput += link.GoodputGBPS
			if link.Ring.WatermarkPct > maximumWatermark {
				maximumWatermark = link.Ring.WatermarkPct
			}
		}
		sort.Slice(links, func(i, j int) bool {
			return links[i].LinkID < links[j].LinkID
		})
		result.Nodes = append(result.Nodes, NodeSnapshot{
			NodeID:       nodeID,
			InstanceID:   node.instanceID,
			Status:       node.status,
			LastSeen:     node.lastSeen,
			GoodputGBPS:  goodput,
			WatermarkPct: maximumWatermark,
			Links:        links,
		})
	}
	sort.Slice(result.Nodes, func(i, j int) bool {
		return result.Nodes[i].NodeID < result.Nodes[j].NodeID
	})
	return result
}

func watermarkPct(used uint32, capacity uint32) float64 {
	if capacity == 0 {
		return 0
	}
	return float64(used) * 100 / float64(capacity)
}

func directionName(direction pb.LinkDirection) string {
	switch direction {
	case pb.LinkDirection_LINK_DIRECTION_INGRESS:
		return "ingress"
	case pb.LinkDirection_LINK_DIRECTION_EGRESS:
		return "egress"
	default:
		return "unknown"
	}
}

func linkStatus(state pb.LinkConnectionState) LinkStatus {
	switch state {
	case pb.LinkConnectionState_LINK_CONNECTION_STATE_DISABLED:
		return LinkDisabled
	case pb.LinkConnectionState_LINK_CONNECTION_STATE_CONNECTED:
		return LinkConnected
	default:
		return LinkDisconnected
	}
}

func transportName(transport pb.TransportKind) string {
	switch transport {
	case pb.TransportKind_TRANSPORT_KIND_TCP:
		return "tcp"
	case pb.TransportKind_TRANSPORT_KIND_RDMA:
		return "rdma"
	default:
		return "unknown"
	}
}
