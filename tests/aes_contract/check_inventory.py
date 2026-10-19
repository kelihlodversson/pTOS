"""Require an explicit ownership classification for every legacy AES case."""
from pathlib import Path
import re

source = Path('aes/gemsuper.c').read_text()
document = Path('doc/aes-x32.txt').read_text()
operations = set(re.findall(r'case ([A-Z][A-Z0-9_]+):', source))
rows = re.findall(r'^    ([A-Z][A-Z0-9_]+)\s+(registry|message|event|desktop|draw|local|mixed)\s+', document, re.M)
assert len(rows) == len(set(name for name, _ in rows)), 'duplicate operation'
classified = {name for name, _ in rows}
assert operations == classified, (sorted(operations - classified), sorted(classified - operations))
print('AES ownership inventory: %d operations classified' % len(operations))
