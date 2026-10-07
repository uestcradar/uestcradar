#!/usr/bin/env python3
"""Check capture evidence and report selected-channel throughput; no hardware access."""
import argparse
import collections
import gzip
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("capture", type=Path)
parser.add_argument("errors", type=Path)
parser.add_argument("--min-msps", type=float, default=0)
args = parser.parse_args()
reports = [line for line in args.capture.read_text().splitlines() if line.startswith("[capture] ")]
assert reports, "missing capture summary"
summary = dict(item.split("=", 1) for item in reports[-1].split()[1:])
samples = int(summary["channel_samples"].split(",")[int(summary["channel"])])
rate = samples / float(summary["elapsed_s"]) / 1e6
counts = collections.Counter()
first = last = end = run_id = expected_delta = None
opener = gzip.open if args.errors.suffix == ".gz" else open
with opener(args.errors, "rt") as stream:
    for line in stream:
        item = json.loads(line)
        if item["event"] == "run_start":
            assert run_id is None, "use a separate error file per benchmark run"
            run_id = item["run_id"]
            expected_delta = item["expected_delta"]
        assert run_id == item["run_id"], "mixed run ids"
        if item["event"] == "run_end":
            end = item
        if item["event"] != "rx_timestamp_error":
            continue
        previous, current = item["previous"], item["current"]
        delta = int(item["delta_u64"])
        assert delta == (int(current["rx_first"]) - int(previous["rx_first"])) % (1 << 64)
        assert delta != expected_delta and item["expected_delta"] == expected_delta
        counts[delta] += 1
        first = first or previous
        last = current
assert end and sum(counts.values()) == int(end["errors"]) == int(summary["timestamp_errors"])
print(f"samples={samples} elapsed_s={summary['elapsed_s']} selected_msps={rate:.6f}")
print(f"iq_packets={summary['iq_packets']} controls={summary['control_packets']} errors={sum(counts.values())}")
print(f"invalid_packets={summary['invalid_packets']} changed_copies={summary['changed_copies']}")
print(f"expected_delta={expected_delta} error_delta_histogram={dict(sorted(counts.items()))}")
if first and last:
    seconds = (int(last["monotonic_ns"]) - int(first["monotonic_ns"])) / 1e9
    if seconds > 0:
        print(f"observed_rx_counter_hz={(int(last['rx_first']) - int(first['rx_first'])) / seconds:.3f}")
assert rate >= args.min_msps, f"throughput {rate:.3f} MS/s < {args.min_msps} MS/s target"
