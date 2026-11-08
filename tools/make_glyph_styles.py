"""Reproduce only licensed Kenney glyph tiles; no guest resource pixels or runtime game decoding."""
import argparse
from pathlib import Path
import hashlib
import json
from compression import zstd
from PIL import Image


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    root=Path(__file__).resolve().parents[1]
    source=root/'assets/appendage-glyph/styles'
    manifest=json.loads((source/'provenance.json').read_text())
    blob=bytearray();rows=[]
    for asset in manifest['assets']:
        path=source/asset['file'];assert hashlib.sha256(path.read_bytes()).hexdigest()==asset['sha256']
        image=Image.open(path).convert('RGBA');assert image.size==(64,64)
        packed=zstd.compress(image.tobytes(),level=19)
        rows.append((asset,len(blob),len(packed)));blob.extend(packed)
    assert len(rows)<=64
    lines=['// Licensed Kenney CC0 derivatives. Reproduce with tools/make_glyph_styles.py.',
           '// Provenance: assets/appendage-glyph/styles/provenance.json. No game pixels.',
           'static const struct { int style, input; unsigned offset, length; const char *name; } glyph_arts[] = {']
    for asset,off,size in rows:lines.append(f'    {{GLYPH_{asset["style"]}, {asset["input"]}, {off}, {size}, "{asset["file"]}"}},')
    lines+=['};','static const unsigned char glyph_art_bytes[] = {']
    for i in range(0,len(blob),20):lines.append('    '+','.join(f'0x{x:02x}' for x in blob[i:i+20])+',')
    lines+=['};','']
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text('\n'.join(lines))
    print(len(rows),'licensed tiles;',len(blob),'compressed bytes')


if __name__=='__main__':main()
