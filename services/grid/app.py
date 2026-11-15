"""Blox catalogue and original-engine job coordinator. One WSGI process only."""
from i18n import translate as _
import base64
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import secrets
import sqlite3
import sys
import subprocess
import threading
import time
from concurrent.futures import ThreadPoolExecutor

from defusedxml import ElementTree
from flask import (Flask, abort, flash, g, has_app_context, jsonify, redirect, render_template,
                   request, send_file, session, url_for)
from werkzeug.middleware.proxy_fix import ProxyFix
from werkzeug.security import check_password_hash
from flask.sessions import SecureCookieSessionInterface


class SchemeSessions(SecureCookieSessionInterface):
    def get_cookie_name(self, app):
        return 'session' if request.is_secure else 'guest_http'

    def get_cookie_secure(self, app):
        return request.is_secure

DATA = Path(os.environ.get('GRID_DATA', './data')).resolve()
for folder in ('places', 'thumbs', 'jobs'):
    (DATA / folder).mkdir(parents=True, exist_ok=True)
app = Flask(__name__)
app.session_interface = SchemeSessions()
app.config.update(SECRET_KEY=os.environ['GRID_SECRET'], MAX_CONTENT_LENGTH=34*1024*1024,
                  MAX_FORM_MEMORY_SIZE=256*1024, MAX_FORM_PARTS=16,
                  SESSION_COOKIE_HTTPONLY=True, SESSION_COOKIE_SAMESITE='Lax',
                  SESSION_COOKIE_SECURE=os.environ.get('GRID_INSECURE') != '1')
app.wsgi_app = ProxyFix(app.wsgi_app, x_for=1, x_proto=1, x_host=0)
SITE = os.environ.get('GRID_SITE', 'https://blox.ralsei.tech')
API = os.environ.get('GRID_API', 'https://api.grid.blox.ralsei.tech')
GAME_HOST = os.environ.get('GRID_GAME_HOST', 'play.grid.blox.ralsei.tech')
RUNTIME = Path(os.environ.get('GRID_RUNTIME', '/runtime')).resolve()
jobs = ThreadPoolExecutor(max_workers=1, thread_name_prefix='thumbnail')
rooms, room_lock = {}, threading.Lock()
content_lock = threading.RLock()
place_versions = {}


def db():
    if 'db' not in g:
        g.db = sqlite3.connect(DATA/'grid.sqlite', timeout=15)
        g.db.row_factory = sqlite3.Row
    return g.db


@app.teardown_appcontext
def close_db(_error):
    if 'db' in g:
        g.db.close()


with app.app_context():
    db().executescript('''
        PRAGMA journal_mode=WAL;
        CREATE TABLE IF NOT EXISTS places (
          id INTEGER PRIMARY KEY, title TEXT NOT NULL, description TEXT NOT NULL,
          sha TEXT NOT NULL, published INTEGER NOT NULL DEFAULT 0,
          thumb TEXT NOT NULL DEFAULT 'queued', created INTEGER NOT NULL);
        CREATE TABLE IF NOT EXISTS guests (
          id INTEGER PRIMARY KEY, nickname TEXT NOT NULL COLLATE NOCASE UNIQUE);
        CREATE TABLE IF NOT EXISTS tickets (
          hash TEXT PRIMARY KEY, place_id INTEGER NOT NULL, guest_id INTEGER NOT NULL,
          expires INTEGER NOT NULL);
        CREATE TABLE IF NOT EXISTS server_runs (
          id INTEGER PRIMARY KEY AUTOINCREMENT, place_id INTEGER NOT NULL,
          port INTEGER NOT NULL, player_limit INTEGER NOT NULL);
        CREATE TABLE IF NOT EXISTS room_guests (
          id INTEGER PRIMARY KEY AUTOINCREMENT, server_id INTEGER NOT NULL,
          nickname TEXT NOT NULL COLLATE NOCASE, owner TEXT NOT NULL,
          expires INTEGER NOT NULL, seen INTEGER NOT NULL DEFAULT 0,
          UNIQUE(server_id,nickname));
        CREATE TABLE IF NOT EXISTS room_tickets (
          hash TEXT PRIMARY KEY, server_id INTEGER NOT NULL, guest_id INTEGER NOT NULL,
          expires INTEGER NOT NULL);
    ''')
    if 'player_limit' not in {r['name'] for r in db().execute('PRAGMA table_info(places)')}:
        db().execute('ALTER TABLE places ADD COLUMN player_limit INTEGER NOT NULL DEFAULT 8')
    if 'asset_proxy' not in {r['name'] for r in db().execute('PRAGMA table_info(places)')}:
        db().execute("ALTER TABLE places ADD COLUMN asset_proxy TEXT NOT NULL DEFAULT 'user-choice'")
    if 'private_owner' not in {r['name'] for r in db().execute('PRAGMA table_info(server_runs)')}:
        db().execute('ALTER TABLE server_runs ADD COLUMN private_owner INTEGER')
    db().commit()


