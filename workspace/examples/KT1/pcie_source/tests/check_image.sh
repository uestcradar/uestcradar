#!/usr/bin/env bash
set -euo pipefail
image="${1:-pcie-source:dev}"
docker image inspect "$image" --format 'id={{.Id}} arch={{.Architecture}} entrypoint={{json .Config.Entrypoint}}'
test "$(docker image inspect "$image" --format '{{index .Config.Labels "io.uestcradar.output"}}')" = '4:1'
docker run --rm "$image" --help
docker run --rm --entrypoint sh "$image" -ec '
  test -x /app/pcie_source
  test ! -e /data/CPI0
  for file in config_sync_4.txt config_drp_4g8.txt log.json; do
    test -s /app/pcie_config/$file
  done
'
# No hardware access, source process or SHM required by this check.
