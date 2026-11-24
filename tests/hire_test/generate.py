"""Extract the complete production dispatcher and its ID-copy helper unchanged.

Dependencies are controlled doubles; argument expectations come from original
56fc10/56ffbd, not from the reconstructed expression under test.
"""
from pathlib import Path
import hashlib
import json
import sys
import argparse

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('output', type=Path)
parser.add_argument('source', nargs='?', type=Path, default=root/'src/truck/helicopter.cpp')
parser.add_argument('--negative-control', action='store_true')
args = parser.parse_args()
source = args.source
text = source.read_text(encoding='utf-8')
def extract(signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        if text[end] == '{': depth += 1
        elif text[end] == '}': depth -= 1
        end += 1
    return text[start:end]
body = '\n\n'.join(extract(s) for s in [
    'static int Bc_Copy(', 'void __cdecl Copter_BoardNearby()'])
if args.negative_control:
    expected = 'int a = Bc_Copy(&car);'
    assert body.count(expected) == 1, 'Review negative control after dispatcher changes'
    body = body.replace(expected, 'int a = Bc_Copy(&id);')
template = Path(__file__).with_name('fixture.cpp.in').read_text(encoding='utf-8')
assert template.count('// PRODUCTION') == 1
out = args.output
out.write_text(template.replace('// PRODUCTION', body), encoding='utf-8')
out.with_suffix('.json').write_text(json.dumps(dict(source=str(source.resolve()),
    source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(), negative_control=args.negative_control,
    extracted_sha256=hashlib.sha256(body.encode()).hexdigest()), indent=2))
