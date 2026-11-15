import io
import gzip
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
from email.message import Message
from unittest.mock import patch
import urllib.error
import urllib.request
import app as proxy


class AssetTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.cache = Path(self.scratch.name)
        self.setting = patch.object(proxy, 'CACHE', self.cache)
        self.setting.start()
        self.addCleanup(self.setting.stop)
        self.client = proxy.app.test_client()

    def response(self, body=b'version 1.00\n992\nmesh', mime='application/octet-stream', length=None):
        result = io.BytesIO(body)
        result.status = 200
        result.headers = Message()
        result.headers['Content-Type'] = mime
        result.headers['Content-Length'] = str(len(body) if length is None else length)
        return result

    def test_first_download_and_permanent_offline_cache_range_and_head(self):
        body = b'version 1.00\n992\nmesh\x00\xff'
        with patch.object(proxy.urllib.request, 'build_opener') as build:
            build.return_value.open.return_value = self.response(body)
            r = self.client.get('/1029523')
            self.assertEqual(r.status_code, 200)
            self.assertEqual(r.data, body)
            self.assertEqual(r.headers['X-Asset-Cache'], 'MISS')
            self.assertEqual(build.return_value.open.call_args.args[0].full_url,
                             'https://assetdelivery.roblox.com/v1/asset/?id=1029523')
            self.assertNotIn('Cookie', build.return_value.open.call_args.args[0].headers)
            self.assertEqual(build.return_value.open.call_args.args[0].get_header('User-agent'), 'Roblox')
        with patch.object(proxy, 'fetch', side_effect=AssertionError('Cached asset contacted upstream')):
            for client in (self.client, proxy.app.test_client()):
                r = client.get('/1029523')
                self.assertEqual(r.data, body)
                self.assertEqual(r.headers['X-Asset-Cache'], 'HIT')
                self.assertIn('immutable', r.headers['Cache-Control'])
            r = self.client.get('/1029523', headers={'Range': 'bytes=0-6'})
            self.assertEqual(r.status_code, 206)
            self.assertEqual(r.data, b'version')
            self.assertEqual(self.client.head('/1029523').headers['Content-Length'], str(len(body)))
        self.assertEqual((self.cache/'1029523/body').read_bytes(), body)

    def test_failure_is_not_cached_and_later_request_can_succeed(self):
        for response in (self.response(b''), self.response(b'bad', length=100),
                         self.response(b'<html>Error</html>', 'text/html'), self.response(b'{"errors":[]}')):
            with patch.object(proxy.urllib.request, 'build_opener') as build:
                build.return_value.open.return_value = response
                self.assertEqual(self.client.get('/1').status_code, 502)
                self.assertFalse((self.cache/'1').exists())
        with patch.object(proxy.urllib.request, 'build_opener') as build:
            build.return_value.open.side_effect = urllib.error.HTTPError('upstream', 404, 'missing', {}, None)
            r = self.client.get('/1')
            self.assertEqual(r.status_code, 404)
            self.assertEqual(r.headers['Cache-Control'], 'no-store')
        with patch.object(proxy.urllib.request, 'build_opener') as build:
            build.return_value.open.return_value = self.response()
            self.assertEqual(self.client.get('/1').status_code, 200)
        self.assertFalse(list(self.cache.glob('.fetch-*')))

    def test_only_ids_and_safe_upstream_redirects(self):
        with patch.object(proxy, 'fetch', side_effect=AssertionError('Bad input fetched')):
            for path in ('/0', '/-1', '/001', '/123x', '/99999999999999999999', '/http://example.com', '/1/../../admin'):
                self.assertEqual(self.client.get(path).status_code, 404)
            self.assertEqual(self.client.post('/1').status_code, 405)
        redirects = proxy.AssetRedirects()
        req = urllib.request.Request('https://assetdelivery.roblox.com/v1/asset/?id=1')
        for url in ('http://rbxcdn.com/asset', 'https://evil.com/file', 'https://rbxcdn.com.evil.com/a',
                    'https://user:password@rbxcdn.com/a', 'https://rbxcdn.com:8080/a'):
            with self.assertRaises(ValueError):
                redirects.redirect_request(req, None, 302, '', {}, url)
        self.assertEqual(redirects.redirect_request(req, None, 302, '', {}, 'https://c0.rbxcdn.com/mesh').full_url,
                         'https://c0.rbxcdn.com/mesh')

    def test_concurrent_misses_download_once(self):
        calls = []
        def download(asset_id, directory):
            calls.append(asset_id)
            time.sleep(.05)
            (directory/'body').write_bytes(b'mesh')
            (directory/'meta.json').write_text(json.dumps({'mime':'application/octet-stream','sha256':'mesh'}))
        def get(_index):
            with proxy.app.test_client() as client:
                return client.get('/123').data
        with patch.object(proxy, 'fetch', side_effect=download):
            with ThreadPoolExecutor(max_workers=8) as pool:
                self.assertEqual(list(pool.map(get, range(8))), [b'mesh']*8)
        self.assertEqual(calls, ['123'])

    def test_private_cookie_only_used_on_initial_upstream_request(self):
        cookies = self.cache/'cookies.txt'
        cookies.write_text('# Netscape HTTP Cookie File\n.roblox.com\tTRUE\t/\tTRUE\t2147483647\t.ROBLOSECURITY\ttest-secret-not-real\n.example.com\tTRUE\t/\tTRUE\t2147483647\tunrelated\tdont-forward\n')
        with patch.dict(proxy.os.environ, ASSET_PROXY_COOKIE_FILE=str(cookies)):
            with patch.object(proxy.urllib.request, 'build_opener') as build:
                requests = []
                def open_upstream(req, timeout):
                    requests.append(dict(req.header_items()))
                    if len(requests) == 1:
                        raise urllib.error.HTTPError(req.full_url,401,'authentication required',{},io.BytesIO())
                    return self.response()
                build.return_value.open.side_effect = open_upstream
                r = self.client.get('/123')
                self.assertEqual(r.status_code, 200)
                r.close()
                req = build.return_value.open.call_args.args[0]
                self.assertNotIn('Cookie', requests[0])
                self.assertEqual(len(requests), 2)
                self.assertEqual(req.get_header('Cookie'), '.ROBLOSECURITY=test-secret-not-real')
                redirected = proxy.AssetRedirects().redirect_request(req, None, 302, '', {}, 'https://c0.rbxcdn.com/asset')
                self.assertIsNone(redirected.get_header('Cookie'))
                self.assertNotIn('test-secret', str(dict(r.headers)))

    def test_public_download_does_not_read_cookie_and_not_found_does_not_retry(self):
        with patch.object(proxy, 'upstream_cookie', side_effect=AssertionError('Public request read cookie')):
            with patch.object(proxy.urllib.request, 'build_opener') as build:
                build.return_value.open.return_value = self.response()
                with self.client.get('/123') as response:
                    self.assertEqual(response.status_code, 200)
                self.assertEqual(build.return_value.open.call_count, 1)
                build.return_value.open.side_effect = urllib.error.HTTPError('upstream',404,'missing',{},None)
                with self.client.get('/124') as response:
                    self.assertEqual(response.status_code, 404)
                self.assertEqual(build.return_value.open.call_count, 2)

    def test_gzip_assets_are_decoded_before_archiving_and_truncated_gzip_is_not_saved(self):
        plain=b'print("old linked script")\r\n'*100
        compressed=gzip.compress(plain)
        with patch.object(proxy.urllib.request,'build_opener') as build:
            response=self.response(compressed)
            response.headers['Content-Encoding']='gzip'
            build.return_value.open.return_value=response
            with self.client.get('/123') as saved:
                self.assertEqual(saved.status_code,200)
                self.assertEqual(saved.data,plain)
                self.assertNotIn('Content-Encoding',saved.headers)
        self.assertEqual((self.cache/'123/body').read_bytes(),plain)
        with patch.object(proxy.urllib.request,'build_opener') as build:
            response=self.response(compressed[:-8])
            response.headers['Content-Encoding']='gzip'
            build.return_value.open.return_value=response
            with self.client.get('/124') as failed:self.assertEqual(failed.status_code,502)
        self.assertFalse((self.cache/'124').exists())

    def test_roblox_asset_permission_failure_is_not_a_proxy_failure_or_cached(self):
        with patch.object(proxy.urllib.request,'build_opener') as build:
            build.return_value.open.side_effect=urllib.error.HTTPError('upstream',409,'access denied',{},None)
            with self.client.get('/1014650') as response:
                self.assertEqual(response.status_code,403)
                self.assertEqual(response.headers['X-Asset-Upstream-Status'],'409')
                self.assertEqual(response.headers['Cache-Control'],'no-store')
        self.assertFalse((self.cache/'1014650').exists())


if __name__ == '__main__':
    unittest.main()
