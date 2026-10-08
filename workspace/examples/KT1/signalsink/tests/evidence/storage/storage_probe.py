import json,mmap,os,time,uuid
from pathlib import Path
root=Path('/root/workspace/captures/signalsink-test-mEPM4C')
size=2*1024**3
block=mmap.mmap(-1,4*1024**2)
block[:]=os.urandom(len(block))
for direct in (False,True):
    path=root/('probe-'+uuid.uuid4().hex)
    fd=os.open(str(path),os.O_WRONLY|os.O_CREAT|os.O_EXCL|(os.O_DIRECT if direct else 0),0o600)
    try:
        start=time.monotonic()
        for _ in range(size//len(block)):
            offset=0
            while offset<len(block):
                count=os.write(fd,memoryview(block)[offset:])
                if not count:raise RuntimeError('zero write')
                offset+=count
        written=time.monotonic()
        os.fsync(fd)
        synced=time.monotonic()
        print(json.dumps(dict(direct=direct,bytes=size,write_seconds=written-start,sync_seconds=synced-written,total_seconds=synced-start,durable_bytes_per_second=size/(synced-start))),flush=True)
    finally:
        os.close(fd)
        path.unlink() # Only this probe's exclusively created disposable file.
block.close()
