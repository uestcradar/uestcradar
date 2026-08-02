#!/usr/bin/env bash
set -euo pipefail

cascade_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
compose_file="$cascade_root/workspace/sidecar/tools/compose.cascade.yaml"
project="${COMPOSE_PROJECT_NAME:-uestcradar-cascade}"
mode="${1:-}"

port_base=$((30000 + ($$ % 10000) * 2))
port_cursor="$port_base"
cascade_ab_override="${CASCADE_AB_PORT:-}"
cascade_bc_override="${CASCADE_BC_PORT:-}"

if [[ "$mode" != "correctness" && "$mode" != "benchmark" ]]; then
    echo "usage: $0 correctness|benchmark" >&2
    exit 2
fi

compose=(docker compose --project-name "$project" -f "$compose_file")

cleanup() {
    "${compose[@]}" down --volumes --remove-orphans >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

wait_for_connections() {
    local deadline=$((SECONDS + 60))
    while (( SECONDS < deadline )); do
        local a b c
        a="$("${compose[@]}" logs --no-color sidecar-a 2>/dev/null | grep -c 'event=connected' || true)"
        b="$("${compose[@]}" logs --no-color sidecar-b 2>/dev/null | grep -c 'event=connected' || true)"
        c="$("${compose[@]}" logs --no-color sidecar-c 2>/dev/null | grep -c 'event=connected' || true)"
        if (( a >= 1 && b >= 2 && c >= 1 )); then
            return 0
        fi
        sleep 1
    done
    "${compose[@]}" logs sidecar-a sidecar-b sidecar-c >&2
    echo "cascade sidecars did not establish both links" >&2
    return 1
}

run_once() {
    local payload="$1"
    local repetition="$2"
    export CASCADE_AB_PORT="${cascade_ab_override:-$port_cursor}"
    export CASCADE_BC_PORT="${cascade_bc_override:-$((port_cursor + 1))}"
    port_cursor=$((port_cursor + 2))
    export TEST_MODE="$mode"
    export PAYLOAD_BYTES="$payload"

    cleanup
    "${compose[@]}" up --detach --force-recreate sidecar-c sidecar-b sidecar-a
    wait_for_connections
    "${compose[@]}" up --detach worker-c
    "${compose[@]}" up --detach worker-b
    "${compose[@]}" up --detach worker-a

    local worker_a worker_b worker_c sidecar_a sidecar_b sidecar_c
    worker_a="$("${compose[@]}" ps --all --quiet worker-a)"
    worker_b="$("${compose[@]}" ps --all --quiet worker-b)"
    worker_c="$("${compose[@]}" ps --all --quiet worker-c)"
    sidecar_a="$("${compose[@]}" ps --all --quiet sidecar-a)"
    sidecar_b="$("${compose[@]}" ps --all --quiet sidecar-b)"
    sidecar_c="$("${compose[@]}" ps --all --quiet sidecar-c)"

    local samples stop_file sampler
    samples="$(mktemp)"
    stop_file="$(mktemp)"
    rm -f "$stop_file"
    (
        sleep "${WARMUP_SECONDS:-3}"
        while [[ ! -e "$stop_file" ]]; do
            local entry service container cpu
            for entry in \
                "sidecar-a:$sidecar_a" \
                "sidecar-b:$sidecar_b" \
                "sidecar-c:$sidecar_c"; do
                service="${entry%%:*}"
                container="${entry#*:}"
                cpu="$(docker stats --no-stream \
                    --format '{{.CPUPerc}}' "$container" 2>/dev/null \
                    | tr -d '%' || true)"
                if [[ -n "$cpu" ]]; then
                    printf '%s %s\n' "$service" "$cpu" >>"$samples"
                fi
            done
            sleep 1
        done
    ) &
    sampler=$!
    mapfile -t statuses < <(docker wait "$worker_a" "$worker_b" "$worker_c")
    touch "$stop_file"
    wait "$sampler"

    echo "cascade_result mode=$mode payload_bytes=$payload repetition=$repetition"
    docker logs "$worker_a"
    docker logs "$worker_b"
    docker logs "$worker_c"
    awk -v mode="$mode" -v payload="$payload" -v repetition="$repetition" '
        { total[$1] += $2; count[$1]++ }
        END {
            for (service in total) {
                printf "{\"benchmark\":\"cascade-sidecar-cpu\",\"mode\":\"%s\",\"payload_bytes\":%d,\"repetition\":%d,\"service\":\"%s\",\"cpu_pct_mean\":%.3f,\"samples\":%d}\n",
                    mode, payload, repetition, service,
                    total[service] / count[service], count[service]
            }
        }
    ' "$samples"
    rm -f "$samples" "$stop_file"
    for status in "${statuses[@]}"; do
        if [[ "$status" != "0" ]]; then
            echo "cascade worker exited with status $status" >&2
            return 1
        fi
    done
}

if [[ "${SKIP_BUILD:-1}" == "1" ]]; then
    "${compose[@]}" pull
else
    "${compose[@]}" build sidecar-a worker-a
fi

if [[ "$mode" == "correctness" ]]; then
    payloads="${CORRECTNESS_PAYLOADS:-4096 65536 1048576}"
    repetitions=1
else
    payloads="${BENCHMARK_PAYLOADS:-4096 65536 262144 1048576}"
    repetitions="${REPETITIONS:-3}"
fi

for payload in $payloads; do
    for ((repetition = 1; repetition <= repetitions; ++repetition)); do
        run_once "$payload" "$repetition"
    done
done
