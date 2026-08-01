#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/../../../.." && pwd)
verify_script="$script_dir/verify-image-contract.sh"

release_host=${RELEASE_HOST:-192.162.2.64}
release_user=${RELEASE_USER:-root}
release_dir=${RELEASE_DIR:-}
registry=${REGISTRY:-registry.chengyistudio.com/cxx}
component=${RELEASE_COMPONENT:-all}
remote_run=0
expected_sha=

usage() {
    cat >&2 <<'USAGE'
usage: release.sh --remote-dir ABSOLUTE_PATH [--component sidecar|worker|web|all]

Environment overrides:
  RELEASE_HOST  default: 192.162.2.64
  RELEASE_USER  default: root
  RELEASE_DIR
  REGISTRY      default: registry.chengyistudio.com/cxx
USAGE
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --remote-dir)
            [[ $# -ge 2 ]] || usage
            release_dir=$2
            shift 2
            ;;
        --component)
            [[ $# -ge 2 ]] || usage
            component=$2
            shift 2
            ;;
        --remote-run)
            remote_run=1
            shift
            ;;
        --expected-sha)
            [[ $# -ge 2 ]] || usage
            expected_sha=$2
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage
            ;;
    esac
done

case "$component" in
    sidecar|worker|web|all) ;;
    *) echo "invalid component: $component" >&2; exit 2 ;;
esac

[[ "$release_dir" =~ ^/[A-Za-z0-9._/-]+$ ]] || {
    echo "--remote-dir must be an absolute path containing safe characters" >&2
    exit 2
}

scoped_paths=(
    workspace/sidecar/Dockerfile
    workspace/examples/cascade_worker/Dockerfile
    workspace/web/Dockerfile
    .agents/skills/docker-release
)

run_local() {
    cd "$repo_root"
    if [[ -n "$(git status --porcelain -- "${scoped_paths[@]}")" ]]; then
        echo "release files contain uncommitted changes; create the release commit first" >&2
        exit 1
    fi

    local sha quoted_dir
    sha=$(git rev-parse HEAD)
    printf -v quoted_dir '%q' "$release_dir"
    echo "dispatching release to ${release_user}@${release_host}:${release_dir}"
    ssh \
        -o BatchMode=yes \
        -o StrictHostKeyChecking=accept-new \
        -o ConnectTimeout=8 \
        "${release_user}@${release_host}" \
        "cd $quoted_dir && .agents/skills/docker-release/scripts/release.sh --remote-run --remote-dir $quoted_dir --component $(printf '%q' "$component") --expected-sha $(printf '%q' "$sha")"
}

log() {
    printf '[release] %s\n' "$*"
}

# Docker 19.03 requires experimental CLI support for `docker manifest inspect`.
# Use pull as the common read/auth/existence check so the ARM release host works
# without changing its daemon or CLI configuration.
pull_remote_tag() {
    local image=$1 output status
    if output=$(docker pull "$image" 2>&1); then
        printf '%s\n' "$output"
        return 0
    else
        status=$?
    fi

    if grep -Eqi \
        'manifest unknown|manifest.*not found|not found: manifest|repository .* not found' \
        <<<"$output"; then
        return 10
    fi

    printf '%s\n' "$output" >&2
    return "$status"
}

assert_remote_tag_absent() {
    local image=$1 status
    log "checking immutable Tag is unused: $image"
    if pull_remote_tag "$image"; then
        echo "immutable Tag already exists: $image" >&2
        return 1
    else
        status=$?
    fi
    if (( status == 10 )); then
        log "immutable Tag is available: $image"
        return 0
    fi
    echo "failed to inspect immutable Tag: $image" >&2
    return "$status"
}

verify_remote_image() {
    "$verify_script" "$1" "$2"
}

ensure_web_build_base() {
    local destination="$registry/web:build-base"
    local status
    log "checking Web build base: $destination"
    if pull_remote_tag "$destination"; then
        return
    else
        status=$?
    fi
    (( status == 10 )) || return "$status"

    local legacy="$registry/telemetry-web:build-base"
    log "initializing $destination from $legacy"
    docker pull "$legacy"
    docker tag "$legacy" "$destination"
    docker push "$destination"
}

smoke_test() {
    local sidecar_image=$1 worker_image=$2 web_image=$3 sha12=$4
    local compose_file=workspace/sidecar/tools/compose.cascade.yaml
    local project="uestcradar-release-${sha12}"
    local web_container="${project}-web"

    local -a compose
    if command -v docker-compose >/dev/null 2>&1; then
        compose=(docker-compose --project-name "$project" -f "$compose_file")
    elif docker compose version >/dev/null 2>&1; then
        compose=(docker compose --project-name "$project" -f "$compose_file")
    else
        echo "neither docker-compose nor docker compose is available" >&2
        return 1
    fi

    cleanup_smoke() {
        SIDECAR_IMAGE="$sidecar_image" \
        CASCADE_IMAGE="$worker_image" \
            "${compose[@]}" down --volumes --remove-orphans >/dev/null 2>&1 || true
        docker rm -f "$web_container" >/dev/null 2>&1 || true
    }
    trap cleanup_smoke RETURN
    cleanup_smoke

    docker run -d \
        --name "$web_container" \
        --network host \
        --read-only \
        --cap-drop ALL \
        --security-opt no-new-privileges:true \
        "$web_image" >/dev/null

    SIDECAR_IMAGE="$sidecar_image" CASCADE_IMAGE="$worker_image" \
        "${compose[@]}" up -d --no-build sidecar-c sidecar-b sidecar-a

    local connected=0
    for _ in $(seq 1 60); do
        local a b c
        a=$(SIDECAR_IMAGE="$sidecar_image" CASCADE_IMAGE="$worker_image" \
            "${compose[@]}" logs --no-color sidecar-a 2>/dev/null | \
            grep -c 'event=connected' || true)
        b=$(SIDECAR_IMAGE="$sidecar_image" CASCADE_IMAGE="$worker_image" \
            "${compose[@]}" logs --no-color sidecar-b 2>/dev/null | \
            grep -c 'event=connected' || true)
        c=$(SIDECAR_IMAGE="$sidecar_image" CASCADE_IMAGE="$worker_image" \
            "${compose[@]}" logs --no-color sidecar-c 2>/dev/null | \
            grep -c 'event=connected' || true)
        if (( a >= 1 && b >= 2 && c >= 1 )); then
            connected=1
            break
        fi
        sleep 1
    done
    (( connected == 1 )) || {
        echo "cascade links did not connect within 60 seconds" >&2
        return 1
    }

    SIDECAR_IMAGE="$sidecar_image" CASCADE_IMAGE="$worker_image" \
        TEST_MODE=correctness FRAMES=1000 \
        "${compose[@]}" up -d --no-build worker-c worker-b worker-a

    local workers_ok=0
    for _ in $(seq 1 120); do
        local all_done=1 all_ok=1 service container state exit_code
        for service in worker-a worker-b worker-c; do
            container=$(SIDECAR_IMAGE="$sidecar_image" CASCADE_IMAGE="$worker_image" \
                "${compose[@]}" ps -q "$service")
            [[ -n "$container" ]] || { all_done=0; all_ok=0; continue; }
            state=$(docker inspect --format '{{.State.Status}}' "$container")
            exit_code=$(docker inspect --format '{{.State.ExitCode}}' "$container")
            [[ "$state" == "exited" ]] || all_done=0
            [[ "$state" != "exited" || "$exit_code" == "0" ]] || all_ok=0
        done
        if (( all_done == 1 )); then
            (( all_ok == 1 )) || return 1
            workers_ok=1
            break
        fi
        sleep 1
    done
    (( workers_ok == 1 )) || {
        echo "cascade Workers did not finish successfully within 120 seconds" >&2
        return 1
    }

    if command -v curl >/dev/null 2>&1; then
        local snapshot=
        for _ in $(seq 1 30); do
            snapshot=$(curl -fsS http://127.0.0.1:8080/api/nodes 2>/dev/null || true)
            [[ "$snapshot" == *"cascade-a"* && "$snapshot" == *"cascade-b"* && \
               "$snapshot" == *"cascade-c"* ]] && break
            sleep 1
        done
        [[ "$snapshot" == *"cascade-a"* && "$snapshot" == *"cascade-b"* && \
           "$snapshot" == *"cascade-c"* ]] || {
            echo "Web API did not report all cascade nodes" >&2
            return 1
        }
    fi

    echo "ARM smoke test passed"
}

publish_image() {
    local version_image=$1 moving_image=$2 kind=$3
    log "pushing immutable image: $version_image"
    docker push "$version_image"
    log "pulling immutable image for contract verification: $version_image"
    docker pull "$version_image"
    verify_remote_image "$version_image" "$kind"

    log "updating moving Tag: $moving_image"
    docker tag "$version_image" "$moving_image"
    docker push "$moving_image"
    docker pull "$moving_image"

    local version_id moving_id
    version_id=$(docker image inspect --format '{{.Id}}' "$version_image")
    moving_id=$(docker image inspect --format '{{.Id}}' "$moving_image")
    [[ "$version_id" == "$moving_id" ]] || {
        echo "moving Tag does not match immutable image: $moving_image" >&2
        return 1
    }
    verify_remote_image "$moving_image" "$kind"
    docker image inspect --format '{{join .RepoDigests "\n"}}' "$moving_image"
}

run_remote() {
    cd "$repo_root"
    log "remote host=$(hostname) arch=$(uname -m) component=$component"
    log "validating release checkout"
    [[ -n "$expected_sha" ]] || { echo "missing --expected-sha" >&2; exit 2; }
    [[ "$(uname -m)" == "aarch64" ]] || {
        echo "release host must be aarch64" >&2
        exit 1
    }
    [[ -z "$(git status --porcelain)" ]] || {
        echo "remote release repository is not clean" >&2
        exit 1
    }
    [[ "$(git rev-parse HEAD)" == "$expected_sha" ]] || {
        echo "remote HEAD does not match local release commit" >&2
        exit 1
    }
    log "checking Docker engine"
    docker info --format 'Docker server={{.ServerVersion}} storage={{.Driver}}'
    log "checking Registry read access with $registry/sidecar:latest"
    if ! pull_remote_tag "$registry/sidecar:latest"; then
        echo "Registry read preflight failed for $registry/sidecar:latest" >&2
        exit 1
    fi

    local sha12=${expected_sha:0:12}
    local sidecar_version="$registry/sidecar:sha-${sha12}-arm64"
    local sidecar_latest="$registry/sidecar:latest"
    local worker_version="$registry/worker:cascade-sha-${sha12}-arm64"
    local worker_latest="$registry/worker:cascade-latest"
    local web_version="$registry/web:sha-${sha12}-arm64"
    local web_latest="$registry/web:latest"

    local -a versions=()
    case "$component" in
        sidecar) versions=("$sidecar_version") ;;
        worker) versions=("$worker_version") ;;
        web) versions=("$web_version") ;;
        all) versions=("$sidecar_version" "$worker_version" "$web_version") ;;
    esac
    local image
    for image in "${versions[@]}"; do
        assert_remote_tag_absent "$image"
    done

    if [[ "$component" == "sidecar" || "$component" == "all" ]]; then
        log "building Sidecar: $sidecar_version"
        docker build --target runtime -f workspace/sidecar/Dockerfile \
            -t "$sidecar_version" .
        log "verifying Sidecar image contract"
        verify_remote_image "$sidecar_version" sidecar
    fi
    if [[ "$component" == "worker" || "$component" == "all" ]]; then
        log "building Worker: $worker_version"
        docker build -f workspace/examples/cascade_worker/Dockerfile \
            -t "$worker_version" workspace/examples/cascade_worker
        log "verifying Worker image contract"
        verify_remote_image "$worker_version" worker
    fi
    if [[ "$component" == "web" || "$component" == "all" ]]; then
        ensure_web_build_base
        log "building Web: $web_version"
        docker build --build-arg GO_BASE="$registry/web:build-base" \
            -f workspace/web/Dockerfile -t "$web_version" .
        log "verifying Web image contract"
        verify_remote_image "$web_version" web
    fi

    if [[ "$component" == "all" ]]; then
        log "running ARM cascade smoke test"
        smoke_test "$sidecar_version" "$worker_version" "$web_version" "$sha12"
    fi

    if [[ "$component" == "sidecar" || "$component" == "all" ]]; then
        publish_image "$sidecar_version" "$sidecar_latest" sidecar
    fi
    if [[ "$component" == "worker" || "$component" == "all" ]]; then
        publish_image "$worker_version" "$worker_latest" worker
    fi
    if [[ "$component" == "web" || "$component" == "all" ]]; then
        publish_image "$web_version" "$web_latest" web
    fi
}

if (( remote_run == 1 )); then
    run_remote
else
    run_local
fi
