"""Build headless controller-keyboard checks against an existing native build (no game window or saves).

Run from the repository root: python3 tests/controller_keyboard_check.py --build build/controller-keyboard-candidate
The build must match the current source. Test objects/executable stay under ignored build/.
"""
import argparse
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--out', type=Path, default=Path('build/controller-keyboard-tests'))
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
ninja = ['ninja', '-f', str(a.build / 'build.ninja')]
replacements = {}
for rel, src in [('ui/menu', Path('tests/controller_keyboard_check.cpp')),
                 ('gfx_window_probe', a.out / 'probe.c')]:
    if rel == 'gfx_window_probe':
        src.write_text('extern int controller_keyboard_checks(void);\nint probe_window(void) { return controller_keyboard_checks(); }\n')
    obj = a.build / 'obj' / 'runtime' / (rel + '.o')
    new = a.out / (Path(rel).name + '.o')
    cmd = shlex.split(subprocess.check_output(ninja + ['-t', 'commands', str(obj)], text=True).splitlines()[-1])
    original = 'runtime/' + rel + ('.cpp' if rel == 'ui/menu' else '.c')
    cmd = [str(src) if x == original else str(new) if x == str(obj) else x for x in cmd]
    # Dependency files also belong to the private test output.
    cmd = [str(new) + '.d' if x == str(obj) + '.d' else x for x in cmd]
    subprocess.run(cmd, check=True)
    replacements[str(obj)] = str(new)
lines = (a.build / 'build.ninja').read_text().splitlines()
line = next(x for x in lines if x.startswith('build ' + str(a.build / 'xcx') + ': link '))
objects = shlex.split(line.split(': link ', 1)[1])
(a.out / 'xcx.rsp').write_text('\n'.join(replacements.get(x, x) for x in objects))
cmd = shlex.split(subprocess.check_output(ninja + ['-t', 'commands', str(a.build / 'xcx')], text=True).splitlines()[-1])
cmd = [x.replace(str(a.build / 'xcx'), str(a.out / 'xcx')) for x in cmd]
cmd += ['-Wl,--wrap=input_pad_read', '-Wl,--wrap=ikb_foreground']
subprocess.run(cmd, check=True)
print(a.out / 'xcx', '--probe-window (headless tests, not the normal window probe)')
