"""Small explicit English/Russian UI catalogue; never translate user content."""
import json
from pathlib import Path
from flask import abort, has_request_context, make_response, redirect, request, url_for

RUSSIAN = json.loads(Path(__file__).with_name('i18n_ru.json').read_text(encoding='utf-8'))
LANGUAGES = ('en', 'ru')


def language():
    if not has_request_context():
        return 'en'
    saved = request.cookies.get('language')
    return saved if saved in LANGUAGES else request.accept_languages.best_match(LANGUAGES, default='en')


def translate(message):
    return RUSSIAN.get(message, message) if language() == 'ru' else message


def install(app):
    @app.context_processor
    def language_context():
        return dict(_=translate, language=language(), language_url=lambda code:
                    url_for('set_language', code=code, next=request.full_path.rstrip('?')))

    @app.get('/language/<code>')
    def set_language(code):
        if code not in LANGUAGES:
            abort(404)
        target = request.args.get('next', '/')
        if not target.startswith('/') or target.startswith('//') or '\\' in target or any(ord(c)<32 for c in target):
            target = '/'
        response = make_response(redirect(target))
        # This non-sensitive preference is shared between HTTP and HTTPS and is
        # independent of account sessions, so login/logout never resets it.
        response.set_cookie('language', code, max_age=365*86400, httponly=True, samesite='Lax', path='/')
        response.headers['Cache-Control'] = 'no-store'
        return response
