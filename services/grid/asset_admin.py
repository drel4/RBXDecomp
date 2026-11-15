"""HTTPS owner-only asset archive configuration; secrets never enter the DB."""
import http.cookiejar
import io
import os
from pathlib import Path
import re
import tempfile
import urllib.error
import urllib.request
from flask import abort, redirect, render_template, request, session
from i18n import translate as _


def cookie_from_upload(payload):
    jar = http.cookiejar.MozillaCookieJar()
    jar._really_load(io.StringIO(payload.decode('utf-8-sig'), newline=None), '<upload>', True, False)
    for cookie in jar:
        if cookie.name == '.ROBLOSECURITY' and cookie.domain.lstrip('.').lower() == 'roblox.com' and not cookie.is_expired():
            if not cookie.value or '\r' in cookie.value or '\n' in cookie.value:
                break
            return cookie
    raise ValueError('No valid Roblox cookie')


def install(grid):
    @grid.app.route('/manage/assets', methods=['GET', 'POST'])
    def manage_assets():
        if request.method == 'GET' and not session.get('admin'):
            return redirect('/admin/login')
        grid.admin()
        secret = Path(os.environ.get('GRID_ASSET_PROXY_SECRETS', '/assetproxy-secrets')) / 'cookies.txt'
        result = None
        message = None
        asset_id = '1029523'
        if request.method == 'POST':
            action = request.form.get('action')
            if action == 'cookies':
                upload = request.files.get('cookies')
                if not upload:
                    abort(400, _('Choose a Netscape cookies.txt file.'))
                payload = upload.read(512 * 1024 + 1)
                try:
                    if len(payload) > 512 * 1024:
                        raise ValueError('Too large')
                    cookie = cookie_from_upload(payload)
                except (ValueError, http.cookiejar.LoadError, UnicodeError):
                    abort(400, _('No valid Roblox login cookie was found. The existing cookie was kept.'))
                secret.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
                fd, name = tempfile.mkstemp(prefix='.cookies-', dir=secret.parent)
                os.close(fd)
                try:
                    jar = http.cookiejar.MozillaCookieJar(name)
                    jar.set_cookie(cookie)
                    jar.save(ignore_discard=True, ignore_expires=False)
                    os.chmod(name, 0o600)
                    os.replace(name, secret)
                finally:
                    Path(name).unlink(missing_ok=True)
                message = _('Roblox login cookie replaced. New downloads use it immediately.')
            elif action == 'test':
                asset_id = request.form.get('asset_id', '').strip()
                if not re.fullmatch(r'[1-9][0-9]{0,18}', asset_id):
                    abort(400, _('Enter a positive numeric asset ID.'))
                try:
                    with urllib.request.urlopen('http://127.0.0.1:4381/' + asset_id, timeout=65) as response:
                        response.read(1)
                        result = dict(id=asset_id, size=response.headers.get('Content-Length', '?'),
                                      cached=response.headers.get('X-Asset-Cache') == 'HIT')
                    message = _('Download succeeded.')
                except urllib.error.HTTPError as error:
                    if error.headers.get('X-Asset-Upstream-Status') in ('401', '403', '409'):
                        message = _('Roblox rejected authentication or access. Replace the cookie and try again.')
                    else:
                        message = _('The asset could not be downloaded. It may be unavailable or inaccessible to this account.')
                except (OSError, urllib.error.URLError):
                    message = _('The asset proxy is unavailable. Try again later.')
            else:
                abort(400)
        return render_template('assets.html', cookie_configured=secret.is_file(), result=result,
                               message=message, asset_id=asset_id)
