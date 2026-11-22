from pathlib import Path
import argparse,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];p=argparse.ArgumentParser();p.add_argument('--sanitize',action='store_true');p.add_argument('--cc',default='clang');p.add_argument('--cxx',default='clang++');a=p.parse_args();build=ROOT.parent/'build/online-coop'/('dynamic-sanitize' if a.sanitize else 'dynamic');build.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env.update(ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',COOP_DYNAMIC_BUILD=str(build))
def run(cmd):subprocess.run(list(map(str,cmd)),cwd=ROOT,env=env,check=True)
run([sys.executable,'tools/build_provider.py','--out',build,'--cxx',a.cxx,*(['--sanitize'] if a.sanitize else [])])
run([a.cc,'-std=c11','-O1','-g','-Wall','-Wextra','-I../runtime',*(['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if a.sanitize else []),'tests/dynamic/driver.c',*([] if os.name=='nt' else ['-ldl']),'-o',build/('driver.exe' if os.name=='nt' else 'driver')])
run([sys.executable,'-B','-m','unittest','discover','-s','tests/dynamic','-p','test_*.py','-v'])
