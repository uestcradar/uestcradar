#!/usr/bin/env python3
"""Explicit-TCP synthetic Source -> two real Sidecars -> unchanged SignalSink.

Requires Docker Compose v2, configured image/capture env from compose.raw-iq.yaml,
and a host-built raw-iq-check. Does not access hardware or prove native throughput.
Leaves captures and bounded evidence in --evidence; removes only its own containers.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compose', action='append', type=Path)
    parser.add_argument('--checker', required=True, type=Path)
    parser.add_argument('--evidence', required=True, type=Path)
    parser.add_argument('--capture-root', type=Path, help='pre-created isolated directory on Docker daemon host')
    args = parser.parse_args()
    root = args.evidence.resolve()
    root.mkdir(parents=True, exist_ok=False)
    captures = args.capture_root or root / 'captures'
    if not captures.is_absolute():
        raise ValueError('capture-root must be absolute')
    if args.capture_root is None:
        captures.mkdir()
    env = dict(os.environ, RAWIQ_TEST_CAPTURES=str(captures))
    # Refuse an occupied listener rather than accidentally connecting to another test.
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', int(env.get('RAWIQ_TEST_PORT', '43637'))))
    project = f'rawiq-test-{os.getpid()}-{time.time_ns()}'
    compose = ['docker', 'compose', '-p', project]
    for path in args.compose or [Path(__file__).with_name('compose.raw-iq.yaml')]:
        compose += ['-f', str(path.resolve())]

    def run(command, timeout=30, check=True):
        try:
            return subprocess.run(command, env=env, text=True, capture_output=True, timeout=timeout, check=check)
        except subprocess.CalledProcessError as error:
            with (root / 'command-errors.log').open('a') as log:
                log.write((error.stdout or '') + (error.stderr or ''))
            raise

    def control(*arguments):
        value = json.loads(run(compose + ['exec', '-T', 'sink', '/app/signalsink', 'control', *arguments]).stdout)
        if not value.get('ok') or str(value.get('process_id')) != '1':
            raise RuntimeError(f'unexpected recorder identity/state: {value}')
        return value

    def wait(check, timeout=30):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            value = check()
            if value:
                return value
            time.sleep(.1)
        raise TimeoutError('pipeline condition did not become true')

    def status_ready():
        try:
            return control('status')
        except subprocess.CalledProcessError:
            return None

    def identities():
        ids = run(compose + ['ps', '-q', 'sink', 'sink-sidecar', 'source-sidecar']).stdout.split()
        return {v['Name']: {'id': v['Id'], 'image': v['Image'], 'started': v['State']['StartedAt']}
                for v in json.loads(run(['docker', 'inspect', *ids]).stdout)}

    try:
        run(compose + ['up', '-d', '--wait', 'sink', 'source-sidecar'], timeout=120)
        assert wait(status_ready)['state'] == 'idle'
        filesystem = run(compose + ['exec', '-T', 'sink', 'stat', '-f', '-c', '%T', '/captures']).stdout.strip()
        if filesystem in ('tmpfs', 'ramfs'):
            raise RuntimeError('capture root is memory-backed; use a real filesystem')
        before = identities()
        started = control('start', '--directory', 'smoke')
        assert started['state'] == 'recording', started
        run(compose + ['up', '-d', 'source'])
        source_id = run(compose + ['ps', '-a', '-q', 'source']).stdout.strip()

        def finished():
            state = json.loads(run(['docker', 'inspect', '--format', '{{json .State}}', source_id]).stdout)
            return state if state['Status'] == 'exited' else None

        assert wait(finished)['ExitCode'] == 0

        def received():
            status = control('status')
            assert status['state'] == 'recording', status
            assert int(status['accepted_frames']) <= 64, status
            return status if int(status['written_frames']) == 64 else None

        wait(received)
        control('stop', '--recording-id', started['recording_id'])
        final = wait(lambda: (s if (s := control('status'))['state'] == 'idle' else None))
        assert final['accepted_frames'] == final['written_frames'] == '64', final
        sink_id = run(compose + ['ps', '-q', 'sink']).stdout.strip()
        capture = root / 'verified.sink'
        run(['docker', 'cp', f'{sink_id}:/captures/smoke/{started["recording_id"]}.sink', str(capture)])
        checker = [str(args.checker.resolve()), str(capture), '--fixture', '--frames', '64']
        result = json.loads(run(checker).stdout)
        assert result['raw_bytes'] == int(final['written_bytes'])
        failures = []
        for kind, offset in [('iq', 8 + 8 + 64 + 24), ('timestamp', 8 + 8 + 64), ('truncated', None)]:
            bad = root / f'{kind}.sink'
            shutil.copyfile(capture, bad)
            with bad.open('r+b') as file:
                if offset is None:
                    file.truncate(bad.stat().st_size - 1)
                else:
                    file.seek(offset)
                    byte = file.read(1)[0]
                    file.seek(offset)
                    file.write(bytes([byte ^ 1]))
            checked = run([checker[0], str(bad), *checker[2:]], check=False)
            assert checked.returncode != 0, kind
            failures.append({'case': kind, 'error': checked.stderr.strip()})
            bad.unlink()  # Only this script's disposable corruption copies.
        assert identities() == before
        source_info = json.loads(run(['docker', 'inspect', source_id]).stdout)[0]
        sink_info = json.loads(run(['docker', 'inspect', sink_id]).stdout)[0]
        image_ids = sorted({v['image'] for v in before.values()} | {source_info['Image']})
        architectures = {image: run(['docker', 'image', 'inspect', '--format', '{{.Architecture}}', image]).stdout.strip()
                         for image in image_ids}
        runtime = {role: {'entrypoint': info['Config']['Entrypoint'],
                          'bind_mounts': [{'source': m['Source'], 'destination': m['Destination'], 'read_only': not m['RW']}
                                          for m in info['Mounts'] if m['Type'] == 'bind']}
                   for role, info in [('source', source_info), ('sink', sink_info)]}
        report = {'scope': 'explicit TCP synthetic closure; runtime mounts are test overrides, not published-image acceptance; not hardware or sustained throughput',
                  'capture_filesystem': filesystem, 'capture_root_on_docker_host': str(captures),
                  'image_architectures': architectures, 'runtime': runtime,
                  'docker_host': env.get('DOCKER_HOST', 'local'),
                  'capture': str(capture), 'file': result, 'recorder': final,
                  'rejected_corruptions': failures, 'existing_pipeline_containers_unchanged': True,
                  'containers': before, 'source_image': source_info['Image']}
        (root / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
    finally:
        logs = run(compose + ['logs', '--no-color', '--tail', '1000'], check=False)
        (root / 'containers.log').write_text(logs.stdout + logs.stderr)
        run(compose + ['down'], timeout=60)


if __name__ == '__main__':
    main()
