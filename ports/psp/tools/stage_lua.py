"""Stage engine Lua 5.1.1 with explicit, checked PSP platform adaptations."""
import pathlib
import shutil
import re

target = pathlib.Path(__file__).resolve().parents[1]
source = target.parents[1] / 'Client/App/lua-5.1.1'
destination = target / 'vendor/lua-5.1.1'
if source.is_dir():
    (destination / 'src').mkdir(parents=True, exist_ok=True)
    for pattern in ('*.h', '*.c'):
        for path in (source / 'src').glob(pattern):
            shutil.copyfile(path, destination / 'src' / path.name)
    shutil.copyfile(source / 'COPYRIGHT', destination / 'COPYRIGHT')
    # The desktop's only luaconf customization embeds a Boost shared_ptr for
    # thread counting. PSP ScriptRuntime owns/counts coroutine registry refs;
    # remove that platform bookkeeping. The two other adaptations below preserve
    # exception contracts and null semantics; the parser/libraries are unchanged.
    config = (destination / 'src/luaconf.h').read_text()
    config, count = re.subn(
        r'#include <boost/shared_ptr.hpp>.*?#define luai_userstateyield\(L,n\)[^\n]*',
        '''/* PSP: ScriptRuntime owns thread lifetime/counting, no Boost state. */
#define LUAI_EXTRASPACE 0
#define luai_userstateopen(L) ((void)L)
#define luai_userstateclose(L) ((void)L)
#define luai_userstatethread(L,L1) ((void)L, (void)L1)
#define luai_userstatefree(L) ((void)L)
#define luai_userstateresume(L,n) ((void)L, (void)n)
#define luai_userstateyield(L,n) ((void)L, (void)n)''', config, flags=re.S)
    if count != 1:
        raise SystemExit('Engine luaconf.h changed: re-audit the PSP extra-space adaptation')
    (destination / 'src/luaconf.h').write_text(config)
    # The engine's VM uses C++ exceptions (not upstream longjmp). GCC 15 needs
    # the inherited std::exception noexcept contract stated explicitly.
    ldo = (destination / 'src/ldo.c').read_text()
    for old, new in [('virtual const char* what() const', 'virtual const char* what() const noexcept'),
                     ('virtual ~lua_exception()', 'virtual ~lua_exception() noexcept')]:
        if ldo.count(old) != 1:
            raise SystemExit('Engine ldo.c changed: re-audit exception compatibility')
        ldo = ldo.replace(old, new)
    (destination / 'src/ldo.c').write_text(ldo)
    # Legacy ngcotouv promises to accept nil but forms &null->uv before the caller
    # tests it. Make the documented nil case defined on optimizing MIPS/host GCC.
    header = (destination / 'src/lstate.h').read_text()
    old = 'check_exp((o) == NULL || (o)->gch.tt == LUA_TUPVAL, &((o)->uv))'
    if header.count(old) != 1:
        raise SystemExit('Engine upvalue conversion changed: re-audit nil handling')
    header = header.replace(old, '((o) == NULL ? NULL : gco2uv(o))')
    (destination / 'src/lstate.h').write_text(header)
elif not (destination / 'src/lua.h').is_file():
    raise SystemExit('Lua source missing: run staging from the OpenRBX repository first')
(target / 'vendor/.lua-staged').write_text('OpenRBX vendored Lua 5.1.1\n')