@app.before_request
def protect_forms():
    if request.path.startswith(('/admin', '/manage')) and not request.is_secure and os.environ.get('GRID_INSECURE') != '1':
        abort(403, _('Admin access is available on the HTTPS site only.'))
    if 'csrf' not in session:
        session['csrf'] = secrets.token_urlsafe(24)
    if request.method == 'POST' and request.path not in ('/Game/MachineConfiguration.ashx',):
        if not hmac.compare_digest(session['csrf'], request.form.get('csrf', '')):
            abort(403, _('The form expired. Reload and try again.'))


@app.after_request
def headers(response):
    response.headers['X-Content-Type-Options'] = 'nosniff'
    response.headers['X-Frame-Options'] = 'SAMEORIGIN'
    response.headers['Referrer-Policy'] = 'no-referrer'
    if response.mimetype == 'text/html' or request.path.startswith(('/launch/', '/game/', '/manage', '/login', '/signin', '/register', '/account', '/admin', '/dev')):
        response.headers['Cache-Control'] = 'no-store'
    return response


def admin():
    if not session.get('admin'):
        abort(403, _('Sign in as admin to manage places.'))


def place(place_id, management=False):
    row = db().execute('SELECT * FROM places WHERE id=?', (place_id,)).fetchone()
    if row is None or not (row['published'] or management):
        abort(404)
    return row


def winepath(path):
    return 'Z:' + str(Path(path).resolve()).replace('/', '\\')


SERVER_BUILDS = {'wine': ('Windows (Wine)', 'grid-worker.exe'),
                 'native': ('Native Linux 32-bit', 'grid-server'),
                 'native64': ('Native Linux 64-bit', 'grid-server-amd64')}


def worker_args(mode, row, last, backend=None):
    if mode == 'server' and (backend or server_backend()) != 'wine':
        return [str(RUNTIME/SERVER_BUILDS[backend or server_backend()][1]), mode,
                str(DATA/'places'/(row['sha']+'.rbxl')),
                str(RUNTIME/'content'), str(last)]
    return ['wine', str(RUNTIME/'grid-worker.exe'), mode,
            winepath(DATA/'places'/(row['sha']+'.rbxl')),
            winepath(RUNTIME/'content'), str(last)]


def server_backend():
    if not has_app_context():
        with app.app_context():
            return server_backend()
    saved = db().execute("SELECT value FROM instance_settings WHERE key='server_backend'").fetchone()
    if saved:
        return saved['value']
    backend = os.environ.get('GRID_SERVER_BACKEND', 'auto')
    if backend not in ('auto', *SERVER_BUILDS):
        raise RuntimeError('Unknown GRID_SERVER_BACKEND')
    if backend == 'auto':
        return 'native' if (RUNTIME/'grid-server').is_file() else 'wine'
    if backend != 'wine' and not (RUNTIME/SERVER_BUILDS[backend][1]).is_file():
        raise RuntimeError('Native server binary is not installed')
    return backend


def validate_server_build(backend):
    if backend not in SERVER_BUILDS:
        abort(400, _('Choose one of the installed server builds.'))
    if not (RUNTIME/SERVER_BUILDS[backend][1]).is_file():
        abort(409, _('This server build is not installed.'))
    return backend


def validate_asset_policy(value):
    if value not in ('enabled', 'user-choice', 'disabled'):
        abort(400, _('Choose Enabled, User choice or Disabled for the asset proxy.'))
    return value


def worker_environment(row):
    environment = os.environ.copy()
    environment['OPENRBX_ASSET_PROXY'] = '0' if row['asset_proxy'] == 'disabled' else '1'
    return environment


def read_place_upload(upload):
    if not upload or not upload.filename.lower().endswith(('.rbxl', '.rbxlx')):
        abort(400, _('Choose a legacy RBXL or RBXLX file.'))
    payload = upload.read(32*1024*1024+1)
    if len(payload) > 32*1024*1024:
        abort(413)
    try:
        root = ElementTree.fromstring(payload)
        if root.tag != 'roblox' or root.get('version') != '4':
            raise ValueError()
    except Exception:
        abort(400, _('This client needs a legacy XML RBXL, version 4; binary places are not supported.'))
    return payload


