import sys,struct,hashlib,json
path=sys.argv[1];count=total=0;first=last=rx=tx0=rx0=None;digest=hashlib.sha256()
with open(path,'rb') as stream:
 assert stream.read(8)==b'USINK001','magic'
 while True:
  prefix=stream.read(8);assert len(prefix)==8,'missing footer'
  length,=struct.unpack('<Q',prefix)
  if length==0:
   assert stream.read(16)==struct.pack('<QQ',count,total) and not stream.read(1),'footer'
   break
  assert length==32856,'RawIQ frame length'
  frame=stream.read(length);assert len(frame)==length,'truncated frame'
  fid,=struct.unpack_from('<Q',frame)
  assert struct.unpack_from('<QII',frame,16)==(4,1,32792),'Envelope contract'
  tx,current_rx,channels,samples=struct.unpack_from('<QQII',frame,64)
  assert (channels,samples)==(1,8192),'matrix dimensions'
  if last is not None:
   assert fid==last+1,'frame ID discontinuity'
   assert (current_rx-rx)%(1<<64)==98304,'RX timestamp delta differs'
  else:first=fid;tx0=tx;rx0=current_rx
  last=fid;rx=current_rx;count+=1;total+=length;digest.update(frame)
assert count>0,'empty capture'
print(json.dumps(dict(path=path,complete=True,frames=count,samples=count*8192,raw_bytes=total,first_frame=first,last_frame=last,first_tx=str(tx0),first_rx=str(rx0),raw_sha256=digest.hexdigest(),rx_delta=98304,source_digest_comparison='unavailable for mid-stream recording window',sample_continuity='unverified')))
