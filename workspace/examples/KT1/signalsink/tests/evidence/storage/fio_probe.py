import json,os,subprocess,tempfile,time
from pathlib import Path
root=Path('/root/workspace/captures/signalsink-test-mEPM4C')
work=Path(tempfile.mkdtemp(prefix='fio-native-',dir=str(root)))
path=work/'test.bin'
path.touch(exist_ok=False)
try:
    for depth in (1,32):
        report=root/('fio-'+work.name+'-depth'+str(depth)+'.json')
        command=['fio','--name=native-storage','--filename='+str(path),'--ioengine=libaio','--rw=write',
                 '--direct=1','--bs=1m','--size=4g','--iodepth='+str(depth),'--numjobs=1','--fallocate=posix',
                 '--refill_buffers=1','--scramble_buffers=1','--verify=crc32c','--verify_fatal=1','--do_verify=1',
                 '--end_fsync=1','--group_reporting=1','--output-format=json','--output='+str(report)]
        begin=time.monotonic();run=subprocess.run(command,timeout=160)
        data=json.loads(report.read_text());job=data['jobs'][0]
        print(json.dumps(dict(depth=depth,exit=run.returncode,error=job['error'],report=str(report),
                              write_bytes=job['write']['io_bytes'],write_Bps=job['write']['bw_bytes'],
                              write_ms=job['write']['runtime'],read_Bps=job['read']['bw_bytes'],
                              usr_cpu=job['usr_cpu'],sys_cpu=job['sys_cpu'],iodepth_level=job['iodepth_level'],
                              disk_util=data.get('disk_util'),wall_seconds=time.monotonic()-begin)),flush=True)
        if run.returncode or job['error']:raise RuntimeError('fio error; see report')
finally:
    path.unlink();work.rmdir()
