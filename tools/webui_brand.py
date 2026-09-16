"""Brand the patched upstream web UI for the TC002 port.

The upstream tree (with the generic patch series applied) stays unbranded so its patches remain
candidates for upstream. This script writes build-webui/index.html: the port's title, a link to
this repository next to the upstream author's Ko-fi button, TC002 help texts, and the PolyForm
Required Notice. Every anchor must match exactly once, so a moved element fails the build instead
of silently shipping an unbranded or mis-credited page.
"""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

REPO = 'https://github.com/sanderdw/awtrix-ng-tc002'
GITHUB_SYMBOL = ('<symbol id="i-github" viewBox="0 0 24 24"><path d="M9 19c-4.3 1.4-4.3-2.5-6-3m12 6v-3.9a3.4 3.4 0 0 0-.9-2.6c3-.3 6.2-1.5 6.2-7A5.4 5.4 0 0 0 18.8 4 5 5 0 0 0 18.7 0S17.5-.3 14.5 1.6a15 15 0 0 0-7 0C4.5-.3 3.3 0 3.3 0A5 5 0 0 0 3.2 4a5.4 5.4 0 0 0-1.5 3.8c0 5.4 3.2 6.6 6.2 7A3.4 3.4 0 0 0 7 17.4V22" transform="translate(2 2) scale(.85)"/></symbol>\n')
REPO_LINK = (f'  <a class="repo-link" id="github-link" href="{REPO}" target="_blank" rel="noopener noreferrer" '
             'title="GitHub repository" aria-label="GitHub repository"><svg class="ic" aria-hidden="true"><use href="#i-github"/></svg></a>\n')
REPO_CSS = ('a.repo-link{color:var(--fg);background:var(--card2);border:1px solid var(--border);border-radius:7px;\n'
            '  padding:7px 10px;height:36px;display:inline-flex;align-items:center;justify-content:center}\n'
            'a.repo-link:hover{border-color:var(--acc)}\n')
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
    ('</symbol>\n</defs></svg>', '</symbol>\n' + GITHUB_SYMBOL + '</defs></svg>'),
    ('<button class="icon kofi" id="kofibtn" title="Support me">',
     '<button class="icon kofi" id="kofibtn" title="Support Blueforcer, the AWTRIX NG author, on Ko-fi">'),
    ('<use href="#i-kofi"/></svg></button>\n', '<use href="#i-kofi"/></svg></button>\n' + REPO_LINK),
    ('<main id="view"></main>\n', '<main id="view"></main>\n' + FOOTER),
]


def brand(html):
    for anchor, replacement in EDITS:
        count = html.count(anchor)
        if count != 1:
            raise SystemExit(f'webui brand: anchor found {count} times, expected once: {anchor[:60]!r}')
        html = html.replace(anchor, replacement)
    for required in ('ko-fi.com/blueforcer', "$('#kofibtn')", 'Stephan M&uuml;hl (Blueforcer)', REPO):
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
