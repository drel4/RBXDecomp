"""Keep Windows include spelling and unavailable audio out of the native ABI."""
import pathlib
import re
import sys

output = pathlib.Path(sys.argv[1])
roots = [pathlib.Path(p) for p in sys.argv[2:] if pathlib.Path(p).is_dir()]
headers = {}
for root in roots:
    for path in root.rglob('*'):
        if path.is_file() and path.suffix.lower() in ('.h', '.hpp', '.inl'):
            key = path.relative_to(root).as_posix()
            headers.setdefault(key.lower(), (key, path))
aliases = output / 'case-includes'
aliases.mkdir(exist_ok=True)
# Alias the canonical names too; a case-insensitive lookup below supplies actual
# misspellings used by the original MSVC sources without rewriting vendored code.
source_root = pathlib.Path(__file__).resolve().parents[3]
for base in (source_root / 'Client/App', source_root / 'Client/Network',
             source_root / 'Client/Rendering/g3d', source_root / 'Client/Rendering/AppDraw',
             source_root / 'services/grid', source_root / 'util'):
    for path in base.rglob('*'):
        if not path.is_file() or path.suffix.lower() not in ('.h','.hpp','.cpp','.c','.inl'):
            continue
        for name in re.findall(r'#\s*include\s*[<"]([^>"\n]+)', path.read_text(errors='replace')):
            found = headers.get(name.lower())
            if found and found[0] != name:
                target = aliases / name
                target.parent.mkdir(parents=True, exist_ok=True)
                if not target.exists():
                    target.symlink_to(found[1])

# A dedicated server has no audio device. Preserve the original Sound instances,
# reflected properties and events; unavailable FMOD output fails explicitly.
fmod = source_root / 'Client/fmod/api/inc'
cpp = ['#include <fmod.hpp>', 'namespace FMOD {']
text = (fmod / 'fmod.hpp').read_text()
for match in re.finditer(r'class\s+(\w+)\s*\{(.*?)\n\s*\};', text, re.S):
    cls, block = match.groups()
    for method, args in re.findall(r'FMOD_RESULT\s+F_API\s+(\w+)\s*\((.*?)\)\s*;', block, re.S):
        cpp.append(f'FMOD_RESULT {cls}::{method}({args}) {{ return FMOD_ERR_UNSUPPORTED; }}')
cpp.append('}')
for name, args in re.findall(r'FMOD_RESULT\s+F_API\s+(FMOD_\w+)\s*\((.*?)\)\s*;', (fmod/'fmod.h').read_text(), re.S):
    cpp.append(f'extern "C" FMOD_RESULT {name}({args}) {{ return FMOD_ERR_UNSUPPORTED; }}')
(output / 'native-fmod.cpp').write_text('\n'.join(cpp)+'\n')
