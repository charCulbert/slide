#!/usr/bin/env python3
"""Builds slide-standalone.html: slide.html with the compost window and the
engine inlined, so it runs from a single file (or as a claude.ai artifact)
with no module imports. Run from this directory:  python3 build.py
"""
import re, pathlib, subprocess, sys
here = pathlib.Path(__file__).parent
compost = here / '../external/compost/src/components/compost-window.js'

def bundle(entry):
    """Tiny ES-module inliner for compost's plain modules: each module becomes an
    IIFE whose exports land in a registry keyed by absolute path."""
    seen, order = {}, []
    def load(file):
        file = file.resolve()
        if file in seen: return
        seen[file] = True
        src = file.read_text()
        deps = []
        def imp(m):
            spec = m.group(5)
            if not spec.startswith('.'): return m.group(0)
            dep = (file.parent / spec).resolve(); deps.append(dep)
            names = ','.join(x for x in (m.group(2), m.group(4)) if x)
            return f'const {{{names.replace(" as ", ":")}}} = __m[{str(dep)!r}];' if names else ''
        src = re.sub(r'^\s*import\s+(?:([\w$]+)|\{([^}]*)\}|\*\s+as\s+([\w$]+))?\s*(?:,\s*\{([^}]*)\})?\s*(?:from\s*)?["\']([^"\']+)["\'];?\s*$', imp, src, flags=re.M)
        for d in deps: load(d)
        exports = []
        def exp(m): exports.append(m.group(2)); return f'{m.group(1)} {m.group(2)}'
        src = re.sub(r'^export\s+(const|let|var|function|class)\s+([\w$]+)', exp, src, flags=re.M)
        def expl(m):
            for x in m.group(1).split(','):
                x = x.strip()
                if x:
                    n, *a = re.split(r'\s+as\s+', x); exports.append(f'{a[0]}:{n}' if a else n)
            return ''
        src = re.sub(r'^export\s*\{([^}]*)\};?', expl, src, flags=re.M)
        src = re.sub(r'^export\s+default\s+', 'const __default = ', src, flags=re.M)
        if '__default' in src: exports.append('default:__default')
        order.append(f'__m[{str(file)!r}]=(()=>{{ {src}\n return {{{",".join(exports)}}}; }})();')
    load(entry)
    return 'const __m={};\n' + '\n'.join(order)

src = (here / 'slide.html').read_text()
engine = (here / 'slide-engine.js').read_text().replace('export const', 'const')
style = re.search(r'<style>[\s\S]*?</style>', src).group(0)
body = re.search(r'<body>([\s\S]*?)<script type="module">', src).group(1)
js = re.search(r'<script type="module">([\s\S]*?)</script>', src).group(1)
js = re.sub(r'^import .*$', '', js, flags=re.M)
page = '<title>Slide</title>\n' + style + '\n' + body + '\n<script>\n' + bundle(compost) + '\n' + engine + '\n' + js + '\n</script>\n'
(here / 'slide-standalone.html').write_text('<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">' + page.replace('<script>', '</head><body><script>', 1) + '</body></html>')
print('wrote slide-standalone.html', len(page), 'chars')
