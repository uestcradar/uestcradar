#!/usr/bin/env python3
"""Exercise the real contract checker with bounded fake Docker inspect output."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

CHECKER = Path(__file__).with_name('verify-image-contract.sh').resolve()


class ContractTest(unittest.TestCase):
    def check_contract(self, component='', input_type='1:3', roles='sink', output='none', entrypoint='/app/worker'):
        labels = {'io.uestcradar.contract': 'worker/v2', 'io.uestcradar.component': component,
                  'io.uestcradar.input': input_type, 'io.uestcradar.roles': roles, 'io.uestcradar.output': output}
        with tempfile.TemporaryDirectory() as root:
            docker = Path(root, 'docker')
            docker.write_text('''#!/usr/bin/env python3
import json, os, re, sys
query = sys.argv[4]
label = re.search(r'index \\. "([^\"]+)"', query)
if label:
    print(json.loads(os.environ['TEST_LABELS']).get(label[1], ''))
else:
    print({'{{.Os}}':'linux', '{{.Architecture}}':'arm64', '{{json .Config.Entrypoint}}':json.dumps([os.environ['TEST_ENTRY']]), '{{json .Config.Cmd}}':'null'}[query])
''')
            docker.chmod(0o700)
            return subprocess.run(['bash', str(CHECKER), 'fixture', 'worker'], capture_output=True, text=True,
                                  env=dict(os.environ, PATH=root+os.pathsep+os.environ['PATH'], TEST_LABELS=json.dumps(labels), TEST_ENTRY=entrypoint)).returncode

    def test_only_explicit_signalsink_can_use_any(self):
        self.assertEqual(self.check_contract('signalsink', 'any', entrypoint='/app/signalsink'), 0)
        self.assertNotEqual(self.check_contract('', 'any'), 0)
        self.assertNotEqual(self.check_contract('signalsink', 'any', roles='operator', entrypoint='/app/signalsink'), 0)
        self.assertNotEqual(self.check_contract('signalsink', '1:3', entrypoint='/app/signalsink'), 0)
        self.assertNotEqual(self.check_contract('signalsink', 'any', entrypoint='/bin/sh'), 0)
        self.assertEqual(self.check_contract(), 0)
        self.assertNotEqual(self.check_contract(roles='source'), 0)


if __name__ == '__main__':
    unittest.main()
