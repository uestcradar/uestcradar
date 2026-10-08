import json,mmap,os,time,uuid
from pathlib import Path
root=Path('/root/workspace/captures/signalsink-test-mEPM4C')
block=mmap.mmap(-1,4*1024**2)
random=os.urandom(len(block))
for pattern in ('random','zero','random'):
    block[:]=random if pattern=='random' else b'\0'*len(block)
    path=root/('pattern-probe-'+uuid.uuid4().hex)
    fd=os.open(str(path),os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_DIRECT,0o600)
    try:
        start=time.monotonic()
        for _ in range(128):
            if os.write(fd,block)!=len(block):raise RuntimeError('short write')
        os.fsync(fd)
        elapsed=time.monotonic()-start
        print(json.dumps(dict(pattern=pattern,bytes=512*1024**2,seconds=elapsed,durable_bytes_per_second=512*1024**2/elapsed)),flush=True)
    finally:
        os.close(fd);path.unlink()
block.close()
