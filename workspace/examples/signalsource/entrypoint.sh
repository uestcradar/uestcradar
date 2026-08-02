#!/bin/sh
set -eu

case "${CASCADE_ROLE:-source}" in
    source)
        exec /app/signalsource "$@"
        ;;
    sink)
        exec /app/frame-sink --type pulse "$@"
        ;;
    *)
        echo "signalsource image supports CASCADE_ROLE=source|sink" >&2
        exit 2
        ;;
esac