def store_place_payload(payload):
    sha = hashlib.sha256(payload).hexdigest()
    target = DATA/'places'/(sha+'.rbxl')
    if not target.exists():
        temporary = target.with_suffix('.'+secrets.token_hex(8)+'.tmp')
        try:
            temporary.write_bytes(payload)
            temporary.replace(target)
        finally:
            temporary.unlink(missing_ok=True)
    return sha


def cleanup_place_file(sha):
    # Caller holds room_lock; starting workers may still be reading their map.
    if sha and not db().execute('SELECT 1 FROM places WHERE sha=?', (sha,)).fetchone():
        if not any(r.get('sha') == sha and r['process'].poll() is None for r in rooms.values()):
            (DATA/'places'/(sha+'.rbxl')).unlink(missing_ok=True)


def thumbnail(place_id):
    with app.app_context():
        with content_lock:
            row = db().execute('SELECT * FROM places WHERE id=?', (place_id,)).fetchone()
            if row is None:
                return
            version = place_versions.setdefault(place_id, object())
        output = DATA/'jobs'/('%d-%s.b64' % (place_id, secrets.token_hex(8)))
        try:
            with (DATA/'jobs'/('%d-thumbnail.log' % place_id)).open('wb') as log:
                try:
                    subprocess.run(worker_args('thumbnail', row, winepath(output)),
                                   cwd=RUNTIME, stdout=log, stderr=log, timeout=20, check=True, env=worker_environment(row))
                except (subprocess.TimeoutExpired, subprocess.CalledProcessError):
                    log.write(b'\nRetrying with the normal missing-texture fallback; uploaded place unchanged.\n')
                    log.flush()
                    subprocess.run(worker_args('thumbnail-offline', row, winepath(output)),
                                   cwd=RUNTIME, stdout=log, stderr=log, timeout=70, check=True, env=worker_environment(row))
            png = base64.b64decode(output.read_bytes(), validate=True)
            if not png.startswith(b'\x89PNG\r\n\x1a\n'):
                raise ValueError('Worker did not return PNG')
            state = 'ready'
        except Exception:
            app.logger.exception('Thumbnail failed for place %d', place_id)
            state = 'failed'
        finally:
            output.unlink(missing_ok=True)
        with content_lock:
            # A deleted place (or a new upload reusing its ID) must not receive
            # output from a worker which started before deletion.
            if place_versions.get(place_id) is not version:
                (DATA/'jobs'/('%d-thumbnail.log' % place_id)).unlink(missing_ok=True)
                return
            if state == 'ready':
                tmp = DATA/'thumbs'/('%d.tmp' % place_id)
                tmp.write_bytes(png)
                tmp.replace(DATA/'thumbs'/('%d.png' % place_id))
            db().execute('UPDATE places SET thumb=? WHERE id=?', (state, place_id))
            db().commit()


def start_room(row, private_owner=None):
    # Caller holds room_lock, including while reserving the first player slot.
    row = place(row['id'], True)
    backend = server_backend()
    occupied = {r['port'] for r in rooms.values() if r['process'].poll() is None}
    port = next((p for p in range(45000, 45016) if p not in occupied), None)
    if port is None:
        abort(503, _('All 16 server slots are in use.'))
    cursor = db().execute('INSERT INTO server_runs(place_id,port,player_limit,private_owner) VALUES(?,?,?,?)',
                          (row['id'], port, row['player_limit'], private_owner))
    server_id = cursor.lastrowid
    db().commit()
    logfile = DATA/'jobs'/('server-%d.log' % server_id)
    status = DATA/'jobs'/('server-%d.json' % server_id)
    (DATA/'jobs'/('server-%d.json.access' % server_id)).write_text('')
    with logfile.open('wb') as log:
        process = subprocess.Popen(worker_args('server', row, port, backend)+
                                   [str(row['player_limit']),
                                    str(status) if backend != 'wine' else winepath(status)], cwd=RUNTIME,
                                   stdin=subprocess.DEVNULL, stdout=log, stderr=log, env=worker_environment(row))
    room = dict(id=server_id, place_id=row['id'], process=process, port=port,
                limit=row['player_limit'], log=logfile, status=status, backend=backend, sha=row['sha'], asset_proxy=row['asset_proxy'], private_owner=private_owner)
    rooms[server_id] = room
    return room


def room_ready(room):
    if room['process'].poll() is not None:
        return False
    if room.get('ready'):
        return True
    try:
        with room['log'].open('rb') as log:
            log.seek(0, 2)
            log.seek(max(0, log.tell()-65536))
            room['ready'] = b'SERVER_READY' in log.read()
            return room['ready']
    except OSError:
        return False


