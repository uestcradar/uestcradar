#!/usr/bin/env python3
"""Check release artifacts and finite-run results; does not prove hardware continuity."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parent
for name, digest in json.loads((root / 'sha256.json').read_text()).items():
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest, name

def load(directory, name):
    return json.loads((root / directory / (name + '.json')).read_text())

image = 'registry.chengyistudio.com/cxx/worker@sha256:6e9b3da9aa6cd0960cdf3e81b68e4f368ea85a797fda15622eeae61be16b0f59'
for directory in ('windows', 'session-1', 'session-2'):
    assert load(directory, 'result')['status'] == 'passed'
    identity = load(directory, 'source-identity-affinity')
    assert identity['image'] == image and identity['mounts'] == []
    assert sorted(identity['affinity'].values()) == [[8], [9], [10]]
    plan = load(directory, 'plan')
    assert [n['ip'] for n in plan['nodes']] == ['192.162.2.64', '192.162.2.80']
    assert plan['nodes'][0]['worker_digest'] == image
    assert all('DATA_PATH=strict-rdma\n' in n['env_preview'] for n in plan['nodes'])

for seconds, frames in [(10, 40925), (60, 229254)]:
    checked = load('windows', 'check-' + str(seconds))
    final = load('windows', 'final-' + str(seconds))
    assert checked['complete'] and checked['frames'] == frames
    assert checked['last_frame'] - checked['first_frame'] + 1 == frames
    assert int(final['accepted_frames']) == int(final['written_frames']) == frames
    assert final['state'] == 'idle' and final['queue_used_bytes'] == '0'

for directory, count, digest in [('session-1', 254155, '7524953354314128364'), ('session-2', 246505, '3345240607619294742')]:
    source = load(directory, 'source-final')
    checked = load(directory, 'sdk-check')
    structural = load(directory, 'window-check')
    final = load(directory, 'final')
    assert source['frames'] == checked['frames'] == structural['frames'] == count
    assert source['business_fnv1a64'] == checked['business_fnv1a64'] == digest
    assert source['samples'] == checked['samples'] == count * 8192
    assert checked['complete'] and checked['raw_bytes'] == count * 32856
    assert structural['first_frame'] == 1 and structural['last_frame'] == count
    assert int(final['accepted_frames']) == int(final['written_frames']) == count
    assert final['state'] == 'idle' and final['queue_used_bytes'] == '0'
    events = [json.loads(line) for line in (root / directory / 'source-events.jsonl').read_text().splitlines() if line.startswith('{')]
    assert len(events) == 1 and events[0]['Actor']['Attributes']['exitCode'] == '0'
    assert events[0]['from'] == image
    log = (root / directory / 'source-live.log').read_text()
    assert '[source] error=' not in log
    assert 'invalid_packets=0 changed_copies=0' in log and 'timestamp_errors=0' in log
    telemetry = load(directory, 'telemetry-before-stop')
    for node in telemetry['nodes']:
        link = next(link for link in node['links'] if link['peer_node_id'])
        assert link['transport'] == 'rdma' and link['status'] == 'connected' and not link['stale']

print('PASS: published identities, CPU placement, RDMA, file counts/digests, Source exits and synchronized recording states.')
