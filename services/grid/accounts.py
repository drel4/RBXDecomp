"""Private-instance accounts, invitations and owner controls."""
from i18n import translate as _
import hashlib
import json
import re
import secrets
import sqlite3
import time
from datetime import timedelta

from flask import abort, flash, g, redirect, render_template, request, session
from werkzeug.security import check_password_hash, generate_password_hash

COLORS = {1: 'White', 21: 'Red', 23: 'Blue', 24: 'Yellow', 26: 'Black',
          28: 'Green', 37: 'Light green', 102: 'Medium blue', 104: 'Purple',
          106: 'Orange', 119: 'Yellow green', 194: 'Gray', 199: 'Dark gray'}
PARTS = ('Head', 'Torso', 'LeftArm', 'RightArm', 'LeftLeg', 'RightLeg')
DEFAULT_COLORS = [24, 23, 24, 24, 119, 119]


def digest(value):
    return hashlib.sha256(value.encode()).hexdigest()


def valid_name(value):
    return re.fullmatch(r'[A-Za-z][A-Za-z0-9_]{2,19}', value) is not None


def install(grid):
    app, db = grid.app, grid.db
    app.config['PERMANENT_SESSION_LIFETIME'] = timedelta(days=30)
    with app.app_context():
        db().executescript('''
            CREATE TABLE IF NOT EXISTS users (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              nickname TEXT NOT NULL COLLATE NOCASE UNIQUE,
              password_hash TEXT NOT NULL, banned INTEGER NOT NULL DEFAULT 0,
              created INTEGER NOT NULL, colors TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS account_sessions (
              hash TEXT PRIMARY KEY, user_id INTEGER NOT NULL, expires INTEGER NOT NULL);
            CREATE INDEX IF NOT EXISTS account_sessions_user ON account_sessions(user_id);
            CREATE TABLE IF NOT EXISTS instance_settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
            INSERT OR IGNORE INTO instance_settings VALUES('registration_open','0');
            CREATE TABLE IF NOT EXISTS invites (
              id INTEGER PRIMARY KEY AUTOINCREMENT, code TEXT NOT NULL UNIQUE,
              max_uses INTEGER NOT NULL, uses INTEGER NOT NULL DEFAULT 0,
              expires INTEGER NOT NULL, revoked INTEGER NOT NULL DEFAULT 0, created INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS auth_limits (
              key TEXT PRIMARY KEY, attempts INTEGER NOT NULL, expires INTEGER NOT NULL);
        ''')
        if 'account_id' not in {r['name'] for r in db().execute('PRAGMA table_info(room_guests)')}:
            db().execute('ALTER TABLE room_guests ADD COLUMN account_id INTEGER')
        db().commit()

    def registration_open():
        return db().execute("SELECT value FROM instance_settings WHERE key='registration_open'").fetchone()[0] == '1'

    def throttle(kind, maximum, seconds):
        # ProxyFix trusts the local nginx proxy. Store only a hash of the IP.
        now = int(time.time())
        key = digest(kind + ':' + (request.remote_addr or 'unknown'))
        db().execute('DELETE FROM auth_limits WHERE expires<=?', (now,))
        db().execute('''INSERT INTO auth_limits VALUES(?,1,?) ON CONFLICT(key)
                      DO UPDATE SET attempts=attempts+1''', (key, now+seconds))
        count = db().execute('SELECT attempts FROM auth_limits WHERE key=?', (key,)).fetchone()[0]
        db().commit()
        if count > maximum:
            abort(429, _('Too many attempts. Please try again later.'))

    def sign_in(user_id):
        token = secrets.token_urlsafe(32)
        now = int(time.time())
        db().execute('DELETE FROM account_sessions WHERE expires<=?', (now,))
        db().execute('INSERT INTO account_sessions VALUES(?,?,?)', (digest(token), user_id, now+30*86400))
        db().commit()
        session.clear()
        session.update(account_token=token, csrf=secrets.token_urlsafe(24))
        session.permanent = True

    grid.auth_throttle = throttle

    @app.before_request
    def load_user():
        g.user = None
        token = session.get('account_token')
        if token:
            g.user = db().execute('''SELECT u.* FROM account_sessions s JOIN users u ON u.id=s.user_id
                WHERE s.hash=? AND s.expires>? AND u.banned=0''', (digest(token), int(time.time()))).fetchone()
            if g.user is None:
                session.pop('account_token', None)

    grid.reload_account = load_user

    @app.context_processor
    def account_context():
        return dict(account_user=g.get('user'), secure_site=grid.SITE)

    def require_user():
        if g.user is None:
            abort(403, _('Log in to your account first.'))
        return g.user

    def refresh_room_access(server_id):
        rows = db().execute('''SELECT u.*,t.id AS approved_tshirt FROM room_guests r JOIN users u ON u.id=r.account_id
                             LEFT JOIN tshirts t ON t.id=u.tshirt_id AND t.state='approved'
                             WHERE r.server_id=? AND u.banned=0''', (server_id,)).fetchall()
        room = grid.rooms.get(server_id)
        if room:
            rows = [r for r in rows if grid.room_allows(room, r['id'])]
        asset_site = 'http://' + grid.SITE.split('://', 1)[-1].rstrip('/')
        lines = ['%d %s %s %s' % (r['id'], r['nickname'], ' '.join(str(c) for c in json.loads(r['colors'])),
                 asset_site+'/tshirts/%d.png' % r['approved_tshirt'] if r['approved_tshirt'] else '-') for r in rows]
        path = grid.DATA/'jobs'/('server-%d.json.access' % server_id)
        tmp = path.with_suffix('.access.tmp')
        tmp.write_text('\n'.join(lines), encoding='ascii')
        tmp.replace(path)

    grid.refresh_room_access = refresh_room_access

    def revoke_user(user_id, sessions=True):
        # Caller holds room_lock. Native workers observe these access updates
        # and disconnect only this player, not the other users in the room.
        server_ids = [r[0] for r in db().execute('SELECT DISTINCT server_id FROM room_guests WHERE account_id=?', (user_id,))]
        if sessions:
            db().execute('DELETE FROM account_sessions WHERE user_id=?', (user_id,))
        db().execute('DELETE FROM room_tickets WHERE guest_id IN (SELECT id FROM room_guests WHERE account_id=?)', (user_id,))
        db().execute('DELETE FROM room_guests WHERE account_id=?', (user_id,))
        db().commit()
        for sid in server_ids:
            if sid in grid.rooms:
                refresh_room_access(sid)
        grid.refresh_private_rooms()

    @app.route('/login', methods=['GET', 'POST'])
    def account_login():
        error = None
        if request.method == 'POST':
            throttle('login', 20, 900)
            nickname = request.form.get('nickname', '').strip()
            password = request.form.get('password', '')
            user = db().execute('SELECT * FROM users WHERE nickname=?', (nickname,)).fetchone()
            if user and len(password) <= 256 and check_password_hash(user['password_hash'], password) and not user['banned']:
                sign_in(user['id'])
                return redirect('/')
            error = _('Incorrect nickname or password, or this account is banned.')
        return render_template('account_login.html', error=error), 403 if error else 200

    @app.route('/signin', methods=['GET', 'POST'])
    @app.route('/register', methods=['GET', 'POST'])
    def register():
        opened = registration_open()
        error = None
        if request.method == 'POST':
            throttle('register', 20, 3600)
            nickname = request.form.get('nickname', '').strip()
            code = request.form.get('invite', '').strip()
            if not valid_name(nickname):
                error = _('Use 3-20 letters, numbers or underscores, starting with a letter.')
            else:
                password = secrets.token_urlsafe(18)
                password_hash = generate_password_hash(password)
                try:
                    db().execute('BEGIN IMMEDIATE')
                    # Re-read the policy inside the same transaction as the
                    # invite claim and account insert: parallel signups cannot
                    # exceed an invite limit or bypass a newly closed gate.
                    opened = registration_open()
                    if not opened:
                        claimed = db().execute('''UPDATE invites SET uses=uses+1 WHERE code=?
                            AND revoked=0 AND uses<max_uses AND (expires=0 OR expires>?)''',
                                               (code, int(time.time()))).rowcount
                        if claimed != 1:
                            db().rollback()
                            error = _('A valid, unexpired invite code is required.')
                    if error is None:
                        user_id = db().execute('INSERT INTO users(nickname,password_hash,created,colors) VALUES(?,?,?,?)',
                                               (nickname, password_hash, int(time.time()), json.dumps(DEFAULT_COLORS))).lastrowid
                        db().commit()
                except sqlite3.IntegrityError:
                    db().rollback()
                    error = _('That nickname is already registered.')
                if error is None:
                    sign_in(user_id)
                    # Never store plaintext passwords in cookies, flash messages,
                    # the database or redirect URLs. Show this response once.
                    return render_template('password_result.html', nickname=nickname,
                                           password=password, registered=True)
        return render_template('register.html', opened=opened, error=error), 400 if error else 200

    @app.route('/account', methods=['GET', 'POST'])
    def account():
        if g.user is None:
            return redirect('/login')
        error = None
        if request.method == 'POST':
            try:
                colors = [int(request.form.get(part, '')) for part in PARTS]
                if any(c not in COLORS for c in colors):
                    raise ValueError()
            except ValueError:
                error = _('Choose a listed color for every body part.')
            if error is None:
                with grid.room_lock:
                    db().execute('UPDATE users SET colors=? WHERE id=?', (json.dumps(colors), g.user['id']))
                    db().commit()
                    for row in db().execute('SELECT DISTINCT server_id FROM room_guests WHERE account_id=?', (g.user['id'],)):
                        if row[0] in grid.rooms:
                            refresh_room_access(row[0])
                flash(_('Avatar colors saved.'))
                return redirect('/account')
        return render_template('account.html', colors=COLORS, parts=PARTS,
                               selected=json.loads(g.user['colors']), error=error), 400 if error else 200

    @app.route('/manage/registration', methods=['GET', 'POST'])
    def manage_registration():
        grid.admin()
        if request.method == 'POST':
            mode = request.form.get('mode')
            if mode not in ('open', 'closed'):
                abort(400, _('Choose open or closed registration.'))
            db().execute("UPDATE instance_settings SET value=? WHERE key='registration_open'", (str(int(mode=='open')),))
            db().commit()
            flash(_('Registration is now %s.') % _(mode))
            return redirect('/manage/registration')
        return render_template('registration_settings.html', opened=registration_open())

    @app.route('/manage/invites', methods=['GET', 'POST'])
    def manage_invites():
        grid.admin()
        if request.method == 'POST':
            try:
                count = int(request.form.get('count', ''))
                uses = int(request.form.get('uses', ''))
                duration = int(request.form.get('duration', ''))
                unit = {'minutes': 60, 'hours': 3600, 'days': 86400}[request.form.get('unit', '')]
                if not 1 <= count <= 100 or not 1 <= uses <= 10000 or not 0 <= duration*unit <= 365*86400:
                    raise ValueError()
            except (ValueError, KeyError):
                abort(400, _('Create 1-100 codes, each with 1-10,000 uses and a lifetime up to 365 days (0 means no expiry).'))
            now = int(time.time())
            for unused_index in range(count):
                db().execute('INSERT INTO invites(code,max_uses,expires,created) VALUES(?,?,?,?)',
                             (secrets.token_urlsafe(18), uses, now+duration*unit if duration else 0, now))
            db().commit()
            flash(_('%d invite codes created.') % count)
            return redirect('/manage/invites')
        rows = db().execute('SELECT * FROM invites ORDER BY id DESC').fetchall()
        return render_template('invites.html', invites=rows, now=int(time.time()))

    @app.route('/manage/invites/<int:invite_id>/revoke', methods=['POST'])
    def revoke_invite(invite_id):
        grid.admin()
        if not db().execute('UPDATE invites SET revoked=1 WHERE id=?', (invite_id,)).rowcount:
            abort(404)
        db().commit()
        flash(_('Invite revoked.'))
        return redirect('/manage/invites')

    @app.route('/manage/users')
    def manage_users():
        grid.admin()
        return render_template('users.html', users=db().execute('SELECT id,nickname,banned,created FROM users ORDER BY id DESC').fetchall())

    @app.route('/manage/users/<int:user_id>/<action>', methods=['GET', 'POST'])
    def manage_user(user_id, action):
        grid.admin()
        user = db().execute('SELECT * FROM users WHERE id=?', (user_id,)).fetchone()
        if user is None:
            abort(404)
        if action not in ('edit', 'delete', 'ban', 'unban'):
            abort(404)
        if request.method == 'GET':
            if action not in ('edit', 'delete'):
                abort(405)
            return render_template('user_edit.html' if action=='edit' else 'user_delete.html', user=user)
        with grid.room_lock:
            if action == 'edit':
                nickname = request.form.get('nickname', '').strip()
                if not valid_name(nickname):
                    abort(400, _('Use a nickname of 3-20 letters, numbers or underscores, starting with a letter.'))
                mode = request.form.get('password_mode', 'keep')
                if mode not in ('keep', 'random', 'custom'):
                    abort(400, _('Choose a password action.'))
                password = secrets.token_urlsafe(18) if mode=='random' else request.form.get('password', '')
                if mode == 'custom' and not 12 <= len(password) <= 128:
                    abort(400, _('Use a password of 12-128 characters.'))
                try:
                    db().execute('UPDATE users SET nickname=? WHERE id=?', (nickname, user_id))
                    if mode != 'keep':
                        db().execute('UPDATE users SET password_hash=? WHERE id=?', (generate_password_hash(password), user_id))
                    db().commit()
                except sqlite3.IntegrityError:
                    db().rollback()
                    abort(409, _('That nickname is already registered.'))
                if nickname != user['nickname'] or mode != 'keep':
                    revoke_user(user_id, sessions=mode!='keep')
                if mode != 'keep':
                    return render_template('password_result.html', nickname=nickname, password=password, registered=False)
                flash(_('User saved.'))
            elif action in ('ban', 'unban'):
                db().execute('UPDATE users SET banned=? WHERE id=?', (int(action=='ban'), user_id))
                db().commit()
                if action == 'ban':
                    revoke_user(user_id)
                flash(_('User %s.') % _('banned' if action=='ban' else 'unbanned'))
            elif action == 'delete':
                if request.form.get('confirm') != 'delete':
                    abort(400, _('Confirm removal on the Remove user page.'))
                revoke_user(user_id)
                grid.community_remove_user(user_id)
                db().execute('DELETE FROM users WHERE id=?', (user_id,))
                db().commit()
                flash(_('User removed from the instance.'))
        return redirect('/manage/users')

    @app.template_filter('utcdate')
    def utcdate(timestamp):
        return time.strftime('%Y-%m-%d %H:%M UTC', time.gmtime(timestamp)) if timestamp else _('Never')
