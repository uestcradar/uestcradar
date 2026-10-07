#!/usr/bin/env bash
set -euo pipefail
image="${1:-pcie-source:dev}"
docker image inspect "$image" --format 'id={{.Id}} arch={{.Architecture}} entrypoint={{json .Config.Entrypoint}}'
test "$(docker image inspect "$image" --format '{{index .Config.Labels "io.uestcradar.output"}}')" = '1:3'
docker run --rm "$image" --help
docker run --rm --entrypoint sh "$image" -ec '
  test -x /app/pcie_source
  for n in 0 1 2 3 4 5 6 7 8 9; do
    for file in metadata.json pulse_time.txt pulse_phase.txt pulse_freq.txt wd0.txt; do
      test -s /data/CPI$n/$file
    done
    test ! -e /data/CPI$n/input.bin
  done
  for file in config_sync_4.txt config_drp_4g8.txt log.json; do
    test -s /app/pcie_config/$file
  done
'
# No hardware access, source process or SHM required by this check.
