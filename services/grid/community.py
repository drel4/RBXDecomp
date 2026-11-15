"""Mutual friends and owner-moderated, free T-shirt graphics."""
import io
import secrets
import time
import warnings
from pathlib import Path

from flask import abort, flash, g, redirect, render_template, request, send_file
from PIL import Image, UnidentifiedImageError
from i18n import translate as _


def install(grid):
    app, db = grid.app, grid.db
    with app.app_context():
        db().executescript('''
            CREATE TABLE IF NOT EXISTS friendships (
              low_id INTEGER NOT NULL, high_id INTEGER NOT NULL,
              requester INTEGER NOT NULL, accepted INTEGER NOT NULL DEFAULT 0,
              created INTEGER NOT NULL, PRIMARY KEY(low_id, high_id), CHECK(low_id<high_id));
            CREATE TABLE IF NOT EXISTS tshirts (
              id INTEGER PRIMARY KEY AUTOINCREMENT, creator INTEGER NOT NULL,
              title TEXT NOT NULL, image TEXT NOT NULL UNIQUE,
              state TEXT NOT NULL DEFAULT 'pending', reason TEXT NOT NULL DEFAULT '',
              created INTEGER NOT NULL, reviewed INTEGER NOT NULL DEFAULT 0);
        ''')
        if 'tshirt_id' not in {r['name'] for r in db().execute('PRAGMA table_info(users)')}:
            db().execute('ALTER TABLE users ADD COLUMN tshirt_id INTEGER')
        db().commit()

    def user():
        if g.user is None:
            abort(403, _('Log in to your account first.'))
        return g.user['id']

    def refresh_avatars():
        for sid, room in list(grid.rooms.items()):
            if room['process'].poll() is None:
                grid.refresh_room_access(sid)

    def remove_user(uid):
        for sid, room in list(grid.rooms.items()):
            if room.get('private_owner') == uid:
                grid.stop_room(sid)
        db().execute('DELETE FROM friendships WHERE low_id=? OR high_id=?', (uid, uid))
        db().execute("UPDATE tshirts SET state='rejected',reason=? WHERE creator=?",
                     (_('Creator account removed.'), uid))
        db().execute('UPDATE users SET tshirt_id=NULL WHERE tshirt_id IN (SELECT id FROM tshirts WHERE creator=?)', (uid,))
        db().commit()
        refresh_avatars()

    grid.community_remove_user = remove_user

    @app.route('/friends', methods=['GET', 'POST'])
    def friends():
        uid = user()
        error = None
        if request.method == 'POST':
            grid.auth_throttle('friend-request', 40, 3600)
            name = request.form.get('nickname', '').strip()
            target = db().execute('SELECT id FROM users WHERE nickname=? AND banned=0', (name,)).fetchone()
            if target is None:
                error = _('No player with that nickname was found.')
            elif target['id'] == uid:
                error = _('You cannot add yourself.')
            else:
                low, high = sorted((uid, target['id']))
                added = db().execute('INSERT OR IGNORE INTO friendships VALUES(?,?,?,0,?)',
                                     (low, high, uid, int(time.time()))).rowcount
                db().commit()
                flash(_('Friend request sent.') if added else _('A friendship or request already exists.'))
                return redirect('/friends')
        rows = db().execute('''SELECT f.*,u.id AS friend_id,u.nickname FROM friendships f JOIN users u
            ON u.id=CASE WHEN f.low_id=? THEN f.high_id ELSE f.low_id END
            WHERE (f.low_id=? OR f.high_id=?) AND u.banned=0 ORDER BY u.nickname''', (uid, uid, uid)).fetchall()
        playing = {}
        accepted = {r['friend_id'] for r in rows if r['accepted']}
        with grid.room_lock:
            for room in grid.rooms.values():
                if room['process'].poll() is not None:
                    continue
                if not grid.room_allows(room, uid):
                    continue
                status = grid.read_room_status(room)
                if status is None:
                    continue
                place = db().execute('SELECT id,title FROM places WHERE id=? AND published=1', (room['place_id'],)).fetchone()
                if place is None:
                    continue
                available = status[0] & accepted
                if room.get('private_owner') in accepted:
                    available = available | {room['private_owner']}
                for fid in available:
                    playing.setdefault(fid, []).append(dict(id=room['id'], place_id=place['id'], title=place['title'], private=room.get('private_owner') is not None))
        return render_template('friends.html', friends=rows, playing=playing, error=error), 400 if error else 200

    @app.post('/friends/<int:friend_id>/<action>')
    def friendship_action(friend_id, action):
        uid = user()
        low, high = sorted((uid, friend_id))
        row = db().execute('SELECT * FROM friendships WHERE low_id=? AND high_id=?', (low, high)).fetchone()
        if row is None:
            abort(404)
        if action == 'accept':
            target = db().execute('SELECT id FROM users WHERE id=? AND banned=0', (friend_id,)).fetchone()
            if row['requester'] == uid or row['accepted'] or target is None:
                abort(400)
            db().execute('UPDATE friendships SET accepted=1 WHERE low_id=? AND high_id=?', (low, high))
            flash(_('Friend request accepted.'))
        elif action == 'remove':
            with grid.room_lock:
                db().execute('DELETE FROM friendships WHERE low_id=? AND high_id=?', (low, high))
                db().commit()
                grid.refresh_private_rooms()
            flash(_('Friendship or request removed.'))
        else:
            abort(404)
        db().commit()
        return redirect('/friends')

    @app.post('/servers/<int:server_id>/stop')
    def stop_private_server(server_id):
        uid = user()
        with grid.room_lock:
            room = grid.rooms.get(server_id)
            if room is None:
                abort(404)
            if room.get('private_owner') != uid:
                abort(403)
            place_id = room['place_id']
            grid.stop_room(server_id)
        flash(_('Your private server has stopped.'))
        return redirect('/places/%d' % place_id)

    @app.route('/marketplace', methods=['GET', 'POST'])
    def marketplace():
        if request.method == 'POST':
            uid = user()
            selected = request.form.get('tshirt', '')
            if selected != 'none':
                if not selected.isdigit() or len(selected) > 10:
                    abort(400)
                row = db().execute("SELECT id FROM tshirts WHERE id=? AND state='approved'", (int(selected),)).fetchone()
                if row is None:
                    abort(404)
                selected = row['id']
            else:
                selected = None
            with grid.room_lock:
                # Recheck under the same lock as moderation and access refresh.
                if selected is not None and db().execute("SELECT id FROM tshirts WHERE id=? AND state='approved'", (selected,)).fetchone() is None:
                    abort(404)
                db().execute('UPDATE users SET tshirt_id=? WHERE id=?', (selected, uid))
                db().commit()
                refresh_avatars()
            flash(_('T-shirt selection saved.'))
            return redirect('/marketplace')
        shirts = db().execute('''SELECT t.*,u.nickname FROM tshirts t LEFT JOIN users u ON u.id=t.creator
            WHERE t.state='approved' ORDER BY t.id DESC''').fetchall()
        return render_template('marketplace.html', shirts=shirts)

    @app.route('/tshirts/upload', methods=['GET', 'POST'])
    def upload_tshirt():
        uid = user()
        error = None
        if request.method == 'POST':
            grid.auth_throttle('tshirt-upload', 20, 3600)
            title = request.form.get('title', '').strip()
            upload = request.files.get('image')
            payload = upload.read(2*1024*1024+1) if upload else b''
            normalized = None
            if not 1 <= len(title) <= 80:
                error = _('Enter a title of 1-80 characters.')
            elif not payload or len(payload) > 2*1024*1024:
                error = _('Choose a PNG or JPEG image up to 2 MB.')
            else:
                try:
                    with warnings.catch_warnings():
                        warnings.simplefilter('error', Image.DecompressionBombWarning)
                        with Image.open(io.BytesIO(payload)) as image:
                            if image.format not in ('PNG', 'JPEG') or getattr(image, 'n_frames', 1) != 1 or not (1 <= image.width <= 1024 and 1 <= image.height <= 1024):
                                raise ValueError()
                            image.load()
                            graphic = image.convert('RGBA')
                            graphic.thumbnail((256, 256), Image.Resampling.LANCZOS)
                            square = Image.new('RGBA', (256, 256))
                            square.paste(graphic, ((256-graphic.width)//2, (256-graphic.height)//2))
                            output = io.BytesIO()
                            square.save(output, format='PNG')
                            normalized = output.getvalue()
                except (OSError, ValueError, UnidentifiedImageError, Image.DecompressionBombError, Image.DecompressionBombWarning):
                    error = _('Use a valid, single PNG or JPEG image, at most 1024 by 1024 pixels.')
            if error is None:
                folder = grid.DATA/'tshirts'
                folder.mkdir(exist_ok=True)
                path = folder/(secrets.token_hex(24)+'.png')
                try:
                    db().execute('BEGIN IMMEDIATE')
                    if db().execute("SELECT COUNT(*) FROM tshirts WHERE creator=? AND state='pending'", (uid,)).fetchone()[0] >= 5:
                        db().rollback()
                        error = _('You already have five T-shirts waiting for review.')
                    else:
                        path.write_bytes(normalized)
                        db().execute('INSERT INTO tshirts(creator,title,image,created) VALUES(?,?,?,?)',
                                     (uid, title, path.name, int(time.time())))
                        db().commit()
                except Exception:
                    db().rollback()
                    path.unlink(missing_ok=True)
                    raise
                if error is None:
                    flash(_('T-shirt submitted. It stays private until an admin approves it.'))
                    return redirect('/tshirts/upload')
        mine = db().execute('SELECT * FROM tshirts WHERE creator=? ORDER BY id DESC', (uid,)).fetchall()
        return render_template('tshirt_upload.html', shirts=mine, error=error), 400 if error else 200

    @app.get('/tshirts/<int:shirt_id>.png')
    def tshirt_image(shirt_id):
        row = db().execute('SELECT * FROM tshirts WHERE id=?', (shirt_id,)).fetchone()
        owner = g.user is not None and row is not None and row['creator'] == g.user['id']
        admin = request.is_secure and grid.session.get('admin')
        if row is None or (row['state'] != 'approved' and not owner and not admin):
            abort(404)
        response = send_file(grid.DATA/'tshirts'/row['image'], mimetype='image/png', max_age=0)
        response.headers['Cache-Control'] = 'private, no-store'
        return response

    @app.route('/manage/moderation', methods=['GET', 'POST'])
    def moderation():
        grid.admin()
        if request.method == 'POST':
            value = request.form.get('tshirt', '')
            action = request.form.get('action', '')
            reason = request.form.get('reason', '').strip()
            if not value.isdigit() or len(value) > 10 or action not in ('approve', 'reject') or len(reason) > 300:
                abort(400)
            with grid.room_lock:
                row = db().execute('SELECT * FROM tshirts WHERE id=?', (int(value),)).fetchone()
                if row is None:
                    abort(404)
                db().execute('UPDATE tshirts SET state=?,reason=?,reviewed=? WHERE id=?',
                             ('approved' if action == 'approve' else 'rejected', '' if action == 'approve' else reason, int(time.time()), row['id']))
                if action == 'reject':
                    db().execute('UPDATE users SET tshirt_id=NULL WHERE tshirt_id=?', (row['id'],))
                db().commit()
                refresh_avatars()
            flash(_('Moderation decision saved.'))
            return redirect('/manage/moderation')
        state = request.args.get('state', 'pending')
        if state not in ('pending', 'approved', 'rejected'):
            abort(400)
        shirts = db().execute('''SELECT t.*,u.nickname FROM tshirts t LEFT JOIN users u ON u.id=t.creator
            WHERE t.state=? ORDER BY t.id DESC''', (state,)).fetchall()
        return render_template('moderation.html', shirts=shirts, state=state)
