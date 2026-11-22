from pathlib import Path
import os,socket,struct,subprocess,time,unittest
BUILD=Path(os.environ['COOP_DYNAMIC_BUILD']);EXT='.dll' if os.name=='nt' else '.so';EXE='.exe' if os.name=='nt' else ''
IDENTITY=b''.join(x.ljust(64,b'\0') for x in [b'0.0.1',b'xcx-mods-service-2.0',b'xcx-de-2.0.0'])
def frame(t,b=b'',v=1):return struct.pack('!4sHHI',b'XCXC',v,t,len(b))+b
def exact(s,n):
 b=b''
 while len(b)<n:
  v=s.recv(n-len(b))
  if not v:raise EOFError()
  b+=v
 return b
class Provider:
 def __init__(self,case):
  self.p=subprocess.Popen([str(BUILD/('driver'+EXE)),str(BUILD/('provider'+EXT))],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1);case.addCleanup(self.close);self.seq=0;assert self.p.stdout.readline().startswith('ready xcx.coop.loopback.v1')
 def call(self,s):
  self.p.stdin.write(s+'\n');self.p.stdin.flush();return self.p.stdout.readline().strip()
 def submit(self,op,val=0):
  self.seq+=1;start=time.monotonic();out=self.call(f'submit {op} {self.seq} {val}');assert time.monotonic()-start<.1,out;return out
 def wait(self,text,timeout=8):
  until=time.monotonic()+timeout;last=''
  while time.monotonic()<until:
   last=self.call('status')
   if text in last:return last
   time.sleep(.01)
  raise AssertionError(last+' expected '+text)
 def result(self,timeout=8):
  until=time.monotonic()+timeout
  while time.monotonic()<until:
   out=self.call('poll')
   if not out.startswith('poll 2 '):return out
   time.sleep(.005)
  raise AssertionError('no completion')
 def close(self):
  if self.p.poll() is None:
   try:self.p.stdin.write('exit\n');self.p.stdin.flush()
   except BrokenPipeError:pass
  out,err=self.p.communicate(timeout=6)
  if getattr(self,'killed',False):return
  assert self.p.returncode==0,(out,err);assert not err,err
