"""Brand the patched upstream web UI for the TC002 port.

The upstream tree (with the generic patch series applied) stays unbranded so its patches remain
candidates for upstream. This script writes build-webui/index.html: the port's title, links to
this repository and to the port maintainer's coffee page after the upstream author's Ko-fi button,
TC002 help texts, and the PolyForm Required Notice. Every anchor must match exactly once, so a moved element fails the build instead
of silently shipping an unbranded or mis-credited page.
"""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

REPO = 'https://github.com/sanderdw/awtrix-ng-tc002'
GITHUB_SYMBOL = ('<symbol id="i-github" viewBox="0 0 24 24"><path d="M9 19c-4.3 1.4-4.3-2.5-6-3m12 6v-3.9a3.4 3.4 0 0 0-.9-2.6c3-.3 6.2-1.5 6.2-7A5.4 5.4 0 0 0 18.8 4 5 5 0 0 0 18.7 0S17.5-.3 14.5 1.6a15 15 0 0 0-7 0C4.5-.3 3.3 0 3.3 0A5 5 0 0 0 3.2 4a5.4 5.4 0 0 0-1.5 3.8c0 5.4 3.2 6.6 6.2 7A3.4 3.4 0 0 0 7 17.4V22" transform="translate(2 2) scale(.85)"/></symbol>\n')
REPO_LINK = (f'  <a class="repo-link" id="github-link" href="{REPO}" target="_blank" rel="noopener noreferrer" '
             'title="GitHub repository" aria-label="GitHub repository"><svg class="ic" aria-hidden="true"><use href="#i-github"/></svg></a>\n')
COFFEE = 'https://bunq.me/sanderdw'
# Material Design "coffee-outline". It is a filled shape, so it opts out of .ic's stroke-only style.
COFFEE_SYMBOL = ('<symbol id="i-coffee" viewBox="0 0 24 24"><path fill="currentColor" stroke="none" d="M2,21V19H20V21H2M20,8V5H18V8H20M20,3A2,2 0 0,1 22,5V8A2,2 0 0,1 20,10H18V13A4,4 0 0,1 14,17H8A4,4 0 0,1 4,13V3H20M16,5H6V13A2,2 0 0,0 8,15H14A2,2 0 0,0 16,13V5Z"/></symbol>\n')
COFFEE_LINK = (f'  <a class="repo-link coffee" id="coffee-link" href="{COFFEE}" target="_blank" rel="noopener noreferrer" '
               'title="Buy the TC002 port maintainer a coffee" aria-label="Buy the TC002 port maintainer a coffee">'
               '<svg class="ic" aria-hidden="true"><use href="#i-coffee"/></svg></a>\n')
REPO_CSS = ('a.repo-link{color:var(--fg);background:var(--card2);border:1px solid var(--border);border-radius:7px;\n'
            '  padding:7px 10px;height:36px;display:inline-flex;align-items:center;justify-content:center}\n'
            'a.repo-link:hover{border-color:var(--acc)}\n'
            'a.repo-link.coffee{color:#fff;background:#ea9e64;border-color:#ea9e64}\n'
            'a.repo-link.coffee:hover{border-color:#ea9e64;filter:brightness(1.08)}\n')
FOOTER_CSS = ('footer.credit{max-width:1060px;margin:0 auto;padding:8px 16px 24px;color:var(--dim);font-size:12px;line-height:1.5}\n'
              'footer.credit a{color:var(--dim);text-decoration:underline}\n')
FOOTER = ('<footer class="credit">AWTRIX NG &copy; <a href="https://github.com/Blueforcer/awtrix-ng" target="_blank" rel="noopener noreferrer">Stephan M&uuml;hl (Blueforcer)</a>, '
          '<a href="https://polyformproject.org/licenses/noncommercial/1.0.0" target="_blank" rel="noopener noreferrer">PolyForm Noncommercial 1.0.0</a>. '
          'Unofficial TC002 port; not a Blueforcer or Ulanzi release. Documentation and the icon editor are hosted by the upstream project: please '
          '<a href="https://ko-fi.com/blueforcer" target="_blank" rel="noopener noreferrer">support it</a>.</footer>\n')

# (anchor, replacement) pairs; each anchor must occur exactly once in the generic UI.
EDITS = [
    ('<title>AWTRIX NG</title>', '<title>AWTRIX NG TC002</title>'),
    ('<h1 id="brand">AWTRIX NG</h1>', '<h1 id="brand">AWTRIX NG TC002</h1>'),
    ('.kofilogo{width:18px;height:18px;flex:none}\n', '.kofilogo{width:18px;height:18px;flex:none}\n' + REPO_CSS),
    ('.topbar{position:sticky;', FOOTER_CSS + '.topbar{position:sticky;'),
    ('</symbol>\n</defs></svg>', '</symbol>\n' + GITHUB_SYMBOL + COFFEE_SYMBOL + '</defs></svg>'),
    ('<button class="icon kofi" id="kofibtn" title="Support me">',
     '<button class="icon kofi" id="kofibtn" title="Support Blueforcer, the AWTRIX NG author, on Ko-fi">'),
    ('<use href="#i-kofi"/></svg></button>\n', '<use href="#i-kofi"/></svg></button>\n' + REPO_LINK + COFFEE_LINK),
    ('<main id="view"></main>\n', '<main id="view"></main>\n' + FOOTER),
    ("'Require a login for the UI and API.|Login für UI und API verlangen.'",
     "'Require a login for the UI and API. While it is off, anyone on your network can control this clock, "
     "upload files and start a firmware update.|Login für UI und API verlangen. Solange dies aus ist, kann jeder "
     "im Netzwerk die Uhr steuern, Dateien hochladen und ein Firmware-Update starten.'"),
]


def brand(html):
    for anchor, replacement in EDITS:
        count = html.count(anchor)
        if count != 1:
            raise SystemExit(f'webui brand: anchor found {count} times, expected once: {anchor[:60]!r}')
        html = html.replace(anchor, replacement)
    for required in ('ko-fi.com/blueforcer', "$('#kofibtn')", 'Stephan M&uuml;hl (Blueforcer)', REPO, COFFEE):
        if required not in html:
            raise SystemExit(f'webui brand: output lacks {required!r}')
    return html


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'build-upstream/webui/index.html')
    parser.add_argument('--output', type=Path, default=ROOT / 'build-webui/index.html')
    args = parser.parse_args()
    html = brand(args.source.read_text(encoding='utf-8'))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding='utf-8') != html:
        args.output.write_text(html, encoding='utf-8')
    print(f'branded web UI: {args.output}')


if __name__ == '__main__':
    main()
