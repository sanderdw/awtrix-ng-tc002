"""Materialise the patched upstream tree: upstream/awtrix-ng (pinned submodule) + patches/ -> build-upstream/.

  apply            clone the pinned upstream commit into build-upstream/ and `git am -3` the series
  export           regenerate patches/ from the commits in build-upstream/ above the pin
  rebase <ref>     move the submodule to <ref>, then apply (stops on the first conflict)
  status           fail unless the submodule is clean and pinned and build-upstream/ is current

The submodule is never edited. Work on upstream code happens as commits inside build-upstream/,
which `export` turns back into patches/. Run through uv: `uv run tools/upstream.py apply`.
"""
import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SUBMODULE = ROOT / 'upstream/awtrix-ng'
PATCHES = ROOT / 'patches'
TREE = ROOT / 'build-upstream'
STAMP = TREE / '.tc002-stamp'
IDENTITY = ['-c', 'user.name=awtrix-ng-tc002', '-c', 'user.email=port@awtrix-ng-tc002.invalid']


def git(*args, cwd=ROOT, check=True, capture=True):
    result = subprocess.run(['git', *args], cwd=cwd, check=check, text=True,
                            capture_output=capture)
    return result.stdout.strip() if capture else result.returncode


def patch_files():
    return sorted(PATCHES.glob('*.patch'))


def pinned_commit():
    """The upstream commit recorded in this repository's index for the submodule."""
    entry = git('ls-files', '--stage', '--', 'upstream/awtrix-ng')
    if not entry.startswith('160000'):
        sys.exit('upstream/awtrix-ng is not a submodule; run `git submodule update --init`')
    return entry.split()[1]


def submodule_head():
    if not (SUBMODULE / 'src').exists():
        sys.exit('upstream/awtrix-ng is empty; run `git submodule update --init`')
    return git('rev-parse', 'HEAD', cwd=SUBMODULE)


def stamp_value(commit):
    digest = hashlib.sha256(commit.encode())
    for path in patch_files():
        digest.update(path.name.encode() + b'\0' + path.read_bytes() + b'\0')
    return digest.hexdigest()


def tree_dirty():
    return TREE.exists() and bool(git('status', '--porcelain', '--untracked-files=no', cwd=TREE))


def apply(force=False):
    commit = submodule_head()
    if commit != pinned_commit():
        sys.exit(f'submodule is at {commit[:12]} but the repository pins {pinned_commit()[:12]}; '
                 'commit the new pin or run `git submodule update`')
    stamp = stamp_value(commit)
    if STAMP.exists() and STAMP.read_text().strip() == stamp and not force:
        print(f'build-upstream is current ({commit[:12]} + {len(patch_files())} patches)')
        return
    if tree_dirty() and not force:
        sys.exit('build-upstream has uncommitted changes; commit them and run `export`, or use --force')
    shutil.rmtree(TREE, ignore_errors=True)
    gitdir = git('rev-parse', '--absolute-git-dir', cwd=SUBMODULE)
    git('clone', '-q', '--shared', '--no-checkout', gitdir, str(TREE))
    git('checkout', '-q', commit, cwd=TREE)
    git('config', 'core.autocrlf', 'false', cwd=TREE)
    patches = [str(p) for p in patch_files()]
    if patches:
        result = subprocess.run(['git', *IDENTITY, 'am', '-3', '--whitespace=nowarn', *patches],
                                cwd=TREE, text=True)
        if result.returncode:
            sys.exit('\nA patch did not apply. Resolve it inside build-upstream/ '
                     '(`git am --continue`), then run `tools/upstream.py export`.')
    STAMP.write_text(stamp + '\n')
    print(f'build-upstream: upstream {commit[:12]} + {len(patches)} patches')


def export():
    commit = pinned_commit()
    if not TREE.exists():
        sys.exit('build-upstream does not exist; run apply first')
    if tree_dirty():
        sys.exit('build-upstream has uncommitted changes; commit them first')
    if git('rev-parse', 'HEAD', cwd=TREE) != commit and not git('merge-base', '--is-ancestor',
                                                              commit, 'HEAD', cwd=TREE, check=False,
                                                              capture=False) == 0:
        sys.exit('build-upstream HEAD does not descend from the pinned upstream commit')
    import tempfile
    with tempfile.TemporaryDirectory(prefix='tc002-patches-') as fresh:
        # Generate first, replace second: a failing format-patch must not empty patches/.
        git('format-patch', '-o', fresh, '--zero-commit', '--no-signature', '--no-stat',
            '--numbered', f'{commit}..HEAD', cwd=TREE)
        PATCHES.mkdir(exist_ok=True)
        for old in patch_files():
            old.unlink()
        for made in sorted(Path(fresh).glob('*.patch')):
            shutil.copyfile(made, PATCHES / made.name)
    STAMP.write_text(stamp_value(commit) + '\n')
    print(f'exported {len(patch_files())} patches')


def rebase(ref):
    git('fetch', '-q', '--tags', 'origin', cwd=SUBMODULE)
    git('checkout', '-q', ref, cwd=SUBMODULE)
    new = submodule_head()
    git('add', '--', 'upstream/awtrix-ng')
    print(f'submodule now at {new[:12]} ({ref}); staged the new pin')
    apply(force=True)


def status():
    commit = pinned_commit()
    problems = []
    if submodule_head() != commit:
        problems.append('submodule checkout differs from the pinned commit')
    if git('status', '--porcelain', cwd=SUBMODULE):
        problems.append('submodule working tree is not clean (upstream must never be edited in place)')
    if not STAMP.exists() or STAMP.read_text().strip() != stamp_value(commit):
        problems.append('build-upstream is missing or stale; run apply')
    elif tree_dirty():
        problems.append('build-upstream has uncommitted changes; commit and export them')
    for problem in problems:
        print('error:', problem)
    print(f'upstream {commit[:12]} + {len(patch_files())} patches' if not problems else 'upstream status: FAILED')
    sys.exit(1 if problems else 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('apply').add_argument('--force', action='store_true')
    sub.add_parser('export')
    sub.add_parser('rebase').add_argument('ref')
    sub.add_parser('status')
    args = parser.parse_args()
    if args.command == 'apply':
        apply(args.force)
    elif args.command == 'export':
        export()
    elif args.command == 'rebase':
        rebase(args.ref)
    else:
        status()


if __name__ == '__main__':
    main()