def read_room_status(room):
    try:
        if time.time()-room['status'].stat().st_mtime < 15:
            status = json.loads(room['status'].read_text())
            active = {int(p['id']) for p in status['players']}
            departed = {int(gid) for gid in status.get('departed', [])}
            clients = int(status['clients'])
            if clients >= 0:
                return active, departed, clients
    except (OSError, ValueError, KeyError, TypeError):
        pass
    return None


def room_population(room):
    now = int(time.time())
    status = read_room_status(room)
    fresh = status is not None
    active, departed, clients = status if fresh else (set(), set(), 0)
    for guest in db().execute('SELECT id,COALESCE(account_id,id) AS player_id,seen,expires FROM room_guests WHERE server_id=?', (room['id'],)).fetchall():
        if guest['player_id'] in active:
            db().execute('UPDATE room_guests SET seen=1,expires=? WHERE id=?', (now+30, guest['id']))
        elif (fresh and (guest['seen'] or guest['player_id'] in departed)) or guest['expires'] < now:
            db().execute('DELETE FROM room_guests WHERE id=?', (guest['id'],))
    reserved = {g['player_id'] for g in db().execute('SELECT COALESCE(account_id,id) AS player_id FROM room_guests WHERE server_id=?', (room['id'],))}
    db().commit()
    return max(clients, len(active)) + len(reserved-active)


def room_allows(room, user_id):
    owner = room.get('private_owner')
    if owner is None:
        return True
    if user_id is None or not db().execute('SELECT id FROM users WHERE id=? AND banned=0', (owner,)).fetchone():
        return False
    if owner == user_id:
        return True
    low, high = sorted((owner, user_id))
    return db().execute('SELECT 1 FROM friendships WHERE low_id=? AND high_id=? AND accepted=1', (low, high)).fetchone() is not None


def refresh_private_rooms():
    # Caller holds room_lock. Revoke pending tickets and live access when an
    # owner loses a friend or their account is removed/banned.
    for room in list(rooms.values()):
        if room.get('private_owner') is None:
            continue
        if not room_allows(room, room['private_owner']):
            stop_room(room['id'])
            continue
        for guest in db().execute('SELECT id,account_id FROM room_guests WHERE server_id=?', (room['id'],)).fetchall():
            if not room_allows(room, guest['account_id']):
                db().execute('DELETE FROM room_tickets WHERE guest_id=?', (guest['id'],))
                db().execute('DELETE FROM room_guests WHERE id=?', (guest['id'],))
        db().commit()
        refresh_room_access(room['id'])


def server_list(place_id=None, management=False):
    result = []
    for room in rooms.values():
        if place_id is not None and room['place_id'] != place_id:
            continue
        if not management and not room_allows(room, g.user['id'] if g.get('user') else None):
            continue
        if room['process'].poll() is None:
            result.append(dict(id=room['id'], place_id=room['place_id'],
                               players=room_population(room), limit=room['limit'],
                               state='Running' if room_ready(room) else 'Starting',
                               backend=SERVER_BUILDS.get(room.get('backend'), ('Unknown',))[0],
                               private_owner=room.get('private_owner')))
    return result


def stop_room(server_id):
    room = rooms.get(server_id)
    if room and room['process'].poll() is None:
        room['process'].terminate()
        try:
            room['process'].wait(timeout=5)
        except subprocess.TimeoutExpired:
            room['process'].kill()
            room['process'].wait(timeout=5)
    rooms.pop(server_id, None)
    if room:
        cleanup_place_file(room.get('sha'))
    db().execute('DELETE FROM room_tickets WHERE server_id=?', (server_id,))
    db().execute('DELETE FROM room_guests WHERE server_id=?', (server_id,))
    db().commit()
    for suffix in ('.access', '.access.tmp'):
        (DATA/'jobs'/('server-%d.json%s' % (server_id, suffix))).unlink(missing_ok=True)


def reap_idle_rooms():
    with app.app_context(), room_lock:
        now = time.monotonic()
        for server_id, room in list(rooms.items()):
            if room['process'].poll() is not None:
                stop_room(server_id)
                continue
            status = read_room_status(room)
            # Departures retain history even if someone joined and left between
            # monitor ticks. A pending ticket/handshake alone is not a player.
            if status is not None and (status[0] or status[1]):
                room['has_played'] = True
            # Missing/stale telemetry is not evidence that a server is empty.
            if status is None or status[0] or status[2] > 0:
                room['empty_since'] = None
                continue
            if room.get('empty_since') is None:
                room['empty_since'] = now
            elif now-room['empty_since'] >= (60 if room.get('has_played') else 15):
                app.logger.info('Stopping empty server #%d (timeout %ds)', server_id,
                                60 if room.get('has_played') else 15)
                stop_room(server_id)


