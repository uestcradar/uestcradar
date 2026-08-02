#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: $0 IMAGE sdk|sidecar|worker|web" >&2
    exit 2
}

[[ $# -eq 2 ]] || usage
image=$1
kind=$2

case "$kind" in
    sdk|sidecar|worker|web) ;;
    *) usage ;;
esac

inspect() {
    docker image inspect --format "$1" "$image"
}

label() {
    inspect "{{with .Config.Labels}}{{index . \"$1\"}}{{end}}"
}

fail() {
    echo "contract verification failed: $*" >&2
    exit 1
}

[[ "$(inspect '{{.Os}}')" == "linux" ]] || fail "$image is not linux"
[[ "$(inspect '{{.Architecture}}')" == "arm64" ]] || fail "$image is not arm64"

case "$kind" in
    sdk) expected_contract=algo-base/v2 ;;
    sidecar) expected_contract=sidecar/v2 ;;
    worker) expected_contract=worker/v2 ;;
    web) expected_contract=web/v1 ;;
esac

[[ "$(label io.uestcradar.contract)" == "$expected_contract" ]] || \
    fail "$image has an invalid io.uestcradar.contract"

if [[ "$kind" == "sdk" ]]; then
    docker run --rm --entrypoint /bin/sh "$image" -c '
        command -v cmake >/dev/null &&
        command -v g++ >/dev/null &&
        test -f /usr/local/lib/libuestcradar_sdk.so &&
        test -f /usr/local/include/sdk.h &&
        test -f /usr/local/include/data.h &&
        test -f /usr/local/lib/cmake/cycomm_sdk/cycomm_sdkConfig.cmake &&
        test -f /usr/local/share/cycomm_sdk/contracts/contracts.manifest.json
    ' || fail "$image does not contain the installed SDK toolchain and contract catalog"
else
    entrypoint=$(inspect '{{json .Config.Entrypoint}}')
    command=$(inspect '{{json .Config.Cmd}}')
    if [[ "$entrypoint" == "null" || "$entrypoint" == "[]" ]]; then
        [[ "$command" != "null" && "$command" != "[]" ]] || \
            fail "$image has neither Entrypoint nor Cmd"
    fi
fi

if [[ "$kind" == "sidecar" ]]; then
    [[ "$entrypoint" == '["/app/sidecar"]' ]] || \
        fail "$image Entrypoint is not /app/sidecar"
elif [[ "$kind" == "web" ]]; then
    [[ "$entrypoint" == '["/telemetry"]' ]] || \
        fail "$image Entrypoint is not /telemetry"
elif [[ "$kind" == "worker" ]]; then
    roles=$(label io.uestcradar.roles)
    input=$(label io.uestcradar.input)
    output=$(label io.uestcradar.output)
    [[ -n "$roles" && -n "$input" && -n "$output" ]] || \
        fail "$image is missing Worker labels"

    declare -A seen=()
    IFS=',' read -r -a role_list <<<"$roles"
    [[ ${#role_list[@]} -gt 0 ]] || fail "$image has no roles"
    for role in "${role_list[@]}"; do
        case "$role" in
            source|operator|sink) ;;
            *) fail "$image has unsupported role: $role" ;;
        esac
        [[ -z "${seen[$role]:-}" ]] || fail "$image repeats role: $role"
        seen[$role]=1
    done

    type_pattern='^(none|[1-9][0-9]*:[1-9][0-9]*)$'
    [[ "$input" =~ $type_pattern ]] || fail "$image has invalid input: $input"
    [[ "$output" =~ $type_pattern ]] || fail "$image has invalid output: $output"
    if [[ -n "${seen[source]:-}" && "$output" == "none" ]]; then
        fail "source Worker requires output"
    fi
    if [[ -n "${seen[operator]:-}" && \
          ( "$input" == "none" || "$output" == "none" ) ]]; then
        fail "operator Worker requires input and output"
    fi
    if [[ -n "${seen[sink]:-}" && "$input" == "none" ]]; then
        fail "sink Worker requires input"
    fi
fi

echo "contract verified: image=$image kind=$kind architecture=arm64"
