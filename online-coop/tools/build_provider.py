"""Build only the co-op native library and assemble a restart-required source/native package."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,default=ROOT.parent/'build/online-coop/dynamic');p.add_argument('--cxx',default='clang++');p.add_argument('--build-id',default='loopback1');p.add_argument('--sanitize',action='store_true');a=p.parse_args()
if not a.build_id or len(a.build_id)>32 or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789.-_' for c in a.build_id):p.error('invalid build ID')
out=a.out.resolve();out.mkdir(parents=True,exist_ok=True);win=sys.platform=='win32';ext='.dll' if win else '.so';lib=out/('provider'+ext)
flags=['-std=c++17','-O1','-g','-Wall','-Wextra','-Wpedantic','-fvisibility=hidden','-Iinclude','-Isrc','-Iprovider','-I'+str(ROOT.parent/'runtime'),'-DCOOP_PROVIDER_BUILD="'+a.build_id+'"']
if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
if win:link=['-static','-lws2_32']
else:link=['-fPIC','-pthread','-Wl,--version-script=provider/exports.map','-Wl,--exclude-libs,ALL','-nodefaultlibs','-Wl,-Bstatic','-lstdc++','-lgcc_eh','-lgcc','-Wl,-Bdynamic','-lc']
subprocess.run([a.cxx,*flags,'-shared','src/coop.cpp','provider/provider.cpp',*link,'-o',str(lib)],cwd=ROOT,check=True)
pkg=out/'mods/coop';shutil.copytree(ROOT/'lua',pkg,dirs_exist_ok=True);dest=pkg/'native'/('windows-x86_64' if win else 'linux-x86_64')/('provider'+ext);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(lib,dest);h=hashlib.sha256(lib.read_bytes()).hexdigest()
(pkg/'provider.ini').write_text(f'schema=2\nid=coop.native\nowner=coop\nabi=2.0\nrequired=1\nhost=xcx-mods-service-2.0\ngame=2.0.0\n'+('windows' if win else 'linux')+'-sha256='+h+'\n')
(out/'mods/enabled.txt').write_text('coop\n');(out/'mods/providers-enabled.txt').write_text('coop:coop.native\n');(out/'mods/catalog.txt').write_text('coop\n')
(out/'identity.json').write_text(json.dumps({'build':a.build_id,'library_sha256':h,'platform':sys.platform,'abi':'2.0','service':'xcx.coop.loopback.v1','sanitized':a.sanitize},indent=2)+'\n');print(lib,h)
