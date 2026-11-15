import io
import json
import os
import time
import unittest
from unittest.mock import patch
from PIL import Image
import test_accounts

grid = test_accounts.grid


class CommunityTests(unittest.TestCase):
    setUp = test_accounts.AccountTests.setUp
    browser = test_accounts.AccountTests.browser
    post = test_accounts.AccountTests.post
    invite = test_accounts.AccountTests.invite
    signup = test_accounts.AccountTests.signup

    def members(self):
        self.signup('FirstPlayer')
        self.other = self.browser()
        self.signup('SecondPlayer', client=self.other)
        with grid.app.app_context():
            return [r[0] for r in grid.db().execute('SELECT id FROM users ORDER BY id')]

    def image(self, dimensions=(80, 40)):
        out = io.BytesIO()
        Image.new('RGBA', dimensions, (20, 150, 80, 140)).save(out, 'PNG')
        return out.getvalue()

    def upload(self, data=None, title='Test logo'):
        return self.post(self.client, '/tshirts/upload', title=title,
                         image=(io.BytesIO(data if data is not None else self.image()), 'logo.png'))

    def shirt(self):
        with grid.app.app_context():
            return dict(grid.db().execute('SELECT * FROM tshirts ORDER BY id DESC').fetchone())

    def review(self, sid, action, reason=''):
        with self.owner.session_transaction(base_url='https://localhost') as state:
            state['admin'] = True
            state['csrf'] = 'moderation-test-csrf'
        return self.owner.post('/manage/moderation', base_url='https://localhost',
                               data=dict(csrf='moderation-test-csrf', tshirt=str(sid), action=action, reason=reason))

    def test_friend_requests_mutual_consent_and_ownership(self):
        first, second = self.members()
        stranger = self.browser()
        self.signup('ThirdPlayer', client=stranger)
        self.assertEqual(self.post(self.client, '/friends', nickname='firstplayer').status_code, 400)
        self.assertEqual(self.post(self.client, '/friends', nickname='missing').status_code, 400)
        self.assertEqual(self.post(self.client, '/friends', nickname='secondplayer').status_code, 302)
        self.assertIn(b'Wants to be your friend', self.other.get('/friends').data)
        self.assertEqual(self.post(self.client, '/friends/%d/accept' % second).status_code, 400)
        self.assertEqual(self.post(stranger, '/friends/%d/accept' % first).status_code, 404)
        self.assertEqual(self.other.post('/friends/%d/accept' % first).status_code, 403)
        self.assertEqual(self.post(self.other, '/friends/%d/accept' % first).status_code, 302)
        self.assertIn(b'Remove friend', self.client.get('/friends').data)
        self.post(self.other, '/friends', nickname='FirstPlayer')
        with grid.app.app_context():
            self.assertEqual(grid.db().execute('SELECT COUNT(*) FROM friendships').fetchone()[0], 1)
        self.assertEqual(self.post(self.other, '/friends/%d/remove' % first).status_code, 302)
        self.assertIn(b'No friends or requests yet', self.client.get('/friends').data)

    def test_friend_presence_uses_fresh_actual_players_and_joins_specific_server(self):
        first, second = self.members()
        self.post(self.client, '/friends', nickname='SecondPlayer')
        self.post(self.other, '/friends/%d/accept' % first)
        with grid.app.app_context():
            grid.db().execute('UPDATE places SET player_limit=2 WHERE id=1')
            grid.db().commit()
        self.assertEqual(self.post(self.other, '/play/1', server='new').status_code, 302)
        room = next(iter(grid.rooms.values()))
        # A reservation alone must never be shown as a friend playing.
        self.assertNotIn(b'Join friend', self.client.get('/friends').data)
        room['status'].write_text(json.dumps(dict(players=[dict(id=second)], clients=1)))
        page = self.client.get('/friends').data
        self.assertIn(b'Join friend', page)
        self.assertIn(('name="server" value="%d"' % room['id']).encode(), page)
        response = self.post(self.client, '/play/1', server=str(room['id']))
        self.assertEqual(response.status_code, 302)
        self.assertEqual(len(grid.rooms), 1)
        os.utime(room['status'], (time.time()-30, time.time()-30))
        self.assertNotIn(b'Join friend', self.client.get('/friends').data)
        self.post(self.other, '/friends/%d/remove' % first)
        self.assertNotIn(b'SecondPlayer', self.client.get('/friends').data)

    def test_pending_images_private_normalized_and_approved_before_wearing(self):
        first, second = self.members()
        self.assertEqual(self.upload(title='<script>Logo</script>').status_code, 302)
        shirt = self.shirt()
        image_url = '/tshirts/%d.png' % shirt['id']
        with self.client.get(image_url) as response:
            self.assertEqual(response.status_code, 200)
            self.assertEqual(response.headers['Cache-Control'], 'private, no-store')
            with Image.open(io.BytesIO(response.data)) as image:
                self.assertEqual(image.size, (256, 256))
                self.assertEqual(image.mode, 'RGBA')
                self.assertEqual(image.getpixel((0, 0))[3], 0)
                self.assertFalse(image.info)
        self.assertEqual(self.other.get(image_url).status_code, 404)
        self.assertEqual(self.browser().get(image_url).status_code, 404)
        self.assertNotIn(b'Logo', self.other.get('/marketplace').data)
        self.assertEqual(self.post(self.other, '/marketplace', tshirt=str(shirt['id'])).status_code, 404)
        self.assertEqual(self.post(self.other, '/manage/moderation', tshirt=str(shirt['id']), action='approve').status_code, 403)
        with self.owner.session_transaction(base_url='https://localhost') as state:
            state['admin'] = True
        self.assertEqual(self.owner.get(image_url, base_url='https://localhost').status_code, 200)
        self.assertEqual(self.owner.get(image_url).status_code, 404)
        self.assertEqual(self.review(shirt['id'], 'approve').status_code, 302)
        self.assertIn(b'&lt;script&gt;Logo&lt;/script&gt;', self.other.get('/marketplace').data)
        self.assertEqual(self.browser().get(image_url).status_code, 200)
        self.assertEqual(self.post(self.other, '/marketplace', tshirt=str(shirt['id'])).status_code, 302)
        self.assertIn(b'Wearing', self.other.get('/marketplace').data)
        self.assertEqual(self.post(self.other, '/play/1', server='new').status_code, 302)
        room = next(iter(grid.rooms.values()))
        access = room['status'].with_suffix('.json.access')
        self.assertIn('http://blox.ralsei.tech'+image_url, access.read_text())
        self.assertEqual(self.review(shirt['id'], 'reject', 'Please use another image.').status_code, 302)
        self.assertNotIn(image_url, access.read_text())
        with grid.app.app_context():
            self.assertIsNone(grid.db().execute('SELECT tshirt_id FROM users WHERE id=?', (second,)).fetchone()[0])
        self.assertEqual(self.other.get(image_url).status_code, 404)
        self.assertIn(b'Please use another image.', self.client.get('/tshirts/upload').data)

    def test_invalid_upload_limits_and_csrf(self):
        self.members()
        self.assertEqual(self.browser().get('/tshirts/upload').status_code, 403)
        self.assertEqual(self.client.post('/tshirts/upload', data={'image': (io.BytesIO(self.image()), 'logo.png')}).status_code, 403)
        for data in (b'<svg xmlns="http://www.w3.org/2000/svg"/>', self.image()[:-40], b'x'*(2*1024*1024+1), self.image((1025, 2))):
            self.assertEqual(self.upload(data).status_code, 400)
        for _ in range(5):
            self.assertEqual(self.upload().status_code, 302)
        self.assertEqual(self.upload().status_code, 400)
        with grid.app.app_context():
            self.assertEqual(grid.db().execute('SELECT COUNT(*) FROM tshirts').fetchone()[0], 5)
        with patch.dict(grid.os.environ, GRID_INSECURE='0'):
            self.assertEqual(self.owner.get('/manage/moderation').status_code, 403)
        self.assertEqual(self.client.get('/friends/1/remove').status_code, 405)

    def test_removing_creator_clears_friendships_and_worn_items(self):
        first, second = self.members()
        self.post(self.client, '/friends', nickname='SecondPlayer')
        self.post(self.other, '/friends/%d/accept' % first)
        self.upload()
        sid = self.shirt()['id']
        self.review(sid, 'approve')
        self.post(self.other, '/marketplace', tshirt=str(sid))
        self.assertEqual(self.post(self.owner, '/manage/users/%d/delete' % first, confirm='delete').status_code, 302)
        with grid.app.app_context():
            self.assertEqual(grid.db().execute('SELECT COUNT(*) FROM friendships').fetchone()[0], 0)
            self.assertIsNone(grid.db().execute('SELECT tshirt_id FROM users WHERE id=?', (second,)).fetchone()[0])
            self.assertEqual(grid.db().execute('SELECT state FROM tshirts WHERE id=?', (sid,)).fetchone()[0], 'rejected')

    def test_private_servers_owner_friends_only_hidden_from_others_and_auto_join(self):
        first, second = self.members()
        stranger = self.browser()
        self.signup('StrangerPlayer', client=stranger)
        with grid.app.app_context():
            grid.db().execute('UPDATE places SET player_limit=4 WHERE id=1')
            grid.db().commit()
        self.assertEqual(self.post(self.client, '/play/1', server='private').status_code, 302)
        room = next(iter(grid.rooms.values()))
        self.assertEqual(room['private_owner'], first)
        self.assertEqual(self.other.get('/api/places/1/servers').json['servers'], [])
        self.assertEqual(self.browser().get('/api/places/1/servers').json['servers'], [])
        self.assertEqual(self.post(self.other, '/play/1', server=str(room['id'])).status_code, 403)
        self.post(self.client, '/friends', nickname='SecondPlayer')
        self.assertEqual(self.post(self.other, '/play/1', server=str(room['id'])).status_code, 403)
        self.post(self.other, '/friends/%d/accept' % first)
        self.assertEqual(self.other.get('/api/places/1/servers').json['servers'][0]['id'], room['id'])
        self.assertEqual(self.post(self.other, '/play/1', server=str(room['id'])).status_code, 302)
        self.assertEqual(self.post(stranger, '/play/1', server=str(room['id'])).status_code, 403)
        self.assertEqual(self.post(stranger, '/play/1', server='auto').status_code, 302)
        self.assertEqual(len(grid.rooms), 2)
        self.assertEqual(self.post(self.client, '/play/1', server='private').status_code, 302)
        self.assertEqual(len(grid.rooms), 2)
        self.assertEqual(self.post(self.other, '/servers/%d/stop' % room['id']).status_code, 403)
        self.assertEqual(self.client.post('/servers/%d/stop' % room['id']).status_code, 403)
        self.assertEqual(self.post(self.client, '/servers/%d/stop' % room['id']).status_code, 302)
        self.assertNotIn(room['id'], grid.rooms)

    def test_private_server_unfriend_revokes_tickets_access_and_presence(self):
        first, second = self.members()
        with grid.app.app_context():
            grid.db().execute('UPDATE places SET player_limit=4 WHERE id=1')
            grid.db().commit()
        self.post(self.client, '/friends', nickname='SecondPlayer')
        self.post(self.other, '/friends/%d/accept' % first)
        self.post(self.client, '/play/1', server='private')
        room = next(iter(grid.rooms.values()))
        response = self.post(self.other, '/play/1', server=str(room['id']))
        room['status'].write_text(json.dumps(dict(players=[dict(id=second)], clients=1)))
        access = room['status'].with_suffix('.json.access')
        self.assertIn('SecondPlayer', access.read_text())
        self.assertIn(b'Join friend', self.other.get('/friends').data)
        self.post(self.client, '/friends/%d/remove' % second)
        self.assertNotIn('SecondPlayer', access.read_text())
        self.assertEqual(self.other.get(response.location).status_code, 410)
        self.assertEqual(self.post(self.other, '/play/1', server=str(room['id'])).status_code, 403)
        self.assertEqual(self.other.get('/api/places/1/servers').json['servers'], [])
        room['process'].terminate.assert_not_called()

    def test_admin_restart_preserves_private_owner_and_ban_stops_owned_room(self):
        first, second = self.members()
        self.post(self.client, '/play/1', server='private')
        room = next(iter(grid.rooms.values()))
        with patch.object(grid, 'validate_server_build'):
            self.assertEqual(self.post(self.owner, '/manage/servers/%d/restart' % room['id']).status_code, 302)
        replacement = next(iter(grid.rooms.values()))
        self.assertEqual(replacement['private_owner'], first)
        self.assertEqual(self.post(self.owner, '/manage/users/%d/ban' % first).status_code, 302)
        self.assertFalse(grid.rooms)
