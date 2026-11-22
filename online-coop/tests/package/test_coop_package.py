"""Runs online-coop/lua through the production Lua runtime driver of the recomp native-UI feature (no game window).

    NATIVE_TEST_EXE=<xcx-recomp>/build/native_ui/regular/host python3 -B -m unittest online-coop/tests/package/test_coop_package.py -v

Build the driver with: python tests/native_ui/check.py
Skipped when NATIVE_TEST_EXE is not set.
"""
from pathlib import Path
import os, shutil, subprocess, tempfile, unittest

ROOT = Path(__file__).resolve().parents[2]
EXE = os.environ.get('NATIVE_TEST_EXE')


@unittest.skipUnless(EXE and Path(EXE).exists(), 'set NATIVE_TEST_EXE to the recomp native-UI Lua driver')
class CoopPackage(unittest.TestCase):
    def run_pkg(self, steps=(), unsupported=False, enabled=True):
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp); mods = t / 'mods'; settings = t / 'settings'; settings.mkdir()
            shutil.copytree(ROOT / 'lua', mods / 'coop')
            (mods / 'enabled.txt').write_text('coop\n' if enabled else '')
            env = os.environ.copy()
            if unsupported: env['NATIVE_TEST_UNSUPPORTED'] = '1'
            r = subprocess.run([EXE, str(mods), str(settings), *steps], env=env, capture_output=True, text=True, timeout=10)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr); self.assertEqual(r.stderr, '')
            return r.stdout

    def test_registers_all_native_features(self):
        out = self.run_pkg()
        # 7 = connect entry (1) + squad screen (2) + squad details screen (4)
        self.assertIn('features 7', out); self.assertNotIn(' error ', out); self.assertIn('coop closed bytes=0 callbacks=0', out)

    def test_native_options_need_the_transport(self):
        # This driver loads no native provider, so the provider-gated Host/Join buttons stay disabled and the native
        # options do nothing; the page says why. The provider-backed path is covered by tests/dynamic/check_lua.py.
        out = self.run_pkg(('close', 'native=2', 'native=3', 'pump=60'))
        self.assertIn('native 2 0', out); self.assertIn('native 3 0', out)
        self.assertIn('Co-op transport unavailable', out); self.assertNotIn(' error ', out)

    def test_other_options_do_nothing(self):
        out = self.run_pkg(('close', 'native=0', 'native=1', 'native=4', 'pump=40'))
        for o in ('native 0 0', 'native 1 0', 'native 4 0'): self.assertIn(o, out)
        self.assertNotIn('requested', out)

    def test_unsupported_build_keeps_the_mods_page_usable(self):
        out = self.run_pkg(unsupported=True)
        self.assertIn('features 0', out); self.assertNotIn(' error ', out)

    def test_disabled_registers_nothing(self):
        out = self.run_pkg(enabled=False)
        self.assertNotIn('features 7', out); self.assertNotIn('coop', out)


if __name__ == '__main__': unittest.main()
