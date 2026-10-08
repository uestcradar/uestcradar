import json,time
from pathlib import Path
while True:
    row=next(line.split() for line in Path('/proc/diskstats').read_text().splitlines() if line.split()[2]=='sda')
    memory={line.split(':')[0]:int(line.split()[1])*1024 for line in Path('/proc/meminfo').read_text().splitlines() if line.startswith(('Dirty:','Writeback:'))}
    print(json.dumps(dict(monotonic=time.monotonic(),written_sectors=int(row[9]),writes_ms=int(row[10]),inflight=int(row[11]),busy_ms=int(row[12]),**memory)),flush=True)
    time.sleep(.25)
