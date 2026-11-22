"""Use the real host loader/Lua runtime against the Co-op package, with disposable files."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,socket,subprocess,tempfile,sys
ROOT=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser();p.add_argument('--host',type=Path,default=ROOT.parent);p.add_argument('--sanitize',action='store_true');a=p.parse_args();host=a.host.resolve();out=ROOT.parent/'build/online-coop'/('dynamic-sanitize' if a.sanitize else 'dynamic');out.mkdir(exist_ok=True,parents=True);win=os.name=='nt';exe='.exe' if win else ''
flags=['-std=gnu11','-O1','-g','-Wall','-Wextra','-DXCX_MOD_TESTING','-I'+str(host/'runtime'),'-I'+str(host/'build/lua-src')]
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
lua=host/'build/lua-src';core=[x for x in sorted(lua.glob('*.c')) if x.name not in ('lua.c','luac.c','linit.c') and (not x.name.endswith('lib.c') or x.name in ('lauxlib.c','lbaselib.c','lstrlib.c','ltablib.c','lmathlib.c'))]
source=[host/'runtime'/n for n in ['mod_provider.c','mod_provider_platform.c','mod_package.c','mod_settings.c','mod_menu.c','mod_native_ui.c','mod_runtime.c','sha256.c']];env=os.environ.copy();env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
io=host/'tests/mods/settings_io.c'
if win:
 io=out/'settings_stub.c';io.write_text('#include <stddef.h>\nvoid plat_file_mkdir_p(const char*p){(void)p;} int plat_file_write_atomic(const char*p,const void*d,size_t n){(void)p;(void)d;(void)n;return 0;}\n')
subprocess.run(['clang',*flags,ROOT/'tests/dynamic/lua_driver.c',*source,io,*core,*([] if win else ['-ldl','-lm']),'-o',out/('lua-host'+exe)],env=env,check=True)
with tempfile.TemporaryDirectory(dir=out) as tmp:
 t=Path(tmp);mods=t/'mods';shutil.copytree(out/'mods',mods);settings=t/'settings';settings.mkdir()
 with socket.socket() as reserve:reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
 (settings/'coop.ini').write_text(f'version=1\nport=i:{port}\n')
 def run(*steps,extra=None):
  e=env.copy();e.update(extra or {});r=subprocess.run([str(out/('lua-host'+exe)),str(mods),str(settings),*steps],env=e,capture_output=True,text=True,timeout=12);assert r.returncode==0,(r.stdout,r.stderr);assert not r.stderr,r.stderr;assert 'coop closed bytes=0 callbacks=0' in r.stdout,r.stdout;return r.stdout
 for state in ['absent','disabled','enabled']:
  (mods/'enabled.txt').write_text('coop\n' if state!='absent' else '')
  (mods/'providers-enabled.txt').write_text('coop:coop.native\n' if state=='enabled' else '')
  if state=='absent':
   r=subprocess.run([str(out/('lua-host'+exe)),str(mods),str(settings)],env=env,capture_output=True,text=True);assert r.returncode==0;assert 'idle provider ready' not in r.stdout
  else:
   text=run('pump=100');assert ('idle provider ready' in text)==(state=='enabled'),text
 text=run('pump=120','intent=coop:host','pump=100','close','pump=20','open','intent=coop:leave','pump=100');assert 'Listening on loopback' in text,text;assert 'Session stopped' in text,text;assert 'pending=0' in text
 text=run('pump=120','intent=coop:host','pump=60',extra={'MOD_FAIL_ID':'coop','MOD_FAIL_PHASE':'action','MOD_FAIL_AFTER':'0'});assert 'coop error action' in text,text;assert 'provider destroyed before Lua teardown' in text
 script=mods/'coop/main.lua';original=script.read_text();script.write_text(original.replace("SERVICE='xcx.coop.loopback.v1'","SERVICE='xcx.coop.loopback.v9'"))
 text=run('pump=120','intent=coop:host','pump=100');assert 'intent coop host 0' in text,text;assert 'Co-op transport unavailable' in text,text
 script.write_text(original)
 script=mods/'coop/main.lua';script.write_text(script.read_text().replace("host.log('coop: transport '..e.message)","if string.sub(e.message,1,7)=='Hosting' then error('active owner fault') end"))
 text=run('pump=120','intent=coop:host','pump=250');assert 'active owner fault' in text,text
 with socket.socket() as probe:probe.bind(('127.0.0.1',port))
 print('production Lua: absent/disabled/idle, Host, overlay close/reopen, Leave, owner OOM and teardown passed')