class Dynamic(unittest.TestCase):
 def setUp(self):
  with socket.socket() as s:s.bind(('127.0.0.1',0));self.port=s.getsockname()[1]
 def provider(self):return Provider(self)
 def listen(self,p):self.assertEqual(p.submit(1,self.port),'submit 0');self.assertIn(' 0 Listening',p.result());p.wait('Hosting/listening')
 def test_idle_has_no_sockets_and_no_listener(self):
  p=self.provider();p.wait('Idle/offline')
  with socket.socket() as s:s.bind(('127.0.0.1',self.port))
  if Path('/proc').exists():self.assertFalse(any(os.readlink(f).startswith('socket:') for f in Path(f'/proc/{p.p.pid}/fd').iterdir()))
 def test_host_join_leave_repeated_and_echo(self):
  h=self.provider();c=self.provider()
  for _ in range(3):
   self.listen(h);self.assertEqual(c.submit(2,self.port),'submit 0');self.assertIn(' 0 Handshake accepted',c.result());h.wait('Transport connected');c.wait('echo received=1');h.wait('test received=1');self.assertEqual(c.submit(3),'submit 0');self.assertIn(' 0 Session stopped',c.result());c.wait('Idle/offline');h.wait('Hosting/listening');h.submit(3);h.result();h.wait('Idle/offline')
 def test_party_invite_accept_decline(self):
  # Command 4 ("probe") is the general send: 1 test, 2 invite, 3 accept, 4 decline. The party state is in the status text.
  h=self.provider();c=self.provider();self.listen(h);self.assertEqual(c.submit(2,self.port),'submit 0');self.assertIn(' 0 Handshake accepted',c.result());h.wait('Transport connected');c.wait('party=none');h.wait('party=none')
  self.assertEqual(h.submit(4,9),'submit -1');self.assertEqual(h.submit(4,0),'submit -1')  # codes outside 1 to 4 are refused before acceptance
  self.assertEqual(h.submit(4,3),'submit 0');self.assertIn(' -1 No invite to accept',h.result())  # nothing to accept yet
  self.assertEqual(c.submit(4,2),'submit 0');self.assertIn(' 0 Invite queued',c.result());c.wait('party=invite-sent');h.wait('party=invite-received')
  self.assertEqual(c.submit(4,2),'submit 0');self.assertIn(' -1 ',c.result())  # one is already pending
  self.assertEqual(h.submit(4,4),'submit 0');self.assertIn(' 0 Decline queued',h.result());c.wait('party=invite-declined');h.wait('party=none')
  self.assertEqual(c.submit(4,2),'submit 0');self.assertIn(' 0 Invite queued',c.result());h.wait('party=invite-received')
  self.assertEqual(h.submit(4,3),'submit 0');self.assertIn(' 0 Accept queued',h.result());h.wait('party=in-party');c.wait('party=in-party')
  self.assertEqual(c.submit(4,2),'submit 0');self.assertIn(' -1 ',c.result())  # already in a party
 def test_conflict_refusal_and_recovery(self):
  h=self.provider();c=self.provider();c.submit(2,self.port);self.assertIn(' -3 ',c.result());c.wait('Failed');self.listen(h);c.submit(1,self.port);self.assertIn(' -3 ',c.result());c.submit(2,self.port);self.assertIn(' 0 Handshake accepted',c.result())
 def test_peer_protocol_host_game_mismatch(self):
  h=self.provider();self.listen(h)
  bad=[frame(1,IDENTITY,v=2),frame(1,IDENTITY.replace(b'xcx-mods-service-2.0',b'xcx-mods-service-9.0')),frame(1,IDENTITY.replace(b'xcx-de-2.0.0',b'xcx-de-9.0.0')),frame(1,b'x'*192),frame(9)]
  for b in bad:
   with socket.create_connection(('127.0.0.1',self.port),2) as s:
    s.sendall(b)
    try:self.assertEqual(s.recv(1),b'')
    except ConnectionResetError:pass
  c=self.provider();c.submit(2,self.port);self.assertIn(' 0 Handshake accepted',c.result())
 def test_queue_credits_types_and_stop_handshake(self):
  p=self.provider();self.assertEqual(p.call('invalid'),'invalid -1 -1 -1 -1')
  with socket.socket() as s:
   s.bind(('127.0.0.1',self.port));s.listen(1);self.assertEqual(p.submit(2,self.port),'submit 0');peer,_=s.accept()
   with peer:
    for _ in range(31):self.assertEqual(p.submit(3),'submit 0')
    self.assertEqual(p.submit(3),'submit 1');self.assertEqual(p.call('stop'),'stop 0');p.wait('Leaving/stopping');records=[p.result() for _ in range(32)];self.assertEqual(len({r.split()[2] for r in records}),32);self.assertTrue(all(' -3 ' in r for r in records));self.assertEqual(p.submit(3),'submit -3')
 def test_active_shutdown_and_unexpected_client_termination(self):
  h=self.provider();self.listen(h);c=self.provider();c.submit(2,self.port);c.result();c.wait('echo received=1');c.close();h.wait('Hosting/listening');c2=self.provider();c2.submit(2,self.port);c2.result();h.close();c2.wait('Failed')
 def test_raw_10000_messages_and_cloexec(self):
  h=self.provider();self.listen(h)
  with socket.create_connection(('127.0.0.1',self.port),2) as s:
   s.sendall(frame(1,IDENTITY));self.assertEqual(exact(s,204),frame(2,IDENTITY))
   if os.name!='nt':
    for f in Path(f'/proc/{h.p.pid}/fd').iterdir():
     if os.readlink(f).startswith('socket:'):
      flags=next(l.split()[1] for l in Path(f'/proc/{h.p.pid}/fdinfo/{f.name}').read_text().splitlines() if l.startswith('flags:'));self.assertTrue(int(flags,8)&os.O_CLOEXEC)
   for base in range(0,10000,100):
    b=b''.join(frame(3,struct.pack('!I',i)) for i in range(base,base+100));s.sendall(b);self.assertEqual(exact(s,len(b)),b''.join(frame(4,struct.pack('!I',i)) for i in range(base,base+100)))
   h.wait('test received=10000')

 def test_abrupt_peer_termination_and_reconnect(self):
  h=self.provider();self.listen(h);c=self.provider();c.submit(2,self.port);c.result();c.wait('echo received=1');c.killed=True;c.p.kill();c.p.communicate(timeout=3)
  h.wait('Hosting/listening');replacement=self.provider();replacement.submit(2,self.port);self.assertIn(' 0 Handshake accepted',replacement.result());replacement.wait('Transport connected')
 def test_stop_immediately_after_join_admission(self):
  p=self.provider();self.assertEqual(p.submit(2,self.port),'submit 0');self.assertEqual(p.call('stop'),'stop 0');self.assertEqual(p.submit(3),'submit -3')
