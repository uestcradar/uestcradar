import subprocess,json,time,os,pathlib
root=pathlib.Path('/tmp/pcie-cpu-test-'+str(time.time_ns()));root.mkdir()
image='registry.chengyistudio.com/cxx/worker@sha256:5cf489a8efd621ee1a1ce5f54a6e33ee8a16d0eca114e40c49d90a00fc1a330b'
binary='/tmp/pcie-cpu-candidate/build/pcie_source'
def cmd(*args):return subprocess.check_output(args,text=True).strip()
assert not cmd('sh','-c','grep -l /dev/mem /proc/[0-9]*/maps 2>/dev/null || true')
cases=[('duplicate',[],['--cpu-map','8,8,10']),('restricted',['--cpuset-cpus','8'],[]),('default-1',[],[]),('default-2',[],[]),('default-3',[],[])]
for mode,flags,args in cases:
 name='pcie-cpu-test-'+mode+'-'+str(time.time_ns());created=False
 try:
  cid=cmd('docker','create','--name',name,'--network','none','--device','/dev/mem:/dev/mem:rw','--cap-add','SYS_RAWIO','-v',binary+':/app/pcie_source:ro',*flags,image,'--capture-only','--duration-seconds','60',*args);created=True
  cmd('docker','start',cid)
  affinity={}
  if mode.startswith('default'):
   pid=int(cmd('docker','inspect','--format','{{.State.Pid}}',cid))
   for _ in range(50):
    tids=sorted(map(int,os.listdir('/proc/%s/task'%pid)))
    if len(tids)==2:break
    time.sleep(.01)
   affinity={str(t):sorted(os.sched_getaffinity(t)) for t in tids}
   assert affinity[str(pid)]==[8] and sorted(affinity.values())==[[8],[9]],affinity
  r=subprocess.run(['docker','wait',cid],capture_output=True,text=True,timeout=80)
  log=subprocess.run(['docker','logs',cid],capture_output=True,text=True);text=log.stdout+log.stderr
  record={'mode':mode,'exit':r.stdout.strip(),'affinity':affinity,'container':cid,'binary':binary,'log':text,'delivery':'temporary native candidate binary mount, not published acceptance'}
  (root/(mode+'.json')).write_text(json.dumps(record,indent=2)+'\n')
  if mode.startswith('default'):
   assert record['exit']=='0' and 'receive_check=pass' in text and 'cpu_placement_verified=true capture_cpu=8 dma_cpu=9 output_cpu=disabled' in text,text
  else:assert record['exit']=='1' and '[source] mode=' not in text,text
  print(mode,record['exit'],affinity,'\n'.join(text.splitlines()[-2:]),flush=True)
 finally:
  if created:subprocess.run(['docker','rm','-f',name],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
print('evidence',root,flush=True)
