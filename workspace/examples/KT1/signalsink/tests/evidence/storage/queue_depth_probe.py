import concurrent.futures,json,mmap,os,time,uuid
from pathlib import Path
root=Path('/root/workspace/captures/signalsink-test-mEPM4C')
path=root/('depth-probe-'+uuid.uuid4().hex)
block=mmap.mmap(-1,4*1024**2)
fd=os.open(str(path),os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_DIRECT,0o600)
def write_lane(lane,depth):
    for index in range(lane,256,depth):
        if os.pwrite(fd,block,index*len(block)) != len(block):raise RuntimeError('short write')
try:
    os.posix_fallocate(fd,0,1024**3)
    for depth in (1,4,8,1):
        block[:]=os.urandom(len(block))
        start=time.monotonic()
        with concurrent.futures.ThreadPoolExecutor(max_workers=depth) as pool:
            futures=[pool.submit(write_lane,lane,depth) for lane in range(depth)]
            for future in futures:future.result()
        os.fsync(fd);elapsed=time.monotonic()-start
        print(json.dumps(dict(depth=depth,bytes=1024**3,seconds=elapsed,durable_bytes_per_second=1024**3/elapsed)),flush=True)
finally:
    os.close(fd);path.unlink();block.close()
