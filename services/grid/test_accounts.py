import concurrent.futures
import hashlib
import json
import re
import time
import unittest

from werkzeug.security import check_password_hash
import test_app

grid = test_app.grid


class AccountTests(unittest.TestCase):
    def setUp(self):
        fixture = test_app.GridTests()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        self.client = fixture.client
        self.owner = self.browser()
        self.assertEqual(self.post(self.owner, '/admin/login', password='test-password').status_code, 302)

    def browser(self):
        client = grid.app.test_client()
        client.get('/')
        return client

    def post(self, client, path, **data):
        with client.session_transaction() as state:
            data['csrf'] = state['csrf']
        return client.post(path, data=data)

    def invite(self, uses=1, count=1, duration=7):
        response = self.post(self.owner, '/manage/invites', uses=str(uses), count=str(count), duration=str(duration), unit='days')
        self.assertEqual(response.status_code, 302)
        with grid.app.app_context():
            return grid.db().execute('SELECT code FROM invites ORDER BY id DESC').fetchone()[0]

    def signup(self, name='Builder', client=None, code=None):
        client = client or self.client
        code = code or self.invite()
        response = self.post(client, '/signin', nickname=name, invite=code, password='user-must-not-choose-this')
        self.assertEqual(response.status_code, 200, response.data)
        password = re.search(rb'id="generated-password" value="([^"]+)"', response.data).group(1).decode()
        self.assertNotEqual(password, 'user-must-not-choose-this')
        self.assertEqual(response.headers['Cache-Control'], 'no-store')
        return password

    def test_closed_default_generated_password_and_saved_sessions(self):
        self.assertIn(b'invite-only', self.client.get('/signin').data)
        self.assertEqual(self.post(self.client, '/signin', nickname='Builder').status_code, 400)
        password = self.signup()
        with grid.app.app_context():
            user = grid.db().execute('SELECT * FROM users').fetchone()
            self.assertTrue(check_password_hash(user['password_hash'], password))
            self.assertNotIn(password, user['password_hash'])
            token = grid.db().execute('SELECT hash FROM account_sessions').fetchone()[0]
        with self.client.session_transaction() as state:
            self.assertTrue(state.permanent)
            self.assertEqual(hashlib.sha256(state['account_token'].encode()).hexdigest(), token)
            self.assertNotIn(password, str(dict(state)))
        restored = self.browser()
        restored.set_cookie('guest_http', self.client.get_cookie('guest_http').value)
        self.assertEqual(restored.get('/account').status_code, 200)
        self.post(restored, '/logout')
        self.assertEqual(self.client.get('/account').status_code, 302)
        self.assertEqual(self.post(self.client, '/login', nickname='builder', password=password).status_code, 302)
        self.assertIn(b'Avatar colors', self.client.get('/account').data)

    def test_invite_batch_limits_expiry_revoke_and_open_policy(self):
        code = self.invite(uses=2, count=3)
        with grid.app.app_context():
            invites = grid.db().execute('SELECT * FROM invites').fetchall()
            self.assertEqual(len(invites), 3)
            self.assertTrue(all(6*86400 < row['expires']-time.time() <= 7*86400 for row in invites))
        self.signup('FirstUser', code=code)
        self.signup('SecondUser', client=self.browser(), code=code)
        self.assertEqual(self.post(self.browser(), '/signin', nickname='ThirdUser', invite=code).status_code, 400)
        expired = self.invite()
        with grid.app.app_context():
            grid.db().execute('UPDATE invites SET expires=1 WHERE code=?', (expired,))
            grid.db().commit()
        self.assertEqual(self.post(self.browser(), '/signin', nickname='ExpiredUser', invite=expired).status_code, 400)
        revoked = self.invite()
        with grid.app.app_context():
            iid = grid.db().execute('SELECT id FROM invites WHERE code=?', (revoked,)).fetchone()[0]
        self.assertEqual(self.post(self.owner, '/manage/invites/%d/revoke' % iid).status_code, 302)
        self.assertEqual(self.post(self.browser(), '/signin', nickname='RevokedUser', invite=revoked).status_code, 400)
        self.assertEqual(self.post(self.owner, '/manage/registration', mode='open').status_code, 302)
        self.assertIn(b'Registration is open', self.browser().get('/signin').data)
        self.assertEqual(self.post(self.browser(), '/signin', nickname='OpenUser').status_code, 200)
        self.assertEqual(self.post(self.owner, '/manage/registration', mode='closed').status_code, 302)
        self.assertEqual(self.post(self.browser(), '/signin', nickname='ClosedAgain').status_code, 400)
        self.assertEqual(self.post(self.owner, '/manage/invites', count='101', uses='1', duration='1', unit='days').status_code, 400)

    def test_invite_last_use_is_atomic_and_duplicate_does_not_consume(self):
        code = self.invite(uses=2)
        self.signup('Original', code=code)
        self.assertEqual(self.post(self.browser(), '/signin', nickname='original', invite=code).status_code, 400)
        def register(name):
            return self.post(self.browser(), '/signin', nickname=name, invite=code).status_code
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(register, ('ParallelOne', 'ParallelTwo')))
        self.assertEqual(sorted(results), [200, 400])
        with grid.app.app_context():
            self.assertEqual(grid.db().execute('SELECT uses FROM invites WHERE code=?', (code,)).fetchone()[0], 2)

    def test_account_identity_avatar_ban_reset_rename_and_remove(self):
        password = self.signup()
        with grid.app.app_context():
            uid = grid.db().execute('SELECT id FROM users').fetchone()[0]
        path = '/manage/users/%d/' % uid
        self.assertEqual(self.post(self.client, path+'ban').status_code, 403)
        self.assertEqual(self.client.post(path+'ban').status_code, 403)
        response = self.post(self.client, '/play/1', nickname='ForgedName', server='new')
        self.assertEqual(response.status_code, 302)
        room = next(iter(grid.rooms.values()))
        access = grid.DATA/'jobs'/('server-%d.json.access' % room['id'])
        self.assertIn('%d Builder ' % uid, access.read_text())
        self.assertNotIn('ForgedName', access.read_text())
        avatar = dict(Head='21', Torso='28', LeftArm='23', RightArm='24', LeftLeg='26', RightLeg='1')
        self.assertEqual(self.post(self.client, '/account', **avatar).status_code, 302)
        self.assertIn('21 28 23 24 26 1', access.read_text())
        self.assertEqual(self.post(self.client, '/account', **dict(avatar,Head='9999')).status_code, 400)
        self.assertEqual(self.post(self.owner, path+'ban').status_code, 302)
        self.assertEqual(access.read_text(), '')
        self.assertEqual(self.client.get(response.location).status_code, 410)
        self.assertEqual(self.client.get('/account').status_code, 302)
        self.assertEqual(self.post(self.client, '/login', nickname='Builder', password=password).status_code, 403)
        room['process'].terminate.assert_not_called()
        self.assertEqual(self.post(self.owner, path+'unban').status_code, 302)
        self.assertEqual(self.post(self.client, '/login', nickname='Builder', password=password).status_code, 302)
        changed = self.post(self.owner, path+'edit', nickname='Renamed', password_mode='random')
        new_password = re.search(rb'id="generated-password" value="([^"]+)"', changed.data).group(1).decode()
        self.assertEqual(self.client.get('/account').status_code, 302)
        self.assertEqual(self.post(self.client, '/login', nickname='Renamed', password=password).status_code, 403)
        self.assertEqual(self.post(self.client, '/login', nickname='Renamed', password=new_password).status_code, 302)
        custom = 'Admin custom password 123!'
        self.assertEqual(self.post(self.owner, path+'edit', nickname='Renamed', password_mode='custom', password=custom).status_code, 200)
        self.assertEqual(self.post(self.client, '/login', nickname='Renamed', password=custom).status_code, 302)
        self.assertEqual(self.owner.get(path+'delete').status_code, 200)
        self.assertEqual(self.post(self.owner, path+'delete').status_code, 400)
        self.assertEqual(self.post(self.owner, path+'delete', confirm='delete').status_code, 302)
        self.assertEqual(self.client.get('/account').status_code, 302)
        with grid.app.app_context():
            self.assertIsNone(grid.db().execute('SELECT id FROM users WHERE id=?', (uid,)).fetchone())
            self.assertEqual(grid.db().execute('SELECT COUNT(*) FROM account_sessions WHERE user_id=?', (uid,)).fetchone()[0], 0)

    def test_auth_boundaries_and_throttling(self):
        self.assertEqual(self.post(self.client, '/play/1', nickname='Guest').location, '/login')
        for path in ('/manage/users', '/manage/invites', '/manage/registration'):
            self.assertEqual(self.client.get(path).status_code, 403)
        from unittest.mock import patch
        import os
        with patch.dict(os.environ, GRID_INSECURE='0'):
            for path in ('/Start.aspx', '/IDE/Start.aspx'):
                response = self.client.get(path)
                self.assertEqual(response.status_code, 200)
                self.assertNotIn('Location', response.headers)
                self.assertNotIn(b'Admin sign-in', response.data)
                self.assertEqual(self.client.get(path, base_url='https://localhost').status_code, 200)
            public = self.client.get('/')
            self.assertEqual(public.status_code, 200)
            self.assertNotIn(b'Admin sign-in', public.data)
            for path in ('/login', '/signin', '/register'):
                response = self.client.get(path)
                self.assertEqual(response.status_code, 200)
                self.assertNotIn('Location', response.headers)
            self.assertEqual(self.client.get('/account').location, '/login')
            self.assertEqual(self.client.get('/admin/login').status_code, 403)
            self.assertEqual(self.post(self.client, '/admin/login').status_code, 403)
        for _ in range(20):
            self.assertEqual(self.post(self.client, '/login', nickname='Nobody', password='bad').status_code, 403)
        self.assertEqual(self.post(self.client, '/login', nickname='Nobody', password='bad').status_code, 429)

    def test_http_accounts_persist_and_https_stays_separate_without_upgrade(self):
        from unittest.mock import patch
        import os
        code = self.invite()
        with patch.dict(os.environ, GRID_INSECURE='0'):
            password = self.signup(code=code)
            cookie = self.client.get_cookie('guest_http')
            self.assertIsNotNone(cookie.expires)
            self.assertTrue(cookie.http_only)
            self.assertFalse(cookie.secure)
            restored = self.browser()
            restored.set_cookie('guest_http', cookie.value)
            self.assertEqual(restored.get('/account').status_code, 200)
            # An HTTP account cookie is not an HTTPS or admin session.
            self.assertEqual(restored.get('/account', base_url='https://localhost').location, '/login')
            public = restored.get('/').data
            self.assertIn(b'href="/account"', public)
            self.assertNotIn(b'Admin sign-in', public)
            play = self.post(restored, '/play/1', server='new')
            self.assertEqual(play.status_code, 302)
            self.assertTrue(play.location.startswith('/launch/'))
            launch = restored.get(play.location)
            self.assertEqual(launch.status_code, 200)
            self.assertIn(b'http://api.grid.blox.ralsei.tech/game/join.ashx', launch.data)
            self.assertNotIn(b'https://api.grid.blox.ralsei.tech', launch.data)
            page = restored.get('/login', base_url='https://localhost')
            csrf = re.search(rb'name="csrf" value="([^"]+)"', page.data).group(1).decode()
            response = restored.post('/login', base_url='https://localhost',
                                     data=dict(csrf=csrf, nickname='Builder', password=password))
            self.assertEqual(response.location, '/')
            self.assertIn('; Secure;', response.headers['Set-Cookie'])
            self.assertEqual(restored.get('/account', base_url='https://localhost').status_code, 200)
            self.assertEqual(restored.get('/admin/login').status_code, 403)
            # Logging out on HTTP revokes its token, not the HTTPS token.
            self.post(restored, '/logout')
            self.assertEqual(restored.get('/account').location, '/login')
            self.assertEqual(restored.get('/account', base_url='https://localhost').status_code, 200)


if __name__ == '__main__':
    unittest.main()
