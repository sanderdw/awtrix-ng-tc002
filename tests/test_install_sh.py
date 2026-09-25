"""install.sh picks the stable release by default and honours --version, without a network."""
import json
import os
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
BUNDLE = 'awtrix-ng-tc002-installer.zip'
RELEASES = [
    {'tag_name': 'v1.1.2-tc002.2', 'prerelease': True, 'draft': False, 'assets': [{'name': BUNDLE}]},
    {'tag_name': 'v1.1.2-tc002.9', 'prerelease': False, 'draft': True, 'assets': [{'name': BUNDLE}]},
    {'tag_name': 'v1.1.2-tc002.1', 'prerelease': False, 'draft': False, 'assets': [{'name': BUNDLE}]},
    {'tag_name': 'v1.1.1-tc002.5', 'prerelease': False, 'draft': False, 'assets': [{'name': BUNDLE}]},
]

# A fake curl: answers the releases API from a file, "downloads" by writing the -o target,
# and logs every URL. The python3 stand-in only records how install.py would be started.
CURL = r'''#!/bin/sh
out=""; url=""
while [ $# -gt 0 ]; do
  case "$1" in -o) out=$2; shift ;; -*) ;; *) url=$1 ;; esac; shift
done
echo "$url" >> "$STUB_LOG"
case "$url" in
  *api.github.com*) cat "$STUB_RELEASES" ;;
  *.sha256) : > "$out" ;;
  *) [ -n "$STUB_MISSING" ] && exit 22; : > "$out" ;;
esac
'''
PYTHON = r'''#!/bin/sh
case "$1" in -c) exec "$REAL_PYTHON" "$@" ;; esac
echo "install.py $*" >> "$STUB_LOG"
'''


@pytest.fixture
def run(tmp_path):
    bin_dir = tmp_path / 'bin'
    bin_dir.mkdir()
    for name, body in (('curl', CURL), ('python3', PYTHON), ('unzip', '#!/bin/sh\n'),
                       ('sha256sum', '#!/bin/sh\n'), ('shasum', '#!/bin/sh\n')):
        (bin_dir / name).write_text(body)
        (bin_dir / name).chmod(0o755)
    releases = tmp_path / 'releases.json'
    releases.write_text(json.dumps(RELEASES))
    log = tmp_path / 'log'

    def run_install(*args, releases_data=None, missing=False, env_version=None):
        if releases_data is not None:
            releases.write_text(json.dumps(releases_data))
        log.write_text('')
        env = {'PATH': f'{bin_dir}:/usr/bin:/bin', 'HOME': str(tmp_path), 'STUB_LOG': str(log),
               'STUB_RELEASES': str(releases), 'STUB_MISSING': '1' if missing else '',
               'REAL_PYTHON': subprocess.check_output(['sh', '-c', 'command -v python3'], text=True).strip()}
        if env_version:
            env['AWTRIX_TC002_VERSION'] = env_version
        result = subprocess.run(['sh', str(ROOT / 'install.sh'), *args], env=env, stdin=subprocess.DEVNULL,
                                capture_output=True, text=True)
        return result, log.read_text().splitlines()
    return run_install


def downloaded(lines):
    return [line.split('/releases/download/')[1] for line in lines if '/releases/download/' in line]


def test_default_is_newest_stable_release(run):
    result, lines = run('192.168.1.5', '--yes')
    assert result.returncode == 0, result.stderr
    assert downloaded(lines)[0] == f'v1.1.2-tc002.1/{BUNDLE}'
    assert '(stable)' in result.stdout
    assert lines[-1].endswith('/install.py 192.168.1.5 --yes')


@pytest.mark.parametrize('args', [
    ('--version', 'v1.1.2-tc002.2', '192.168.1.5', '--yes'),
    ('192.168.1.5', '--version', 'v1.1.2-tc002.2', '--yes'),
    ('--version=1.1.2-tc002.2', '192.168.1.5', '--yes'),
])
def test_version_option_pins_any_release_and_is_not_passed_on(run, args):
    result, lines = run(*args)
    assert result.returncode == 0, result.stderr
    assert not any('api.github.com' in line for line in lines)
    assert downloaded(lines)[0] == f'v1.1.2-tc002.2/{BUNDLE}'
    assert '(requested)' in result.stdout
    assert lines[-1].endswith('/install.py 192.168.1.5 --yes')


def test_environment_variable_still_pins_a_release(run):
    result, lines = run('192.168.1.5', env_version='v1.1.1-tc002.5')
    assert result.returncode == 0, result.stderr
    assert downloaded(lines)[0] == f'v1.1.1-tc002.5/{BUNDLE}'


def test_only_prereleases_means_no_default(run):
    result, _ = run('192.168.1.5', releases_data=[RELEASES[0]])
    assert result.returncode == 1
    assert 'no stable release' in result.stderr and '--version' in result.stderr


def test_unknown_version_fails_with_a_hint(run):
    result, _ = run('--version', 'v9.9.9-tc002.1', '192.168.1.5', missing=True)
    assert result.returncode == 1
    assert 'has no installer bundle' in result.stderr


def test_version_without_tag_and_missing_clock_ip_are_usage_errors(run):
    assert run('192.168.1.5', '--version')[0].returncode == 2
    assert run('--version', 'v1.1.2-tc002.2')[0].returncode == 2
