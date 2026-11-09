"""Linux prototype checks; --gpu-build additionally links/tests an existing isolated candidate's objects."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(command, env=None, log=None):
    result = subprocess.run([str(x) for x in command], cwd=ROOT, env=env,
                            capture_output=True, text=True, timeout=120)
    if log:
        log.write_text(result.stdout + result.stderr)
    if result.returncode:
        print(result.stdout + result.stderr)
        raise RuntimeError(f'Failed ({result.returncode}): {command[0]}')
    print(result.stdout.strip() if not log else f'{log.name}: passed')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--gpu-build', type=Path)
    p.add_argument('--gpu-compile-only', action='store_true', help='With --gpu-build, prepare fixtures without starting GPU/SDL tests')
    p.add_argument('--cc', default='clang')
    p.add_argument('--out', type=Path, default=ROOT/'build/appendage-tests',
                   help='Separate fixture output directory; use a new path to preserve earlier evidence')
    args = p.parse_args()
    if sys.platform != 'linux':
        raise SystemExit('These POSIX fixtures require Linux; Windows runtime acceptance remains separate.')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith(('XCX_', 'LARECOMP_'))}
    env['XCX_APPENDAGE_GLYPH'] = '1'
    control = out / 'control'
    control.write_text('1')
    env['XCX_APPENDAGE_GLYPH_CONTROL'] = str(control)
    (out / 'layout.h').write_text('#define MOD_MAIN_BASE 0x500000000000ull\n')
    unit = out / 'unit'
    run([args.cc, '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined',
         '-I'+str(out), '-I'+str(ROOT/'runtime'), ROOT/'tests/appendage_glyph_test.c', ROOT/'runtime/input_glyphs.c', ROOT/'runtime/glyph_art.c', ROOT/'runtime/input_map.c', '-lzstd', '-lm', '-o', unit])
    policy = out/'policy'
    run([args.cc, '-std=gnu11', '-O1', '-g', '-fsanitize=address,undefined', '-I'+str(ROOT/'runtime'), ROOT/'tests/input_glyphs_test.c', ROOT/'runtime/input_glyphs.c', ROOT/'runtime/glyph_art.c', ROOT/'runtime/input_map.c', '-lzstd', '-lm', '-o', policy])
    run([policy], env, out/'policy.log')
    run([sys.executable, ROOT/'tools/make_glyph_styles.py', '--out', out/'styles.inc'])
    assert (out/'styles.inc').read_bytes() == (ROOT/'runtime/glyph_art.inc').read_bytes()
    run([unit, 'enabled'], env, out/'unit-enabled.log')
    run([unit, 'guides'], env, out/'unit-guides.log')
    run([unit, 'resources'], env, out/'unit-resources.log')
    run([unit, 'disabled'], dict(env, XCX_APPENDAGE_GLYPH='0'), out/'unit-disabled.log')
    run([sys.executable, ROOT/'tools/make_appendage_glyph.py', '--out', out/'pixels.inc'])
    assert (out/'pixels.inc').read_bytes() == (ROOT/'runtime/appendage_r1.inc').read_bytes()
    sys.path.insert(0, str(ROOT/'tools'))
    from make_appendage_glyph import pixels
    from PIL import Image
    asset = ROOT/'assets/appendage-glyph/playstation_trigger_r1.png'
    surface = pixels(asset)
    assert surface.crop((384,64,448,128)).tobytes() == Image.open(asset).convert('RGBA').tobytes()
    assert all(not any(surface.getpixel((x,y))) for y in range(192) for x in range(512)
               if not (384 <= x < 448 and 64 <= y < 128))
    assert hashlib.sha256(surface.tobytes()).hexdigest() == '7b8c592420373543f21fdcf90aa1fc8e7c39712d445c235fb31fa1c01d994ab0'
    spec = importlib.util.spec_from_file_location('appendage', ROOT/'recomp/appendage.py')
    hooks = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(hooks)
    class Nso:
        build_id = hooks.BUILD
        source_sha256 = hooks.SHA256
    class Module:
        name = 'main'
        base = 0x7000000000
        nso = Nso()
        def word(self, address):
            return hooks.WORDS[address-self.base]
    module = Module()
    assert hooks.verified(module)
    module.nso.source_sha256 = 'wrong'
    try:
        hooks.verified(module)
        raise AssertionError('Hash mismatch accepted')
    except ValueError:
        pass
    module.nso.source_sha256 = hooks.SHA256
    module.word = lambda address: 0
    try:
        hooks.verified(module)
        raise AssertionError('Instruction mismatch accepted')
    except ValueError:
        pass
    module.nso.build_id = 'unsupported'
    assert not hooks.verified(module)
    assert hooks.call(module, module.base+0x60f68c, module.base+0x917fc0).startswith('appendage_glyph_attach')
    assert 'appendage_glyph_bind' in hooks.call(module, module.base+0xa27678, module.base+0xa47720)
    for site in (0x5a665c,0x5c6ed4):
        assert 'appendage_glyph_guide' not in hooks.call(module,module.base+site,module.base+0x5da974)
    assert 'appendage_glyph_lookup_attach' in hooks.call(module,module.base+0x55ff1c,module.base+0xa1cfc8)
    assert 'appendage_glyph_' not in hooks.call(module,module.base+0x55ff20,module.base+0xa1cfc8)
    assert hooks.entry(module, module.base+0xa2fac0) and hooks.entry(module, module.base+0xa2fcd0)
    assert not hooks.entry(module, module.base+0xa2fac4)
    print('Licensed pixels, exact hook identity/rejection and unrelated entry checks passed')
    if not args.gpu_build:
        return
    build = args.gpu_build.resolve()
    obj = out/'gpu.o'
    run([args.cc, '-std=gnu11', '-O1', '-g', '-I'+str(ROOT/'runtime'),
         '-I'+str(ROOT/'generated/upd'), '-I'+str(build), '-I'+str(ROOT/'toolchain/vma'),
         '-I'+str(ROOT/'runtime/third_party/imgui'), '-c', ROOT/'tests/appendage_glyph_gpu_test.c', '-o', obj])
    objects = sorted(x for x in (build/'obj').rglob('*.o') if x.name != 'vk_exec_draw.o')
    assert objects and any(x.name == 'appendage_glyph.o' for x in objects)
    rsp = out/'gpu.rsp'
    rsp.write_text('\n'.join('"'+str(x)+'"' for x in objects+[obj]))
    gpu = out/'gpu'
    run([args.cc, '-fuse-ld=lld', '-no-pie', '-Wl,-z,noexecstack', '-Wl,--wrap=main', '-Wl,--wrap=sha256',
         '@'+str(rsp), '-lvulkan', '-lSDL3', '-lopus', '-lvpx', '-lzstd', '-l:libstdc++.a', '-lm',
         '-lpthread', '-ldl', '-Wl,-rpath,'+str(build), '-o', gpu])
    sdl_obj = out/'sdl.o'
    run([args.cc, '-std=gnu11', '-O1', '-g', '-I'+str(ROOT/'runtime'), '-c', ROOT/'tests/input_glyphs_sdl_test.c', '-o', sdl_obj])
    sdl_objects = sorted(x for x in (build/'obj').rglob('*.o') if x.name != 'input_sdl.o')
    sdl_rsp = out/'sdl.rsp'
    sdl_rsp.write_text('\n'.join('"'+str(x)+'"' for x in sdl_objects+[sdl_obj]))
    sdl = out/'sdl'
    run([args.cc, '-fuse-ld=lld', '-no-pie', '-Wl,--wrap=main', '@'+str(sdl_rsp), '-lvulkan', '-lSDL3', '-lopus', '-lvpx', '-lzstd', '-l:libstdc++.a', '-lm', '-lpthread', '-ldl', '-Wl,-rpath,'+str(build), '-o', sdl])
    if args.gpu_compile_only:
        print('GPU and SDL fixtures compiled only; no GPU/SDL tests started.')
        return
    run([sdl],env,out/'sdl.log')
    for stage in ('normal', 'pixels', 'image', 'upload', 'view', 'source'):
        stage_env = dict(env, XCX_VK_VALIDATION='1', GLYPH_TEST_CONFIG=str(out/f'settings-{stage}.ini'))
        if stage != 'normal':
            stage_env['XCX_APPENDAGE_GLYPH_FAIL'] = stage
        run([gpu, stage], stage_env, out/f'gpu-{stage}.log')
    print('Read each GPU log for validation availability; zero reported errors alone does not establish layer coverage.')


if __name__ == '__main__':
    main()
