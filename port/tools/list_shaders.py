#!/usr/bin/env python3
"""List the files of libretro's glsl-shaders that the launcher's Shader list needs.

    python3 tools/list_shaders.py GLSL_SHADERS_DIR [--write dist/shaders.txt | --check dist/shaders.txt]

The presets are read from kShaders in src/launcher.cpp. For each one this follows the preset
(#reference lines, shaderN files, the lookup textures) and adds the licence files that sit next to
those files. The portable build copies exactly the files in dist/shaders.txt (one path per line,
relative to the glsl-shaders folder) instead of the whole collection. Run it again after adding a
preset to kShaders or moving to another glsl-shaders commit.
"""
import os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
LAUNCHER = os.path.join(here, '..', 'src', 'launcher.cpp')
LICENSE_NAMES = ('license', 'licence', 'copying', 'copyright', 'readme')


def unquote(v):
    v = v.strip()
    if v.startswith('"'):
        e = v.find('"', 1)
        return v[1:e if e > 0 else None]
    return v.split('#')[0].strip()


def presets():
    text = open(LAUNCHER, encoding='utf-8').read()
    block = text[text.index('kShaders[]'):]
    block = block[:block.index('};')]
    return re.findall(r'\{"[^"]*",\s*"([^"]+\.glslp?)"', block)


def follow(root, rel, found, depth=0):
    """Add rel and everything it needs to found. Paths are relative to root, with / separators."""
    if depth > 16: sys.exit(f'too many nested #reference lines at {rel}')
    path = os.path.join(root, rel)
    if not os.path.isfile(path): sys.exit(f'missing: {rel}')
    found.add(rel)
    if not rel.endswith('.glslp'): return
    base = os.path.dirname(rel)
    keys = {}
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.strip()
        if line.startswith('#reference'):
            follow(root, os.path.normpath(os.path.join(base, unquote(line[10:]))).replace(os.sep, '/'), found, depth + 1)
            continue
        if not line or line.startswith('#') or '=' not in line: continue
        k, v = line.split('=', 1)
        keys[k.strip()] = (unquote(v), base)
    # the keys of a preset that is referenced come from the referencing file's folder, like the game
    n = int(keys.get('shaders', ('0', ''))[0] or 0)
    for i in range(n):
        v = keys.get(f'shader{i}')
        if v: follow(root, os.path.normpath(os.path.join(v[1], v[0])).replace(os.sep, '/'), found, depth + 1)
    for tid in (keys.get('textures', ('', ''))[0]).split(';'):
        tid = tid.strip()
        if tid and tid in keys:
            f = os.path.normpath(os.path.join(keys[tid][1], keys[tid][0])).replace(os.sep, '/')
            if not os.path.isfile(os.path.join(root, f)): sys.exit(f'missing texture: {f}')
            found.add(f)


def build(root):
    found = set()
    for p in presets():
        follow(root, p, found)
    # licence files in the folders of what was found (and the collection's top level)
    dirs = {''} | {os.path.dirname(f) for f in found}
    for d in sorted(dirs):
        for name in os.listdir(os.path.join(root, d)):
            if os.path.isfile(os.path.join(root, d, name)) and name.lower().split('.')[0] in LICENSE_NAMES:
                found.add((d + '/' + name) if d else name)
    return sorted(found)


if __name__ == '__main__':
    if len(sys.argv) < 2: sys.exit(__doc__)
    root = sys.argv[1]
    files = build(root)
    if '--write' in sys.argv:
        out = sys.argv[sys.argv.index('--write') + 1]
        open(out, 'w', encoding='utf-8', newline='\n').write(''.join(f + '\n' for f in files))
        print(f'{len(files)} files -> {out}')
    elif '--check' in sys.argv:
        want = open(sys.argv[sys.argv.index('--check') + 1], encoding='utf-8').read().split()
        if want != files:
            sys.exit('dist/shaders.txt is out of date; run: python3 tools/list_shaders.py GLSL_DIR --write dist/shaders.txt')
        print(f'ok: {len(files)} files')
    else:
        print('\n'.join(files))
