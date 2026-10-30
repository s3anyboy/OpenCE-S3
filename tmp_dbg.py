import sys, struct, subprocess, tempfile, os
sys.path.insert(0, r'E:\GitHub\OpenCE-S3\tools')
import test_shadowrun_caches as t
from pathlib import Path

d = Path(tempfile.mkdtemp())
src = d / 'driver.c'
src.write_text(t.DRIVER)
clang = t.find_clang()
out = d / 'driver.exe'
cmd = [clang, '-std=c99', '-O1', *t.STRICT_FLAGS, *t.UB_TRAP_FLAGS,
       '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '-D_CRT_SECURE_NO_WARNINGS',
       f'-I{t.MODULE.parent}', str(src), str(t.MODULE), '-o', str(out)]
r = subprocess.run(cmd, capture_output=True, text=True)
print(r.returncode, r.stdout[-2000:], r.stderr[-2000:])
for addr in (t.SHADOWRUN_TAG_DATA, t.RECOMP_TAG_DATA, t.XBOX_TAG_CACHE):
    p = d / 'x.bin'
    p.write_bytes(t.tag_data(addr))
    c = subprocess.run([str(out), 'shadowrun', str(p), str(addr)], capture_output=True, text=True)
    print(hex(addr), c.returncode, c.stdout, c.stderr)