def idle_server_monitor():
    tick = threading.Event()
    while not tick.wait(1):
        try:
            reap_idle_rooms()
        except Exception:
            app.logger.exception('Idle server cleanup failed')


@app.route('/')
@app.route('/Start.aspx')
@app.route('/IDE/Start.aspx')
def home():
    return render_template('home.html', places=db().execute(
        'SELECT * FROM places WHERE published=1 ORDER BY id DESC').fetchall())


@app.route('/download')
def download_client():
    return render_template('download.html')


@app.route('/downloads/notblox.xdelta')
@app.route('/downloads/notroblox.xdelta')
def download_client_patch():
    path = DATA/'downloads/notblox.xdelta'
    if not path.is_file():
        path = DATA/'downloads/notroblox.xdelta'
    if not path.is_file():
        abort(404, _('The client patch is not available yet.'))
    return send_file(path, mimetype='application/octet-stream', as_attachment=True,
                     download_name='notblox.xdelta', max_age=0)


@app.route('/places/<int:place_id>')
def details(place_id):
    row = place(place_id)
    with room_lock:
        servers = server_list(place_id)
    return render_template('place.html', place=row, servers=servers)


@app.route('/thumbs/<int:place_id>.png')
def thumb(place_id):
    place(place_id)
    path = DATA/'thumbs'/('%d.png' % place_id)
    if not path.exists():
        return send_file(Path(app.static_folder)/'pending.svg', mimetype='image/svg+xml')
    return send_file(path, mimetype='image/png', max_age=60)


@app.route('/asset/<int:place_id>')
def asset(place_id):
    row = place(place_id)
    return send_file(DATA/'places'/(row['sha']+'.rbxl'), mimetype='application/octet-stream',
                     as_attachment=True, download_name='place-%d.rbxl' % place_id)


@app.route('/play/<int:place_id>', methods=['POST'])
def play(place_id):
    row = place(place_id)
    if g.user is None:
        return redirect(url_for('account_login'))
    nickname = g.user['nickname']
    owner = session.setdefault('guest_session', secrets.token_urlsafe(24))
    selected = request.form.get('server', 'auto')
    with room_lock:
        reload_account()
        if g.user is None:
            abort(403, _('Your session expired. Log in again.'))
        nickname = g.user['nickname']
        row = place(place_id)
        if selected == 'private':
            room = next((r for r in rooms.values() if r['place_id']==place_id and
                         r.get('private_owner')==g.user['id'] and r['process'].poll() is None), None)
            if room is None:
                room = start_room(row, private_owner=g.user['id'])
        elif selected == 'new':
            room = start_room(row)
        elif selected == 'auto':
            room = next((r for r in rooms.values() if r['place_id']==place_id and
                         r.get('private_owner') is None and r['process'].poll() is None and room_population(r)<r['limit']), None)
            if room is None:
                room = start_room(row)
        else:
            if not selected.isdigit():
                abort(400, _('Choose a valid server.'))
            room = rooms.get(int(selected))
            if room is None or room['place_id']!=place_id or room['process'].poll() is not None:
                abort(410, _('That server has stopped. Choose another server.'))
        if not room_allows(room, g.user['id']):
            abort(403, _('This private server is for its owner and their accepted friends only.'))
        count = room_population(room)
        guest = db().execute('SELECT * FROM room_guests WHERE server_id=? AND nickname=?',
                             (room['id'], nickname)).fetchone()
        if guest and guest['owner'] != owner:
            abort(409, _('That nickname is in use on this server. Choose another name or server.'))
        if guest and guest['seen']:
            abort(409, _('You are already connected with that nickname on this server.'))
        if guest is None and count >= room['limit']:
            abort(409, _('This server is full. Choose another server or start a new one.'))
        now = int(time.time())
        if guest is None:
            guest_id = db().execute('INSERT INTO room_guests(server_id,nickname,owner,expires,account_id) VALUES(?,?,?,?,?)',
                                    (room['id'],nickname,owner,now+300,g.user['id'])).lastrowid
        else:
            guest_id = guest['id']
            db().execute('UPDATE room_guests SET expires=? WHERE id=?', (now+300,guest_id))
        session['nickname'] = nickname
        token = secrets.token_urlsafe(32)
        db().execute('DELETE FROM room_tickets WHERE expires<? OR guest_id=?', (now,guest_id))
        db().execute('INSERT INTO room_tickets VALUES(?,?,?,?)',
                     (hashlib.sha256(token.encode()).hexdigest(),room['id'],guest_id,now+300))
        db().commit()
        refresh_room_access(room['id'])
    return redirect(url_for('launch', token=token))


