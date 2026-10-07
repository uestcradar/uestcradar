"""Run with python3; checks Worker paths without Docker, SSH, or publishing."""
from pathlib import Path
import subprocess

script = Path(__file__).with_name("release.sh")
root = script.resolve().parents[4]
text = script.read_text()
# Load only menu/validation functions, not release dispatch or its side effects.
functions = text[text.index("validate_worker_dockerfile() {"):text.index("run_local() {")]
tag_code = text[text.index("    local worker_tag="):text.index("    local worker_version=")]
prefix = 'set -euo pipefail\nrepo_root=$1\n' + functions

workers = ["KT2", "KT1/cascade_worker", "KT1/pcie_source", "KT1/signalsource"]
for index, worker in enumerate(workers, 1):
    tag = worker.rsplit("/", 1)[-1].replace("_", "-").lower()
    subprocess.run(
        ["bash", "-c", prefix + '''
select_release_target
[[ "$worker_name" == "$2" ]]
check_tag() {
''' + tag_code + '''
    [[ "$worker_tag" == "$3" ]]
}
check_tag "$@"
''', "test", str(root), worker, tag],
        input=f"3\n{index}\n", text=True, capture_output=True, check=True,
    )
for invalid in ["../KT2", "KT1/../KT2", "KT1/signalsource/extra", "/KT2", "KT1/signalsink"]:
    subprocess.run(
        ["bash", "-c", prefix + '\n! validate_worker_dockerfile "$2"',
         "test", str(root), invalid], capture_output=True, check=True,
    )
print("Worker menu, path validation and unchanged image tags: PASS")
