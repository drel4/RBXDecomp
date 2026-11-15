"""Permanent, ID-only Roblox asset archive with private upstream authentication."""
import contextlib
import hashlib
import gzip
import http.client
import http.cookiejar
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import urllib.error
import urllib.parse
import urllib.request

from flask import Flask, Response, abort, request, send_file

app = Flask(__name__)
CACHE = Path(os.environ.get('ASSET_PROXY_DATA', '/data/assets')).resolve()
MAX_BYTES = 64 * 1024 * 1024


class AssetRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        target = urllib.parse.urlsplit(newurl)
        host = (target.hostname or '').lower()
        if target.scheme != 'https' or target.username or target.password or target.port not in (None, 443):
            raise ValueError('Invalid upstream redirect')
        if not any(host == domain or host.endswith('.' + domain) for domain in ('roblox.com', 'rbxcdn.com')):
            raise ValueError('Unexpected upstream host')
        redirected = super().redirect_request(req, fp, code, msg, headers, newurl)
        if redirected is not None:
            redirected.remove_header('Cookie')
            redirected.remove_header('Authorization')
        return redirected


def upstream_cookie():
    path = os.environ.get('ASSET_PROXY_COOKIE_FILE')
    if not path or not Path(path).is_file():
        return None
    jar = http.cookiejar.MozillaCookieJar(path)
    jar.load(ignore_discard=True, ignore_expires=False)
    for cookie in jar:
        domain = cookie.domain.lstrip('.').lower()
        if cookie.name == '.ROBLOSECURITY' and domain == 'roblox.com' and not cookie.is_expired():
            if '\r' in cookie.value or '\n' in cookie.value:
                raise ValueError('Invalid upstream cookie')
            return '.ROBLOSECURITY=' + cookie.value
    return None


def fetch(asset_id, directory):
    url = 'https://assetdelivery.roblox.com/v1/asset/?id=' + asset_id
    # Match the legacy client. Public assets must not depend on an account
    # cookie (or be affected by a revoked/otherwise restricted account).
    upstream = urllib.request.Request(url, headers={'User-Agent': 'Roblox', 'Accept-Encoding': 'identity'})
    opener = urllib.request.build_opener(AssetRedirects())
    try:
        response = opener.open(upstream, timeout=20)
    except urllib.error.HTTPError as error:
        if error.code not in (401, 403, 409):
            raise
        cookie = upstream_cookie()
        if not cookie:
            raise
        error.close()
        upstream.add_header('Cookie', cookie)
        response = opener.open(upstream, timeout=20)
    with response:
        if response.status != 200:
            raise ValueError('Upstream did not return an asset')
        mime = response.headers.get_content_type()
        encoding = response.headers.get('Content-Encoding', 'identity').strip().lower()
        if mime == 'text/html' or encoding not in ('', 'identity', 'gzip'):
            raise ValueError('Unexpected upstream response')
        expected = response.headers.get('Content-Length')
        if expected and int(expected) > MAX_BYTES:
            raise ValueError('Asset is too large')
        total = 0
        digest = hashlib.sha256()
        source = gzip.GzipFile(fileobj=response) if encoding == 'gzip' else response
        with (directory / 'body').open('wb') as output:
            while True:
                chunk = source.read(64 * 1024)
                if not chunk:
                    break
                total += len(chunk)
                if total > MAX_BYTES:
                    raise ValueError('Asset is too large')
                output.write(chunk)
                digest.update(chunk)
        if not total or (encoding != 'gzip' and expected and int(expected) != total):
            raise ValueError('Empty or incomplete asset')
        with (directory / 'body').open('rb') as body:
            prefix = body.read(512).lstrip().lower()
        if prefix.startswith((b'<!doctype html', b'<html', b'{"errors"')):
            raise ValueError('Upstream returned an error document')
        (directory / 'meta.json').write_text(json.dumps({'mime': mime, 'bytes': total, 'sha256': digest.hexdigest()}), encoding='utf-8')


@contextlib.contextmanager
def asset_lock(asset_id):
    # Cross-process locking; lock stripes bound file count and work with gunicorn.
    import fcntl
    locks = CACHE / '.locks'
    locks.mkdir(parents=True, exist_ok=True)
    with (locks / str(int(asset_id) % 256)).open('a+b') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(lock, fcntl.LOCK_UN)


@app.get('/health')
def health():
    return {'ok': True}


@app.route('/<asset_id>', methods=['GET', 'HEAD'])
def asset(asset_id):
    if not re.fullmatch(r'[1-9][0-9]{0,18}', asset_id):
        abort(404)
    CACHE.mkdir(parents=True, exist_ok=True)
    saved = CACHE / asset_id
    hit = saved.is_dir()
    if not hit:
        with asset_lock(asset_id):
            if not saved.is_dir():
                temporary = Path(tempfile.mkdtemp(prefix='.fetch-', dir=CACHE))
                try:
                    fetch(asset_id, temporary)
                    temporary.rename(saved)
                except urllib.error.HTTPError as error:
                    status = 403 if error.code in (401, 403, 409) else 404 if error.code in (400, 404, 410) else 502
                    return Response('Asset unavailable.\n', status=status,
                                    headers={'Cache-Control': 'no-store', 'X-Asset-Upstream-Status': str(error.code)}, mimetype='text/plain')
                except (OSError, ValueError, EOFError, http.client.HTTPException, urllib.error.URLError):
                    app.logger.warning('Asset %s could not be archived', asset_id)
                    return Response('Asset download failed. Try again later.\n', status=502,
                                    headers={'Cache-Control': 'no-store'}, mimetype='text/plain')
                finally:
                    if temporary.exists():
                        shutil.rmtree(temporary)
            else:
                hit = True
    meta = json.loads((saved / 'meta.json').read_text(encoding='utf-8'))
    response = send_file(saved / 'body', mimetype=meta['mime'], conditional=True, etag=meta['sha256'], max_age=31536000)
    response.headers['Cache-Control'] = 'public, max-age=31536000, immutable'
    response.headers['X-Asset-Cache'] = 'HIT' if hit else 'MISS'
    response.headers['X-Content-Type-Options'] = 'nosniff'
    return response
