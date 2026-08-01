#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "$script_dir/../../../.." && pwd)
verify_script="$script_dir/verify-image-contract.sh"

release_host=${RELEASE_HOST:-192.162.2.64}
release_user=${RELEASE_USER:-root}
release_dir=${RELEASE_DIR:-}
registry=${REGISTRY:-registry.chengyistudio.com/cxx}
component=
worker_name=
remote_run=0
expected_sha=

usage() {
    cat >&2 <<'USAGE'
usage: release.sh --remote-dir ABSOLUTE_PATH

The release target is selected from an interactive CLI menu.

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
        --worker-name)
            [[ $# -ge 2 ]] || usage
            worker_name=$2
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

[[ "$release_dir" =~ ^/[A-Za-z0-9._/-]+$ ]] || {
    echo "--remote-dir must be an absolute path containing safe characters" >&2
    exit 2
}

validate_worker_dockerfile() {
    local name=$1
    [[ "$name" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || {
        echo "invalid Worker directory name: $name" >&2
        return 1
    }

    local worker_dir="$repo_root/workspace/examples/$name"
    local dockerfile="$worker_dir/Dockerfile"
    [[ -d "$worker_dir" && -f "$dockerfile" && -r "$dockerfile" && -s "$dockerfile" ]] || {
        echo "invalid Worker Dockerfile: workspace/examples/$name/Dockerfile" >&2
        return 1
    }

    grep -Eq '^[[:space:]]*FROM[[:space:]]+' "$dockerfile" || {
        echo "invalid Worker Dockerfile: missing FROM instruction: $dockerfile" >&2
        return 1
    }

    local label
    for label in \
        io.uestcradar.contract \
        io.uestcradar.roles \
        io.uestcradar.input \
        io.uestcradar.output; do
        grep -Fq "$label" "$dockerfile" || {
            echo "invalid Worker Dockerfile: missing Label $label: $dockerfile" >&2
            return 1
        }
    done
}

select_release_target() {
    local choice
    component=
    worker_name=
    printf '%s\n' \
        '请选择要发布的镜像：' \
        '  1) Sidecar' \
        '  2) Web' \
        '  3) Worker' \
        '  0) 退出'
    read -r -p '请输入序号: ' choice || {
        echo "未读取到选择，退出" >&2
        exit 1
    }
    case "$choice" in
        1) component=sidecar ;;
        2) component=web ;;
        3) component=worker ;;
        0) echo "已取消发布"; exit 0 ;;
        *) echo "无效选择: $choice" >&2; exit 2 ;;
    esac

    [[ "$component" == "worker" ]] || return

    local -a worker_names=()
    local dockerfile
    for dockerfile in "$repo_root"/workspace/examples/*/Dockerfile; do
        [[ -f "$dockerfile" ]] || continue
        worker_names+=("$(basename "$(dirname "$dockerfile")")")
    done
    (( ${#worker_names[@]} > 0 )) || {
        echo "workspace/examples 中没有可选的 Worker Dockerfile" >&2
        exit 1
    }

    echo "请选择 Worker："
    local index
    for index in "${!worker_names[@]}"; do
        printf '  %d) %s\n' "$((index + 1))" "${worker_names[$index]}"
    done
    echo '  0) 退出'
    read -r -p '请输入序号: ' choice || {
        echo "未读取到选择，退出" >&2
        exit 1
    }
    [[ "$choice" =~ ^[0-9]+$ ]] || {
        echo "无效 Worker 选择: $choice" >&2
        exit 2
    }
    (( choice != 0 )) || { echo "已取消发布"; exit 0; }
    (( choice >= 1 && choice <= ${#worker_names[@]} )) || {
        echo "无效 Worker 选择: $choice" >&2
        exit 2
    }
    worker_name=${worker_names[$((choice - 1))]}
    validate_worker_dockerfile "$worker_name"
}

run_local() {
    cd "$repo_root"
    select_release_target

    local -a scoped_paths=(.agents/skills/docker-release)
    case "$component" in
        sidecar) scoped_paths+=(workspace/sidecar/Dockerfile) ;;
        web) scoped_paths+=(workspace/web/Dockerfile) ;;
        worker) scoped_paths+=("workspace/examples/$worker_name") ;;
    esac
    if [[ -n "$(git status --porcelain -- "${scoped_paths[@]}")" ]]; then
        echo "release files contain uncommitted changes; create the release commit first" >&2
        exit 1
    fi

    local sha quoted_dir remote_command
    sha=$(git rev-parse HEAD)
    printf -v quoted_dir '%q' "$release_dir"
    remote_command="cd $quoted_dir && .agents/skills/docker-release/scripts/release.sh --remote-run --remote-dir $quoted_dir --component $(printf '%q' "$component") --expected-sha $(printf '%q' "$sha")"
    if [[ "$component" == "worker" ]]; then
        remote_command+=" --worker-name $(printf '%q' "$worker_name")"
    fi
    if [[ "$component" == "worker" ]]; then
        echo "dispatching Worker '$worker_name' release to ${release_user}@${release_host}:${release_dir}"
    else
        echo "dispatching $component release to ${release_user}@${release_host}:${release_dir}"
    fi
    ssh \
        -o BatchMode=yes \
        -o StrictHostKeyChecking=accept-new \
        -o ConnectTimeout=8 \
        "${release_user}@${release_host}" \
        "$remote_command"
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
    case "$component" in
        sidecar|worker|web) ;;
        *) echo "invalid remote component: $component" >&2; exit 2 ;;
    esac
    if [[ "$component" == "worker" ]]; then
        [[ -n "$worker_name" ]] || {
            echo "missing --worker-name for Worker release" >&2
            exit 2
        }
        validate_worker_dockerfile "$worker_name"
    elif [[ -n "$worker_name" ]]; then
        echo "--worker-name is only valid for a Worker release" >&2
        exit 2
    fi

    log "remote host=$(hostname) arch=$(uname -m) component=$component${worker_name:+ worker=$worker_name}"
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
    local web_version="$registry/web:sha-${sha12}-arm64"
    local web_latest="$registry/web:latest"

    local worker_tag=${worker_name//_/-}
    worker_tag=${worker_tag,,}
    local worker_version="$registry/worker:${worker_tag:+${worker_tag}-}sha-${sha12}-arm64"
    local worker_latest="$registry/worker:${worker_tag:+${worker_tag}-}latest"
    local version_image moving_image kind
    case "$component" in
        sidecar)
            version_image=$sidecar_version
            moving_image=$sidecar_latest
            kind=sidecar
            ;;
        worker)
            version_image=$worker_version
            moving_image=$worker_latest
            kind=worker
            ;;
        web)
            version_image=$web_version
            moving_image=$web_latest
            kind=web
            ;;
    esac
    assert_remote_tag_absent "$version_image"

    if [[ "$component" == "sidecar" ]]; then
        log "building Sidecar: $sidecar_version"
        docker build --target runtime -f workspace/sidecar/Dockerfile \
            -t "$sidecar_version" .
        log "verifying Sidecar image contract"
        verify_remote_image "$sidecar_version" sidecar
    fi
    if [[ "$component" == "worker" ]]; then
        local worker_dir="workspace/examples/$worker_name"
        log "building Worker '$worker_name': $worker_version"
        docker build -f "$worker_dir/Dockerfile" \
            -t "$worker_version" "$worker_dir"
        log "verifying Worker image contract"
        verify_remote_image "$worker_version" worker
    fi
    if [[ "$component" == "web" ]]; then
        ensure_web_build_base
        log "building Web: $web_version"
        docker build --build-arg GO_BASE="$registry/web:build-base" \
            -f workspace/web/Dockerfile -t "$web_version" .
        log "verifying Web image contract"
        verify_remote_image "$web_version" web
    fi

    publish_image "$version_image" "$moving_image" "$kind"
}

if (( remote_run == 1 )); then
    run_remote
else
    run_local
fi
