import json,mmap,os,time,uuid
from pathlib import Path
root=Path('/root/workspace/captures/signalsink-test-mEPM4C')
path=root/('preallocation-probe-'+uuid.uuid4().hex)
block=mmap.mmap(-1,4*1024**2)
fd=os.open(str(path),os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_DIRECT,0o600)
try:
    begin=time.monotonic();os.posix_fallocate(fd,0,1024**3);allocation=time.monotonic()-begin
    for mode in ('preallocated-first-write','overwrite-test-file'):
        block[:]=os.urandom(len(block));os.lseek(fd,0,os.SEEK_SET)
        start=time.monotonic()
        for _ in range(256):
            if os.write(fd,block)!=len(block):raise RuntimeError('short write')
        os.fsync(fd);elapsed=time.monotonic()-start
        print(json.dumps(dict(mode=mode,allocation_seconds=allocation,bytes=1024**3,seconds=elapsed,durable_bytes_per_second=1024**3/elapsed)),flush=True)
finally:
    os.close(fd);path.unlink();block.close()
