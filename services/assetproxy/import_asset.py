"""Import a known asset by ID without replacing an existing archived version."""
import argparse
import hashlib
import json
import re
import shutil
import tempfile
from pathlib import Path
from app import CACHE, MAX_BYTES, asset_lock

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('id')
    parser.add_argument('file', type=Path)
    parser.add_argument('--mime', default='application/octet-stream')
    args = parser.parse_args()
    if not re.fullmatch(r'[1-9][0-9]{0,18}', args.id):
        parser.error('Expected a positive numeric asset ID')
    payload = args.file.read_bytes()
    if not payload or len(payload) > MAX_BYTES:
        parser.error('Empty or oversized asset')
    CACHE.mkdir(parents=True, exist_ok=True)
    with asset_lock(args.id):
        if (CACHE / args.id).exists():
            raise SystemExit('Asset already archived; existing version kept.')
        temporary = Path(tempfile.mkdtemp(prefix='.import-', dir=CACHE))
        try:
            (temporary / 'body').write_bytes(payload)
            (temporary / 'meta.json').write_text(json.dumps({'mime': args.mime, 'bytes': len(payload),
                'sha256': hashlib.sha256(payload).hexdigest()}), encoding='utf-8')
            temporary.rename(CACHE / args.id)
        finally:
            if temporary.exists():
                shutil.rmtree(temporary)
    print('Archived asset', args.id, len(payload), 'bytes')
