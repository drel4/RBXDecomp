import io, os, json, tempfile, time, unittest
from pathlib import Path
from unittest.mock import Mock, patch
from werkzeug.security import generate_password_hash
scratch=tempfile.TemporaryDirectory()
os.environ.update(GRID_DATA=scratch.name,GRID_SECRET='isolated-regression-secret',GRID_ADMIN_HASH=generate_password_hash('test-password'),GRID_INSECURE='1',GRID_DISABLE_IDLE_MONITOR='1')
import app as grid

class GridTests(unittest.TestCase):
    def setUp(self):
        grid.rooms.clear()
        grid.place_versions.clear()
        with grid.app.app_context():
            for table in ('friendships','tshirts','room_tickets','room_guests','server_runs','places','account_sessions','users','invites','auth_limits'):
                grid.db().execute('DELETE FROM '+table)
            grid.db().execute("UPDATE instance_settings SET value='0' WHERE key='registration_open'")
            grid.db().execute("DELETE FROM instance_settings WHERE key='server_backend'")
            grid.db().execute("INSERT INTO places(id,title,description,sha,published,thumb,created,player_limit) VALUES(1,'Crossroads','Map','test',1,'ready',0,1)")
            grid.db().commit()
        self.client=grid.app.test_client()
        self.client.get('/')
        self.patcher=patch.object(grid.subprocess,'Popen',side_effect=self.worker)
        self.patcher.start();self.addCleanup(self.patcher.stop)

    def worker(self,args,**kwargs):
        kwargs['stdout'].write(b'SERVER_READY\n');kwargs['stdout'].flush()
        process=Mock();process.poll.return_value=None
        process.terminate.side_effect=lambda:setattr(process.poll,'return_value',0)
        return process

    def post(self,path,data=None,client=None):
        client=client or self.client
        if path.startswith('/play/'):
            nickname=(data or {}).get('nickname','Builder')
            with grid.app.app_context():
                row=grid.db().execute('SELECT id FROM users WHERE nickname=?',(nickname,)).fetchone()
                uid=row[0] if row else grid.db().execute('INSERT INTO users(nickname,password_hash,created,colors) VALUES(?,?,?,?)',
                    (nickname,'test-fixture-not-a-password',0,json.dumps([24,23,24,24,119,119]))).lastrowid
                token=grid.secrets.token_urlsafe(32)
                grid.db().execute('INSERT INTO account_sessions VALUES(?,?,?)',
                    (grid.hashlib.sha256(token.encode()).hexdigest(),uid,int(time.time())+1000))
                grid.db().commit()
            with client.session_transaction() as state:state['account_token']=token
        with client.session_transaction() as state:csrf=state['csrf']
        return client.post(path,data=dict(csrf=csrf,**(data or {})))

    def login(self):
        self.assertEqual(self.post('/admin/login',{'password':'test-password'}).status_code,302)

    def test_language_choice_persists_and_user_content_is_not_translated(self):
        self.assertIn(b'lang="en"',self.client.get('/').data)
        choice=self.client.get('/language/ru?next=/signin')
        self.assertEqual(choice.status_code,302)
        self.assertEqual(choice.location,'/signin')
        self.assertIn('Max-Age=31536000',choice.headers['Set-Cookie'])
        self.assertIn('HttpOnly',choice.headers['Set-Cookie'])
        for scheme in ('http','https'):
            page=self.client.get('/signin',base_url=scheme+'://localhost')
            self.assertIn('Зарегистрироваться',page.get_data(as_text=True))
            self.assertIn('Регистрация по приглашениям',page.get_data(as_text=True))
            self.assertIn('lang="ru"',page.get_data(as_text=True))
        with grid.app.app_context():
            grid.db().execute("UPDATE places SET title='Play',description='Nickname <script>unsafe</script>' WHERE id=1")
            grid.db().commit()
        home=self.client.get('/').get_data(as_text=True)
        self.assertIn('>Play</a>',home)
        self.assertIn('Nickname &lt;script&gt;unsafe&lt;/script&gt;',home)
        self.assertIn('>Играть</a>',home)
        self.post('/logout')
        self.assertIn('lang="ru"',self.client.get('/').get_data(as_text=True))
        self.client.get('/language/en?next=/')
        self.assertIn(b'lang="en"',self.client.get('/',headers={'Accept-Language':'ru-RU,ru;q=0.9'}).data)
        fresh=grid.app.test_client()
        self.assertIn(b'lang="ru"',fresh.get('/',headers={'Accept-Language':'ru-RU,ru;q=0.9'}).data)

    def test_language_redirects_localized_admin_and_errors(self):
        for target in ('https://example.com/','//example.com/','/\\example.com/','/\r\nLocation:bad'):
            response=self.client.get('/language/ru',query_string={'next':target})
            self.assertEqual(response.location,'/')
        self.assertEqual(self.client.get('/language/fr').status_code,404)
        missing=self.client.get('/missing-language-test-page')
        self.assertEqual(missing.status_code,404)
        self.assertIn('Страница не найдена.',missing.get_data(as_text=True))
        wrong_method=self.post('/download')
        self.assertEqual(wrong_method.status_code,405)
        self.assertIn('Этот метод запроса не разрешён',wrong_method.get_data(as_text=True))
        self.login()
        page=self.client.get('/manage').get_data(as_text=True)
        for label in ('Управление плейсами','Серверная сборка','Обновить миниатюру','Снять с публикации'):
            self.assertIn(label,page)
        bad=self.post('/manage/1/edit',{'title':''})
        self.assertEqual(bad.status_code,400)
        self.assertIn('Введите название',bad.get_data(as_text=True))
        rejected=self.client.post('/manage/1/edit',data={'title':'No csrf'})
        self.assertEqual(rejected.status_code,403)
        self.assertIn('Срок действия формы истёк',rejected.get_data(as_text=True))
        self.assertEqual(self.post('/manage/registration',{'mode':'open'}).status_code,302)
        self.assertIn('Регистрация теперь открыта.',self.client.get('/manage/registration').get_data(as_text=True))
        with patch.dict(os.environ,GRID_INSECURE='0'):
            self.assertEqual(self.client.get('/manage').status_code,403)
            self.assertEqual(self.client.get('/manage',base_url='https://localhost').status_code,302)

    def test_translation_catalogue_and_embedded_download_hook(self):
        import ast,re,i18n
        from jinja2 import nodes
        keys=set()
        templates=Path(grid.__file__).parent/'templates'
        for path in templates.glob('*.html'):
            tree=grid.app.jinja_env.parse(path.read_text(encoding='utf-8'))
            for call in tree.find_all(nodes.Call):
                if isinstance(call.node,nodes.Name) and call.node.name=='_':
                    keys.update(c.value for c in call.find_all(nodes.Const) if isinstance(c.value,str))
        for name in ('app.py','accounts.py','asset_admin.py','community.py'):
            for call in ast.walk(ast.parse((templates.parent/name).read_text(encoding='utf-8'))):
                if isinstance(call,ast.Call) and isinstance(call.func,ast.Name) and call.func.id=='_':
                    keys.update(arg.value for arg in call.args if isinstance(arg,ast.Constant) and isinstance(arg.value,str))
        keys.difference_update(('Arm','Leg',' arm',' leg'))
        self.assertFalse(keys-set(i18n.RUSSIAN))
        for key,value in i18n.RUSSIAN.items():
            self.assertTrue(value)
            self.assertEqual(re.findall(r'%[sd]',key),re.findall(r'%[sd]',value))
        html=self.client.get('/').get_data(as_text=True)
        self.assertIn('window.external.GetApp()',html)
        self.assertIn('embedded-client',html)
        self.assertIn('client-download-link',html)
        self.assertIn('href="/download"',html)
        stylesheet=(Path(grid.app.static_folder)/'site.css').read_text(encoding='utf-8')
        self.assertIn('.embedded-client .client-download-link{display:none}',stylesheet)
        notice='notblox is an independent instance and is not affiliated with Roblox Corporation in any way.'
        self.login()
        for code in ('en','ru'):
            self.client.get('/language/'+code)
            expected=i18n.RUSSIAN[notice] if code=='ru' else notice
            for path in ('/','/login','/signin','/download','/manage','/missing-page'):
                page=self.client.get(path).get_data(as_text=True)
                self.assertIn(expected,page)
                self.assertIn('id="footer"',page)
            self.assertIn('https://github.com/OpenRBX-org/RBXDecomp',self.client.get('/download').get_data(as_text=True))

    def test_asset_admin_cookie_replacement_and_download_testing(self):
        import asset_admin,re
        with tempfile.TemporaryDirectory() as folder, patch.dict(os.environ, GRID_ASSET_PROXY_SECRETS=folder):
            self.assertEqual(self.client.get('/manage/assets').status_code,302)
            self.assertEqual(self.post('/manage/assets',{'action':'test','asset_id':'1029523'}).status_code,403)
            self.login()
            page=self.client.get('/manage/assets').get_data(as_text=True)
            self.assertIn('value="1029523"',page)
            self.assertIn('Not configured',page)
            tokens=re.findall(r'name="csrf" value="([^"]*)"',page)
            with self.client.session_transaction() as state:expected=state['csrf']
            self.assertEqual(tokens,[expected,expected])
            fixture=b'# Netscape HTTP Cookie File\n.roblox.com\tTRUE\t/\tTRUE\t2147483647\t.ROBLOSECURITY\ttest-only-not-a-real-secret\n.example.com\tTRUE\t/\tTRUE\t2147483647\tother\tnot-kept\n'
            response=self.client.post('/manage/assets',data={'csrf':tokens[0],'action':'cookies','cookies':(io.BytesIO(fixture.replace(b'\n',b'\r\n')),'cookies.txt')})
            self.assertEqual(response.status_code,200)
            self.assertNotIn(b'test-only-not-a-real-secret',response.data)
            saved=Path(folder)/'cookies.txt'
            self.assertIn('test-only-not-a-real-secret',saved.read_text())
            self.assertNotIn('not-kept',saved.read_text())
            original=saved.read_bytes()
            rejected=self.post('/manage/assets',{'action':'cookies','cookies':(io.BytesIO(b'bad export'),'cookies.txt')})
            self.assertEqual(rejected.status_code,400)
            self.assertEqual(saved.read_bytes(),original)
            self.assertEqual(self.client.post('/manage/assets',data={'action':'test','asset_id':'1029523'}).status_code,403)
            self.assertEqual(self.post('/manage/assets',{'action':'test','asset_id':'http://example.com/'}).status_code,400)
            with patch.object(asset_admin.urllib.request,'urlopen') as fetch:
                fetch.return_value.__enter__.return_value.headers={'Content-Length':'204469','X-Asset-Cache':'HIT'}
                response=self.client.post('/manage/assets',data={'csrf':tokens[1],'action':'test','asset_id':'1029523'})
                self.assertEqual(response.status_code,200)
                self.assertIn(b'204469',response.data)
                self.assertIn(b'Saved copy',response.data)
                self.assertEqual(fetch.call_args.args[0],'http://127.0.0.1:4381/1029523')
            with patch.dict(os.environ,GRID_INSECURE='0'):
                self.assertEqual(self.client.get('/manage/assets').status_code,403)

    def test_public_download_page_and_exact_patch_attachment(self):
        patch=grid.DATA/'downloads/notblox.xdelta'
        patch.parent.mkdir(exist_ok=True)
        patch.unlink(missing_ok=True)
        for base in ('http://localhost','https://localhost'):
            client=grid.app.test_client()
            page=client.get('/download',base_url=base)
            self.assertEqual(page.status_code,200)
            self.assertNotIn('Location',page.headers)
            for link in (b'https://archive.org/download/ROBLOX20072013/December%2029%202007%20%280.3.676.0%29.zip',
                         b'/downloads/notblox.xdelta',b'https://kotcrab.github.io/xdelta-wasm/',
                         b'https://github.com/OpenRBX-org/RBXDecomp'):
                self.assertIn(link,page.data)
            self.assertIn(b'Source file',page.data)
            self.assertIn(b'Patch file',page.data)
            self.assertIn(b'Apply Patch',page.data)
            self.assertIn(b'/download',client.get('/',base_url=base).data)
        self.assertEqual(self.client.get('/downloads/notblox.xdelta').status_code,404)
        payload=b'\xd6\xc3\xc4\x00\x00binary-patch-test'
        patch.write_bytes(payload)
        try:
            for base in ('http://localhost','https://localhost'):
                response=grid.app.test_client().get('/downloads/notblox.xdelta',base_url=base)
                self.assertEqual(response.status_code,200)
                self.assertEqual(response.data,payload)
                self.assertEqual(response.mimetype,'application/octet-stream')
                self.assertIn('attachment;',response.headers['Content-Disposition'])
                self.assertIn('notblox.xdelta',response.headers['Content-Disposition'])
                response.close()
        finally:
            patch.unlink()

    def test_place_asset_proxy_policy_persists_and_server_join_uses_snapshot(self):
        self.login()
        self.assertIn(b'name="asset_proxy"',self.client.get('/manage/1/edit').data)
        with grid.app.app_context():self.assertEqual(grid.place(1,True)['asset_proxy'],'user-choice')
        self.assertEqual(self.post('/manage/1/edit',{'title':'Crossroads','asset_proxy':'invalid'}).status_code,400)
        for policy,mode,worker_value in (('enabled',1,'1'),('disabled',0,'0'),('user-choice',-1,'1')):
            with patch.object(grid.jobs,'submit'):
                response=self.post('/manage/1/edit',{'title':'Crossroads','description':'Map','asset_proxy':policy})
            self.assertEqual(response.status_code,302)
            with grid.app.app_context():
                row=grid.place(1,True)
                self.assertEqual(row['asset_proxy'],policy)
                self.assertEqual(grid.worker_environment(row)['OPENRBX_ASSET_PROXY'],worker_value)
            join=self.post('/play/1',{'nickname':'PolicyTester','server':'new'})
            room=list(grid.rooms.values())[-1]
            self.assertEqual(room['asset_proxy'],policy)
            self.assertEqual(grid.subprocess.Popen.call_args.kwargs['env']['OPENRBX_ASSET_PROXY'],worker_value)
            # Editing metadata must not change the policy of an existing room.
            with grid.app.app_context():
                grid.db().execute("UPDATE places SET asset_proxy='disabled' WHERE id=1")
                grid.db().commit()
            token=join.location.rsplit('/',1)[-1]
            script=self.client.get('/game/join.ashx?ticket='+token).get_data(as_text=True)
            self.assertIn('game:SetAssetProxyPolicy(%d)'%mode,script)
            self.assertLess(script.index('SetAssetProxyPolicy'),script.index('client:Connect'))
            room['process'].poll.return_value=0
        with grid.app.app_context():
            grid.db().execute("UPDATE places SET asset_proxy='enabled' WHERE id=1");grid.db().commit()
        self.assertEqual(self.post('/manage/1/edit',{'title':'Crossroads','description':'Map'}).status_code,302)
        with grid.app.app_context():self.assertEqual(grid.place(1,True)['asset_proxy'],'enabled')

    def test_native_server_paths_and_wine_thumbnail_stay_separate(self):
        with tempfile.TemporaryDirectory() as directory:
            runtime=Path(directory)
            (runtime/'grid-server').touch()
            row={'sha':'map'}
            with patch.object(grid,'RUNTIME',runtime), patch.dict(os.environ,{'GRID_SERVER_BACKEND':'native'}):
                args=grid.worker_args('server',row,45001)
                self.assertEqual(args,[str(runtime/'grid-server'),'server',str(grid.DATA/'places/map.rbxl'),str(runtime/'content'),'45001'])
                args=grid.worker_args('thumbnail',row,grid.winepath(runtime/'out.b64'))
                self.assertEqual(args[:3],['wine',str(runtime/'grid-worker.exe'),'thumbnail'])
                self.assertTrue(args[3].startswith('Z:'))
                self.assertTrue(args[4].startswith('Z:'))
            with patch.object(grid,'RUNTIME',runtime), patch.dict(os.environ,{'GRID_SERVER_BACKEND':'wine'}):
                self.assertEqual(grid.worker_args('server',row,45001)[0],'wine')
            (runtime/'grid-server').unlink()
            with patch.object(grid,'RUNTIME',runtime), patch.dict(os.environ,{'GRID_SERVER_BACKEND':'native'}):
                with self.assertRaises(RuntimeError): grid.worker_args('server',row,45001)

    def test_select_installed_builds_persists_without_stopping_rooms(self):
        self.post('/play/1',{'server':'new'})
        existing=next(iter(grid.rooms.values()))
        self.assertEqual(self.post('/manage/server-build',{'backend':'native64'}).status_code,403)
        self.login()
        with tempfile.TemporaryDirectory() as directory, patch.object(grid,'RUNTIME',Path(directory)):
            for label,filename in grid.SERVER_BUILDS.values():(Path(directory)/filename).touch()
            self.assertEqual(self.post('/manage/server-build',{'backend':'../../bad'}).status_code,400)
            self.assertEqual(self.client.post('/manage/server-build',data={'backend':'wine'}).status_code,403)
            for backend in ('native','native64','wine'):
                self.assertEqual(self.post('/manage/server-build',{'backend':backend}).status_code,302)
                self.assertEqual(grid.server_backend(),backend)
                self.assertEqual(self.client.get('/health').json['server_backend'],backend)
                self.assertEqual(self.post('/manage/1/start').status_code,302)
                room=list(grid.rooms.values())[-1]
                self.assertEqual(room['backend'],backend)
                args=grid.subprocess.Popen.call_args.args[0]
                if backend=='wine':
                    self.assertEqual(args[0],'wine');self.assertTrue(args[-1].startswith('Z:'))
                else:
                    self.assertEqual(args[0],str(Path(directory)/grid.SERVER_BUILDS[backend][1]))
                    self.assertEqual(args[-1],str(room['status']))
                self.assertEqual(grid.worker_args('thumbnail',{'sha':'test'},'out')[0],'wine')
            existing['process'].terminate.assert_not_called()
            (Path(directory)/'grid-server-amd64').unlink()
            self.assertEqual(self.post('/manage/server-build',{'backend':'native64'}).status_code,409)
            self.assertEqual(grid.server_backend(),'wine')
            with patch.dict(os.environ,GRID_INSECURE='0'):
                self.assertEqual(self.post('/manage/server-build',{'backend':'native'}).status_code,403)

    def test_replace_place_keeps_identity_running_map_and_shared_files(self):
        old=grid.DATA/'places/test.rbxl';old.write_bytes(b'old place')
        self.post('/play/1',{'server':'new'})
        room=next(iter(grid.rooms.values()))
        self.login()
        payload=b'<roblox version="4"><!-- updated map --></roblox>'
        newsha=grid.hashlib.sha256(payload).hexdigest()
        original_thumb=grid.DATA/'thumbs/1.png';original_thumb.write_bytes(b'previous thumbnail')
        with patch.object(grid.jobs,'submit') as submit:
            result=self.post('/manage/1/edit',{'title':'Updated','description':'New map','place':(io.BytesIO(payload),'replacement.rbxl')})
            self.assertEqual(result.status_code,302)
            submit.assert_called_once_with(grid.thumbnail,1)
        with grid.app.app_context():
            row=grid.place(1,True)
            self.assertEqual((row['id'],row['sha'],row['published'],row['player_limit']),(1,newsha,1,1))
        self.assertEqual((grid.DATA/'places'/(newsha+'.rbxl')).read_bytes(),payload)
        self.assertEqual(old.read_bytes(),b'old place')
        self.assertEqual(original_thumb.read_bytes(),b'previous thumbnail')
        room['process'].terminate.assert_not_called()
        self.assertEqual(self.post('/manage/servers/'+str(room['id'])+'/stop').status_code,302)
        self.assertFalse(old.exists())
        with patch.object(grid.jobs,'submit') as submit:
            self.assertEqual(self.post('/manage/1/edit',{'title':'Bad','place':(io.BytesIO(b'bad'),'broken.rbxl')}).status_code,400)
            submit.assert_not_called()
        with grid.app.app_context():self.assertEqual(grid.place(1,True)['sha'],newsha)

    def test_restart_applies_selected_build_and_latest_map(self):
        self.post('/play/1',{'server':'new'})
        old=next(iter(grid.rooms.values()))
        self.assertEqual(self.post('/manage/servers/'+str(old['id'])+'/restart').status_code,403)
        self.login()
        with tempfile.TemporaryDirectory() as directory, patch.object(grid,'RUNTIME',Path(directory)):
            self.assertEqual(self.post('/manage/servers/'+str(old['id'])+'/restart').status_code,409)
            old['process'].terminate.assert_not_called()
            (Path(directory)/'grid-server-amd64').touch()
            self.assertEqual(self.post('/manage/server-build',{'backend':'native64'}).status_code,302)
            with patch.object(grid.jobs,'submit'):
                self.assertEqual(self.post('/manage/1/edit',{'title':'Latest','place':(io.BytesIO(b'<roblox version="4"/>'),'latest.rbxl')}).status_code,302)
            self.assertEqual(self.post('/manage/servers/'+str(old['id'])+'/restart').status_code,302)
            old['process'].terminate.assert_called_once()
            replacement=next(iter(grid.rooms.values()))
            self.assertNotEqual(replacement['id'],old['id'])
            self.assertEqual(replacement['backend'],'native64')
            self.assertEqual(replacement['sha'],grid.hashlib.sha256(b'<roblox version="4"/>').hexdigest())

    def test_edit_metadata_preserves_place_and_running_servers(self):
        self.post('/play/1',{'nickname':'Builder','server':'new'})
        room=next(iter(grid.rooms.values()))
        self.assertEqual(self.client.get('/manage/1/edit').status_code,403)
        self.assertEqual(self.post('/manage/1/edit',{'title':'Changed'}).status_code,403)
        self.login()
        page=self.client.get('/manage/1/edit')
        self.assertIn(b'value="Crossroads"',page.data)
        self.assertIn(b'>Map</textarea>',page.data)
        with grid.app.app_context():before=dict(grid.place(1,True))
        self.assertEqual(self.client.post('/manage/1/edit',data={'title':'Changed'}).status_code,403)
        title='New "title" <script>alert(1)</script>'
        description='First line\nSecond line & <b>plain text</b>'
        with patch.object(grid.jobs,'submit') as submit:
            response=self.post('/manage/1/edit',{'title':'  '+title+'  ','description':description})
            self.assertEqual(response.status_code,302)
            submit.assert_not_called()
        with grid.app.app_context():
            after=dict(grid.place(1,True))
            self.assertEqual(after,dict(before,title=title,description=description))
        self.assertIs(grid.rooms[room['id']],room)
        room['process'].terminate.assert_not_called()
        public=self.client.get('/places/1').data
        self.assertNotIn(b'<script>alert(1)</script>',public)
        self.assertIn(b'&lt;script&gt;',public)
        self.assertIn(b'First line\nSecond line &amp; &lt;b&gt;',public)
        self.assertEqual(self.client.get('/api/places').json['places'][0]['title'],title)
        self.assertEqual(self.post('/manage/1/edit',{'title':'No description','description':''}).status_code,302)
        with grid.app.app_context():self.assertEqual(grid.place(1,True)['description'],'')

    def test_edit_metadata_validation_drafts_and_https(self):
        self.login()
        for title,description in [('   ','Retain this text'),('x'*81,'Map'),('Valid','x'*2001)]:
            response=self.post('/manage/1/edit',{'title':title,'description':description})
            self.assertEqual(response.status_code,400)
            self.assertIn(description.encode(),response.data)
            with grid.app.app_context():self.assertEqual(grid.place(1,True)['title'],'Crossroads')
        self.assertEqual(self.client.get('/manage/999/edit').status_code,404)
        self.assertEqual(self.post('/manage/999/edit',{'title':'Missing'}).status_code,404)
        with grid.app.app_context():
            grid.db().execute('UPDATE places SET published=0');grid.db().commit()
        self.assertEqual(self.post('/manage/1/edit',{'title':'x'*80,'description':'y'*2000}).status_code,302)
        with grid.app.app_context():self.assertEqual(grid.place(1,True)['published'],0)
        with patch.dict(os.environ,GRID_INSECURE='0'):
            self.assertEqual(self.post('/manage/1/edit',{'title':'HTTP must fail'}).status_code,403)

    def test_multiple_servers_limits_names_and_stop(self):
        first=self.post('/play/1',{'nickname':'Builder','server':'new'})
        self.assertEqual(first.status_code,302)
        a=next(iter(grid.rooms.values()))
        self.assertEqual(a['limit'],1)
        other=grid.app.test_client();other.get('/')
        self.assertEqual(self.post('/play/1',{'nickname':'builder','server':str(a['id'])},other).status_code,409)
        self.assertEqual(self.post('/play/1',{'nickname':'Someone','server':str(a['id'])},other).status_code,409)
        second=self.post('/play/1',{'nickname':'Builder','server':'new'},other)
        self.assertEqual(second.status_code,302)
        b=list(grid.rooms.values())[1]
        self.assertNotEqual(a['port'],b['port'])
        self.assertEqual(len(self.client.get('/api/places/1/servers').json['servers']),2)
        self.assertEqual(self.client.get(first.location).status_code,200)
        self.assertTrue(grid.room_ready(a))
        a['log'].write_bytes(b'x'*70000)
        self.assertTrue(grid.room_ready(a))
        token=first.location.rsplit('/',1)[1]
        script=self.client.get('/game/join.ashx?ticket='+token)
        self.assertIn((','+str(a['port'])+',').encode(),script.data)
        self.assertIn(b'Builder',script.data)
        self.assertEqual(self.client.get('/game/join.ashx?ticket='+token).status_code,410)
        self.assertEqual(self.post('/manage/servers/'+str(a['id'])+'/stop',client=other).status_code,403)
        self.login()
        self.assertEqual(self.client.post('/manage/servers/'+str(a['id'])+'/stop').status_code,403)
        self.assertEqual(self.post('/manage/servers/'+str(b['id'])+'/stop').status_code,302)
        self.assertEqual(self.client.get(second.location).status_code,410)
        self.assertIn(a['id'],grid.rooms)
        self.assertNotIn(b['id'],grid.rooms)
        self.assertEqual(self.post('/manage/1/limit',{'player_limit':'2'}).status_code,302)
        self.assertEqual(self.post('/manage/1/start',{'count':'2'}).status_code,302)
        self.assertEqual([r['limit'] for r in grid.rooms.values()],[1,2,2])
        self.assertEqual(self.post('/manage/1/start',{'count':'16'}).status_code,503)
        self.assertEqual(self.post('/manage/1/limit',{'player_limit':'33'}).status_code,400)
        self.assertEqual(self.post('/manage/1/stop').status_code,302)
        self.assertFalse(grid.rooms)

    def test_disconnect_releases_nickname(self):
        self.post('/play/1',{'nickname':'Builder','server':'new'})
        room=next(iter(grid.rooms.values()))
        with grid.app.app_context():gid=grid.db().execute('SELECT account_id FROM room_guests').fetchone()[0]
        room['status'].write_text(json.dumps(dict(clients=1,players=[dict(id=gid,nickname='Builder')])))
        self.client.get('/places/1')
        with grid.app.app_context():
            grid.db().execute('UPDATE room_guests SET expires=0');grid.db().commit()
        self.client.get('/places/1')
        with grid.app.app_context():self.assertEqual(grid.db().execute('SELECT COUNT(*) FROM room_guests').fetchone()[0],1)
        with grid.app.app_context():
            grid.db().execute('UPDATE room_guests SET seen=0');grid.db().commit()
        room['status'].write_text(json.dumps(dict(clients=0,players=[],departed=[gid])))
        self.client.get('/places/1')
        other=grid.app.test_client();other.get('/')
        self.assertEqual(self.post('/play/1',{'nickname':'builder','server':str(room['id'])},other).status_code,302)

    def test_empty_server_timeout_and_active_player_reset(self):
        response=self.post('/play/1',{'nickname':'Builder','server':'new'})
        room=next(iter(grid.rooms.values()))
        room['status'].write_text('{"clients":0,"players":[]}')
        with patch.object(grid.time,'monotonic',return_value=100):grid.reap_idle_rooms()
        with patch.object(grid.time,'monotonic',return_value=114):grid.reap_idle_rooms()
        room['process'].terminate.assert_not_called()
        room['status'].write_text('{"clients":1,"players":[]}')
        with patch.object(grid.time,'monotonic',return_value=115):grid.reap_idle_rooms()
        self.assertIsNone(room['empty_since'])
        self.assertFalse(room.get('has_played',False))
        room['status'].write_text('{"clients":1,"players":[{"id":123}]}')
        with patch.object(grid.time,'monotonic',return_value=116):grid.reap_idle_rooms()
        self.assertTrue(room['has_played'])
        room['status'].write_text('{"clients":0,"players":[]}')
        with patch.object(grid.time,'monotonic',return_value=200):grid.reap_idle_rooms()
        with patch.object(grid.time,'monotonic',return_value=259):grid.reap_idle_rooms()
        room['process'].terminate.assert_not_called()
        with patch.object(grid.time,'monotonic',return_value=260):grid.reap_idle_rooms()
        room['process'].terminate.assert_called_once()
        self.assertNotIn(room['id'],grid.rooms)
        self.assertEqual(self.client.get(response.location).status_code,410)

    def test_fresh_server_stops_after_fifteen_seconds(self):
        self.post('/play/1',{'nickname':'Builder','server':'new'})
        room=next(iter(grid.rooms.values()))
        room['status'].write_text('{"clients":0,"players":[]}')
        with patch.object(grid.time,'monotonic',return_value=100):grid.reap_idle_rooms()
        with patch.object(grid.time,'monotonic',return_value=114):grid.reap_idle_rooms()
        room['process'].terminate.assert_not_called()
        with patch.object(grid.time,'monotonic',return_value=115):grid.reap_idle_rooms()
        room['process'].terminate.assert_called_once()

    def test_short_player_visit_promotes_timeout(self):
        self.post('/play/1',{'nickname':'Builder','server':'new'})
        room=next(iter(grid.rooms.values()))
        room['status'].write_text('{"clients":0,"players":[],"departed":[123]}')
        with patch.object(grid.time,'monotonic',return_value=100):grid.reap_idle_rooms()
        self.assertTrue(room['has_played'])
        with patch.object(grid.time,'monotonic',return_value=159):grid.reap_idle_rooms()
        room['process'].terminate.assert_not_called()
        with patch.object(grid.time,'monotonic',return_value=160):grid.reap_idle_rooms()
        room['process'].terminate.assert_called_once()

    def test_stale_status_does_not_stop_connected_server(self):
        self.post('/play/1',{'nickname':'Builder','server':'new'})
        room=next(iter(grid.rooms.values()))
        room['empty_since']=0
        room['status'].write_text('{"clients":0,"players":[]}')
        os.utime(room['status'],(0,0))
        with patch.object(grid.time,'monotonic',return_value=1000):grid.reap_idle_rooms()
        room['process'].terminate.assert_not_called()
        self.assertIsNone(room['empty_since'])

    def test_thumbnail_missing_assets_fallback_preserves_place(self):
        source=Path(scratch.name)/'places/test.rbxl'
        original=b'<roblox version="4"><Content name="Texture"><url>http://www.roblox.com/asset/?id=1</url></Content></roblox>'
        source.write_bytes(original)
        png=grid.base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a6z8AAAAASUVORK5CYII=')
        def render(args,**kwargs):
            if args[2]=='thumbnail':raise grid.subprocess.TimeoutExpired(args,20)
            self.assertEqual(args[2],'thumbnail-offline')
            # The worker receives a unique output path for each render.
            output = Path(args[-1][2:].replace('\\', '/'))
            output.write_bytes(grid.base64.b64encode(png))
        with patch.object(grid.subprocess,'run',side_effect=render):
            grid.thumbnail(1)
        self.assertEqual(source.read_bytes(),original)
        self.assertEqual((Path(scratch.name)/'thumbs/1.png').read_bytes(),png)
        with grid.app.app_context():self.assertEqual(grid.place(1,True)['thumb'],'ready')

    def test_delete_place_stops_servers_removes_files_and_tickets(self):
        response=self.post('/play/1',{'nickname':'Builder','server':'new'})
        room=next(iter(grid.rooms.values()))
        source=grid.DATA/'places/test.rbxl';source.write_bytes(b'map')
        image=grid.DATA/'thumbs/1.png';image.write_bytes(b'image')
        log=grid.DATA/'jobs/1-thumbnail.log';log.write_bytes(b'log')
        room['status'].write_text('{"clients":0,"players":[]}')
        self.assertEqual(self.client.get('/manage/1/delete').status_code,403)
        self.assertEqual(self.post('/manage/1/delete',{'confirm':'delete'}).status_code,403)
        self.login()
        self.assertIn(b'Delete place',self.client.get('/manage').data)
        self.assertIn(b'This cannot be undone',self.client.get('/manage/1/delete').data)
        self.assertTrue(source.exists())
        self.assertEqual(self.client.post('/manage/1/delete',data={'confirm':'delete'}).status_code,403)
        self.assertEqual(self.post('/manage/1/delete').status_code,400)
        self.assertEqual(self.post('/manage/1/delete',{'confirm':'delete'}).status_code,302)
        room['process'].terminate.assert_called_once()
        self.assertFalse(grid.rooms)
        for path in (source,image,log,room['log'],room['status']):self.assertFalse(path.exists(),path)
        for path in ('/places/1','/asset/1','/thumbs/1.png','/api/places/1/servers'):
            self.assertEqual(self.client.get(path).status_code,404,path)
        self.assertEqual(self.client.get(response.location).status_code,410)
        self.assertEqual(self.client.get('/api/places').json['places'],[])
        with grid.app.app_context():
            for table in ('places','server_runs','room_guests','room_tickets'):
                self.assertEqual(grid.db().execute('SELECT COUNT(*) FROM '+table).fetchone()[0],0)
        self.assertEqual(self.post('/manage/1/delete',{'confirm':'delete'}).status_code,404)

    def test_delete_preserves_identical_upload_until_last_place_removed(self):
        source=grid.DATA/'places/test.rbxl';source.write_bytes(b'shared map')
        with grid.app.app_context():
            grid.db().execute("INSERT INTO places(id,title,description,sha,created) VALUES(2,'Copy','','test',0)")
            grid.db().commit()
        self.login()
        self.assertEqual(self.post('/manage/1/delete',{'confirm':'delete'}).status_code,302)
        self.assertEqual(source.read_bytes(),b'shared map')
        self.assertEqual(self.post('/manage/2/delete',{'confirm':'delete'}).status_code,302)
        self.assertFalse(source.exists())

    def test_thumbnail_cannot_recreate_deleted_place_files(self):
        self.login()
        png=grid.base64.b64encode(b'\x89PNG\r\n\x1a\nrest')
        responses=[]
        def render(args,**kwargs):
            responses.append(self.post('/manage/1/delete',{'confirm':'delete'}).status_code)
            Path(args[-1][2:].replace('\\', '/')).write_bytes(png)
        with patch.object(grid.subprocess,'run',side_effect=render):grid.thumbnail(1)
        self.assertEqual(responses,[302])
        self.assertFalse((grid.DATA/'thumbs/1.png').exists())
        self.assertFalse(list((grid.DATA/'jobs').glob('1-*.b64')))
        self.assertFalse((grid.DATA/'jobs/1-thumbnail.log').exists())
        with patch.object(grid.subprocess,'run') as render:
            grid.thumbnail(1)  # Also ignore jobs queued before deletion.
            render.assert_not_called()

    def test_upload_and_http_admin_boundary(self):
        self.assertEqual(self.post('/manage').status_code,403)
        self.login()
        with patch.object(grid.jobs,'submit'):
            upload=self.post('/manage',{'title':'<script>title</script>','description':'Map','place':(io.BytesIO(b'<roblox version="4"><!--'+b'x'*200000+b'--></roblox>'),'../../map.rbxl')})
            self.assertEqual(upload.status_code,302)
            bad=self.post('/manage',{'title':'Bad','place':(io.BytesIO(b'<!DOCTYPE roblox [<!ENTITY x "bad">]><roblox version="4">&x;</roblox>'),'bad.rbxl')})
            self.assertEqual(bad.status_code,400)
        with grid.app.app_context():draft=grid.db().execute('SELECT MAX(id) FROM places').fetchone()[0]
        self.assertEqual(self.client.get('/places/'+str(draft)).status_code,404)
        self.assertEqual(self.post('/manage/'+str(draft)+'/publish').status_code,409)
        with patch.dict(os.environ,GRID_INSECURE='0'):
            self.assertNotIn(b'Admin sign-in',self.client.get('/').data)
            self.assertEqual(self.client.get('/admin/login').status_code,403)
            self.assertEqual(self.client.post('/admin/login',data={'password':'test-password'}).status_code,403)
            secure=grid.app.test_client()
            response=secure.get('/admin/login',base_url='https://localhost')
            self.assertEqual(response.status_code,200)
            self.assertIn(b'Admin sign-in',response.data)
            self.assertIn('Secure',response.headers['Set-Cookie'])
            public=grid.app.test_client();response=public.get('/')
            self.assertIn('guest_http=',response.headers['Set-Cookie'])
            self.assertNotIn('; Secure',response.headers['Set-Cookie'])

if __name__=='__main__':unittest.main()