def ticket(token):
    if not re.fullmatch(r'[A-Za-z0-9_-]{43}', token):
        abort(404)
    row = db().execute('''SELECT t.*, u.nickname, u.id AS account_id, s.place_id FROM room_tickets t
        JOIN room_guests g ON g.id=t.guest_id JOIN server_runs s ON s.id=t.server_id
        JOIN users u ON u.id=g.account_id
        WHERE t.hash=? AND t.expires>? AND u.banned=0''',
        (hashlib.sha256(token.encode()).hexdigest(), int(time.time()))).fetchone()
    if row is None:
        abort(410, _('The play ticket expired. Return to the place page and press Play.'))
    return row


@app.route('/launch/<token>')
def launch(token):
    join = ticket(token)
    row = place(join['place_id'])
    room = rooms.get(join['server_id'])
    if room is None or room['process'].poll() is not None:
        abort(410, _('That server stopped. Return to the place and choose another server.'))
    if not room_allows(room, join['account_id']):
        abort(403, _('This private server is for its owner and their accepted friends only.'))
    ready = room_ready(room)
    api = ('https://' if request.is_secure else 'http://') + API.split('://', 1)[-1]
    return render_template('launch.html', place=row, server_id=room['id'], token=token, ready=ready, api=api,
                           failed=room['process'].poll() is not None)


@app.route('/game/join.ashx')
def join_script():
    join = ticket(request.args.get('ticket', ''))
    row = place(join['place_id'])
    with room_lock:
        join = ticket(request.args.get('ticket', ''))
        room = rooms.get(join['server_id'])
        if room is None or room['process'].poll() is not None:
            abort(410, _('That server stopped. Choose another server.'))
        if not room_allows(room, join['account_id']):
            abort(403, _('This private server is for its owner and their accepted friends only.'))
        if not room_ready(room):
            abort(503, _('The game server is still starting.'))
        claimed = db().execute('DELETE FROM room_tickets WHERE hash=?', (join['hash'],)).rowcount
        db().execute('UPDATE room_guests SET expires=? WHERE id=?', (int(time.time())+60,join['guest_id']))
        db().commit()
        if claimed != 1:
            abort(410, _('This play ticket was already used. Press Play again.'))
        # Restart the empty countdown while the client begins connecting.
        room['empty_since'] = None
    # Names are deliberately limited to a Lua-safe ASCII subset at the form.
    mode = {'enabled': 1, 'user-choice': -1, 'disabled': 0}[room.get('asset_proxy', 'user-choice')]
    script = '''pcall(function() game:SetAssetProxyPolicy(%d) end)
local client=game:service("NetworkClient")
local player=game.Players:CreateLocalPlayer(%d)
player.Name="%s"
client.ConnectionAccepted:connect(function(peer,replicator)
 local marker=replicator:SendMarker()
 marker.Received:wait()
 replicator:RequestCharacter()
 game:service("RunService"):Run()
end)
client.ConnectionFailed:connect(function(peer,code)
 local m=Instance.new("Message") m.Text="Could not connect to the game server ("..code..")" m.Parent=game.Workspace
end)
client:Connect("%s",%d,0,10)
''' % (mode, join['account_id'], join['nickname'], GAME_HOST, room['port'])
    return app.response_class(script, mimetype='text/plain')


@app.route('/api/places')
def api_places():
    site = ('https://' if request.is_secure else 'http://') + SITE.split('://', 1)[-1]
    return jsonify(places=[dict(id=p['id'], title=p['title'], thumbnail=site+'/thumbs/%d.png'%p['id'])
                           for p in db().execute('SELECT * FROM places WHERE published=1')])


@app.route('/about')
def about():
    return redirect('/')


@app.route('/health')
def health():
    return jsonify(ok=True, engine_worker=(RUNTIME/SERVER_BUILDS[server_backend()][1]).is_file(),
                   server_backend=server_backend(), thumbnail_backend='wine')


@app.route('/Game/MachineConfiguration.ashx', methods=['GET', 'POST'])
def machine_configuration():
    return '', 204


