#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fetch nine pinned lossless calibration recordings; verify every Git blob hash."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import sys
import urllib.request

manifest=Path(__file__).resolve().parent/'reference/salamander-source.json'
p=json.loads(manifest.read_text())
directory=Path(sys.argv[1] if len(sys.argv)>1 else 'out/piano-reference-cache')
directory.mkdir(parents=True,exist_ok=True)
def fetch(entry):
    path=directory/Path(entry['path']).name
    data=path.read_bytes() if path.exists() else urllib.request.urlopen(
        f'https://raw.githubusercontent.com/{p["repository"]}/{p["commit"]}/{entry["path"]}',timeout=60).read()
    assert len(data)==entry['size']
    assert hashlib.sha1(f'blob {len(data)}\0'.encode()+data).hexdigest()==entry['sha'], 'reference changed'
    assert data[:4]==b'fLaC', 'not lossless FLAC'
    path.write_bytes(data)
    return dict(file=path.name,sha256=hashlib.sha256(data).hexdigest(),git_blob_sha=entry['sha'])
with ThreadPoolExecutor(max_workers=3) as pool: rows=list(pool.map(fetch,p['files']))
(directory/'verified.json').write_text(json.dumps(rows,indent=2)+'\n')
print('Verified',len(rows),'original FLAC files in',directory)
