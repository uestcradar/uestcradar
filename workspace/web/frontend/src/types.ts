export interface WorkerContract { roles: string[]; input: string; output: string }
export interface ImageInfo { reference: string; id: string; architecture: string; contract: WorkerContract }
export interface RDMAInterface { device: string; port: string; netdev: string; ipv4: string; state: string; physical_state: string }
export interface NodeInspection {
  ip: string; reachable: boolean; hostname?: string; architecture?: string; docker_version?: string;
  compose_cli?: string; cpus?: string; memory_bytes?: string; docker_disk?: string;
  rdma: RDMAInterface[]; sidecar_image_id?: string; sidecar_contract?: string;
  workers: ImageInfo[]; existing_deployment: boolean; host_key_fingerprint?: string;
  host_key_required?: boolean; error?: string;
}
export interface ChainEntry { key: string; ip: string; rdma_device: string; worker_image: string }
export interface PlannedNode {
  ip: string; node_id: string; role: Role; rdma_device: string; netdev: string; rdma_ip: string;
  worker_reference: string; worker_image_id: string; sidecar_image_id: string;
  input: string; output: string; existing_deployment: boolean; env_preview: string;
}
export interface DeploymentPlan { id: string; created_at: string; nodes: PlannedNode[] }
export interface Task { id: string; kind: string; status: string; message?: string; current_ip?: string; completed?: string[] }
export type Role = 'source' | 'operator' | 'sink';

export interface RingSnapshot { capacity_slots: number; used_slots: number; write_position: number; read_position: number; watermark_pct: number; shutdown: boolean }
export interface LinkSnapshot { link_id: string; peer_node_id: string; direction: string; status: string; transport: string; goodput_gbps: number; ring: RingSnapshot; stale: boolean }
export interface TelemetryNode { node_id: string; status: string; last_seen: string; goodput_gbps: number; watermark_pct: number; links: LinkSnapshot[] }
export interface ClusterSnapshot { generated_at: string; nodes: TelemetryNode[] }