@app.route('/admin/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        auth_throttle('admin-login', 20, 900)
        if not check_password_hash(os.environ['GRID_ADMIN_HASH'], request.form.get('password', '')):
            abort(403, _('Incorrect admin password.'))
        session['admin'] = True
        session.permanent = True
        return redirect('/manage')
    return render_template('login.html')


@app.route('/logout', methods=['POST'])
def logout():
    if session.get('account_token'):
        db().execute('DELETE FROM account_sessions WHERE hash=?',
                     (hashlib.sha256(session['account_token'].encode()).hexdigest(),))
        db().commit()
    session.clear()
    return redirect('/')


@app.route('/manage', methods=['GET', 'POST'])
def manage():
    if request.method == 'GET' and not session.get('admin'):
        return redirect('/admin/login')
    admin()
    if request.method == 'POST':
        upload = request.files.get('place')
        title = request.form.get('title', '').strip()
        description = request.form.get('description', '').strip()
        if not upload or not upload.filename.lower().endswith(('.rbxl', '.rbxlx')) or not 1 <= len(title) <= 80:
            abort(400, _('Choose an RBXL file and a title of 1–80 characters.'))
        if len(description) > 2000:
            abort(400, _('Keep the description under 2,000 characters.'))
        policy = validate_asset_policy(request.form.get('asset_proxy', 'user-choice'))
        payload = read_place_upload(upload)
        with content_lock:
            sha = store_place_payload(payload)
            cursor = db().execute('INSERT INTO places(title,description,sha,created,asset_proxy) VALUES(?,?,?,?,?)',
                                  (title, description, sha, int(time.time()), policy))
            db().commit()
            place_versions[cursor.lastrowid] = object()
        jobs.submit(thumbnail, cursor.lastrowid)
        flash(_('Place added as a draft. Check its thumbnail, then publish it below.'))
        return redirect('/manage')
    with room_lock:
        servers = server_list(management=True)
    return render_template('manage.html', places=db().execute('SELECT * FROM places ORDER BY id DESC').fetchall(),
                           servers=servers, selected_build=server_backend(),
                           builds=[dict(id=key, label=value[0], available=(RUNTIME/value[1]).is_file())
                                   for key, value in SERVER_BUILDS.items()])


@app.route('/manage/<int:place_id>/<action>', methods=['GET', 'POST'])
def manage_action(place_id, action):
    admin()
    row = place(place_id, True)
    if request.method == 'GET':
        if action == 'edit':
            return render_template('edit_place.html', place=row)
        if action == 'delete':
            return render_template('delete_place.html', place=row)
        abort(405)
    if action == 'edit':
        title = request.form.get('title', '').strip()
        description = request.form.get('description', '').strip()
        policy = validate_asset_policy(request.form.get('asset_proxy', row['asset_proxy']))
        error = None
        if not 1 <= len(title) <= 80:
            error = _('Enter a title of 1-80 characters.')
        elif len(description) > 2000:
            error = _('Keep the description under 2,000 characters.')
        if error:
            return render_template('edit_place.html',
                                   place=dict(row, title=title, description=description), error=error), 400
        upload = request.files.get('place')
        payload = read_place_upload(upload) if upload and upload.filename else None
        changed = False
        with room_lock, content_lock:
            row = place(place_id, True)
            if policy != row['asset_proxy']:
                changed = True
                place_versions[place_id] = object()
                db().execute("UPDATE places SET thumb='queued' WHERE id=?", (place_id,))
            if payload is not None:
                sha = store_place_payload(payload)
                if sha != row['sha']:
                    db().execute("UPDATE places SET sha=?,thumb='queued' WHERE id=?", (sha, place_id))
                    place_versions[place_id] = object()
                    changed = True
            db().execute('UPDATE places SET title=?,description=?,asset_proxy=? WHERE id=?',
                         (title, description, policy, place_id))
            db().commit()
            if changed:
                cleanup_place_file(row['sha'])
        if changed:
            jobs.submit(thumbnail, place_id)
            flash(_('Place updated. New servers use the new file; running servers keep their current map. Thumbnail refresh queued.'))
        else:
            flash(_('Place details saved.'))
    elif action == 'delete':
        if request.form.get('confirm') != 'delete':
            abort(400, _('Confirm deletion using the Delete place page.'))
        with room_lock, content_lock:
            row = place(place_id, True)
            server_ids = [r['id'] for r in db().execute(
                'SELECT id FROM server_runs WHERE place_id=?', (place_id,))]
            for sid in server_ids:
                stop_room(sid)
                for suffix in ('log', 'json'):
                    (DATA/'jobs'/('server-%d.%s' % (sid, suffix))).unlink(missing_ok=True)
            db().execute('DELETE FROM tickets WHERE place_id=?', (place_id,))
            db().execute('DELETE FROM server_runs WHERE place_id=?', (place_id,))
            db().execute('DELETE FROM places WHERE id=?', (place_id,))
            db().commit()
            place_versions.pop(place_id, None)
            for path in (DATA/'thumbs'/('%d.png' % place_id),
                         DATA/'thumbs'/('%d.tmp' % place_id),
                         DATA/'jobs'/('%d.b64' % place_id)):
                path.unlink(missing_ok=True)
            try:
                (DATA/'jobs'/('%d-thumbnail.log' % place_id)).unlink(missing_ok=True)
            except PermissionError:
                # Windows cannot unlink an open log; the active thumbnail job
                # removes it when it sees the invalidated version above.
                pass
            # Identical uploads share a content-addressed RBXL file.
            cleanup_place_file(row['sha'])
        flash(_('Place deleted. Its servers have been stopped.'))
    elif action == 'thumbnail':
        db().execute("UPDATE places SET thumb='queued' WHERE id=?", (place_id,))
        jobs.submit(thumbnail, place_id)
    elif action in ('publish', 'unpublish'):
        if action == 'publish' and row['thumb'] != 'ready':
            abort(409, _('Wait for a successful thumbnail before publishing.'))
        db().execute('UPDATE places SET published=? WHERE id=?', (action == 'publish', place_id))
    elif action == 'stop':
        with room_lock:
            for sid in [r['id'] for r in rooms.values() if r['place_id']==place_id]:
                stop_room(sid)
        flash(_('Servers stopped.'))
    elif action == 'limit':
        value = request.form.get('player_limit', '')
        if not value.isdigit() or not 1 <= int(value) <= 32:
            abort(400, _('Player limit must be 1-32.'))
        db().execute('UPDATE places SET player_limit=? WHERE id=?', (int(value),place_id))
        flash(_('Player limit saved for new servers. Existing servers keep their current limit.'))
    elif action == 'start':
        count = request.form.get('count', '1')
        if not count.isdigit() or not 1 <= int(count) <= 16:
            abort(400, _('Request 1-16 servers.'))
        with room_lock:
            row = place(place_id, True)
            if sum(r['process'].poll() is None for r in rooms.values())+int(count)>16:
                abort(503, _('Not enough free server slots for this request.'))
            for unused_index in range(int(count)):
                start_room(row)
        flash(_('Servers requested.'))
    else:
        abort(404)
    db().commit()
    return redirect('/manage')


@app.route('/manage/servers/<int:server_id>/stop', methods=['POST'])
def stop_server(server_id):
    admin()
    with room_lock:
        stop_room(server_id)
    flash(_('Server #%d stopped.') % server_id)
    return redirect('/manage')


@app.route('/manage/server-build', methods=['POST'])
def select_server_build():
    admin()
    backend = validate_server_build(request.form.get('backend'))
    with room_lock:
        db().execute("INSERT INTO instance_settings(key,value) VALUES('server_backend',?) "
                     "ON CONFLICT(key) DO UPDATE SET value=excluded.value", (backend,))
        db().commit()
    flash(_('New servers will use %s. Running servers are unchanged.') % _(SERVER_BUILDS[backend][0]))
    return redirect('/manage')


@app.route('/manage/servers/<int:server_id>/restart', methods=['POST'])
def restart_server(server_id):
    admin()
    with room_lock:
        room = rooms.get(server_id)
        if room is None:
            abort(404, _('This server has already stopped.'))
        validate_server_build(server_backend())
        row = place(room['place_id'], True)
        stop_room(server_id)
        replacement = start_room(row, private_owner=room.get('private_owner'))
    flash(_('Server #%d restarted as #%d using the selected build and latest place file. Players must rejoin.') %
          (server_id, replacement['id']))
    return redirect('/manage')


@app.route('/api/places/<int:place_id>/servers')
def api_servers(place_id):
    place(place_id)
    with room_lock:
        return jsonify(servers=server_list(place_id))


@app.errorhandler(400)
@app.errorhandler(403)
@app.errorhandler(404)
@app.errorhandler(405)
@app.errorhandler(409)
@app.errorhandler(410)
@app.errorhandler(413)
@app.errorhandler(429)
@app.errorhandler(500)
@app.errorhandler(503)
def error_page(error):
    return render_template('error.html', error=error), error.code


import accounts
import i18n
i18n.install(app)
accounts.install(sys.modules[__name__])
import asset_admin
asset_admin.install(sys.modules[__name__])
import community
community.install(sys.modules[__name__])

if os.environ.get('GRID_DISABLE_IDLE_MONITOR') != '1':
    threading.Thread(target=idle_server_monitor, name='idle-server-monitor', daemon=True).start()
