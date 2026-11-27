# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause
"""Store validated channels on existing releases and reconstruct Pages."""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

PLATFORMS = ('linux-64', 'linux-aarch64')
PACKAGES = ('bpe_blackboard', 'block_plugin_engine', 'bpe_ros_boundary_plugins')
LATEST = re.compile(r'bpe-conda-jazzy-latest-(\d+)-(\d+)\.zip')
OFFICIAL = re.compile(r'v\d+\.\d+\.\d+')


def run(*args):
    subprocess.run(args, check=True)


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def api(endpoint):
    return json.loads(output('gh', 'api', endpoint))


def repo_endpoint():
    return 'repos/' + os.environ['GITHUB_REPOSITORY'] + '/releases'


def releases():
    pages = json.loads(output('gh', 'api', '--paginate', '--slurp', repo_endpoint() + '?per_page=100'))
    return [r for page in pages for r in page if not r['draft'] and not r['prerelease']]


def latest_release():
    return api(repo_endpoint() + '/latest')


def release(tag):
    if not OFFICIAL.fullmatch(tag):
        raise ValueError('Expected an official tag vX.Y.Z')
    r = api(repo_endpoint() + '/tags/' + tag)
    if r['draft'] or r['prerelease']:
        raise ValueError('Release must already be published and official')
    return r


def writable(r):
    if r.get('immutable'):
        raise ValueError('Release is immutable: ' + r['tag_name'])


def official_name(tag):
    return f'bpe-conda-jazzy-{tag}.zip'


def ensure_absent(r, name):
    if any(a['name'] == name for a in r['assets']):
        raise ValueError('Official binary already exists: ' + name)


def git_sha(ref):
    return output('git', 'rev-parse', '--verify', ref + '^{commit}')


def prepare():
    mode = os.environ['MODE']
    if mode == 'release':
        tag = os.environ['TAG']
        r = release(tag)
        writable(r)
        ensure_absent(r, official_name(tag))
        sha = git_sha('refs/tags/' + tag)
    elif mode == 'latest':
        r = latest_release()
        writable(r)
        tag = r['tag_name']
        sha = os.environ['GITHUB_SHA']
    else:
        raise ValueError(mode)
    versions = {ET.fromstring(output('git', 'show', f'{sha}:{p}/package.xml')).findtext('version') for p in PACKAGES}
    if len(versions) != 1:
        raise ValueError('BPE versions must match')
    version = versions.pop()
    if mode == 'release' and tag != 'v' + version:
        raise ValueError('Tag and package versions differ')
    with open(os.environ['GITHUB_OUTPUT'], 'a') as f:
        print(f'sha={sha}\nversion={version}\ntag={tag}', file=f)


def index(channel):
    for platform in (*PLATFORMS, 'noarch'):
        (channel / platform).mkdir(parents=True, exist_ok=True)
    run('pixi', 'exec', '-c', 'conda-forge', '--spec', 'conda-index', 'python', '-m', 'conda_index', str(channel))
    for p in channel.rglob('.cache'):
        if p.is_dir():
            shutil.rmtree(p)


def asset_name():
    if os.environ['MODE'] == 'release':
        return official_name(os.environ['TAG'])
    return f"bpe-conda-jazzy-latest-{os.environ['GITHUB_RUN_ID']}-{os.environ['GITHUB_RUN_ATTEMPT']}.zip"


def assemble():
    channel = Path('channel')
    for platform in PLATFORMS:
        dest = channel / platform
        dest.mkdir(parents=True)
        files = list((Path('packages') / ('conda-' + platform)).glob('*.conda'))
        if len(files) != 3:
            raise ValueError(f'Expected three packages for {platform}')
        for file in files:
            shutil.copy2(file, dest)
    index(channel)
    metadata = {k.lower(): os.environ[k] for k in ('SHA', 'VERSION', 'MODE', 'TAG')}
    metadata['workflow_sha'] = os.environ['GITHUB_SHA']
    metadata['run_id'] = os.environ['GITHUB_RUN_ID']
    (channel / 'build.json').write_text(json.dumps(metadata, indent=2) + '\n')
    Path('output').mkdir()
    with zipfile.ZipFile(Path('output') / asset_name(), 'w', zipfile.ZIP_DEFLATED) as archive:
        for file in sorted(channel.rglob('*')):
            if file.is_file():
                archive.write(file, file.relative_to(channel))


def store():
    name = asset_name()
    if os.environ['MODE'] == 'release':
        tag = os.environ['TAG']
        r = release(tag)
        writable(r)
        ensure_absent(r, name)
        remote = output('git', 'ls-remote', 'origin', 'refs/tags/' + tag, 'refs/tags/' + tag + '^{}')
        if not remote or remote.splitlines()[-1].split()[0] != os.environ['SHA']:
            raise ValueError('Tag moved')
        run('gh', 'release', 'upload', tag, 'output/' + name)
        return
    r = latest_release()
    writable(r)
    tag = r['tag_name']
    # Upload first, then remove old development assets. Never clobber an official asset.
    old = [(rel, a) for rel in releases() for a in rel['assets'] if LATEST.fullmatch(a['name'])]
    if any(rel.get('immutable') for rel, _ in old):
        raise ValueError('Cannot clean up latest on an immutable release')
    # A retry of the same run must not overwrite even its own upload.
    ensure_absent(r, name)
    run('gh', 'release', 'upload', tag, 'output/' + name)
    for rel, asset in old:
        run('gh', 'api', '-X', 'DELETE', repo_endpoint() + '/assets/' + str(asset['id']))


def restore(tag, name, dest):
    with tempfile.TemporaryDirectory() as tmp:
        run('gh', 'release', 'download', tag, '--pattern', name, '--dir', tmp)
        with zipfile.ZipFile(Path(tmp) / name) as archive:
            for platform in PLATFORMS:
                members = [n for n in archive.namelist() if re.fullmatch(re.escape(platform) + r'/[^/]+\.conda', n)]
                if len(members) != 3:
                    raise ValueError(f'Invalid bundle {name}: {platform}')
                for member in members:
                    target = dest / member
                    target.parent.mkdir(parents=True, exist_ok=True)
                    if target.exists():
                        raise ValueError('Duplicate package: ' + str(target))
                    target.write_bytes(archive.read(member))


def pages():
    root = Path('site/conda/jazzy')
    official, development = root / 'releases', root / 'latest'
    candidates = []
    marked = latest_release()['tag_name']
    for r in releases():
        tag = r['tag_name']
        for asset in r['assets']:
            name = asset['name']
            if OFFICIAL.fullmatch(tag) and name == official_name(tag):
                restore(tag, name, official)
            match = LATEST.fullmatch(name)
            if match:
                candidates.append((tag == marked, int(match[1]), int(match[2]), tag, name))
    if candidates:
        _, _, _, tag, name = max(candidates)
        restore(tag, name, development)
    for channel in (official, development):
        index(channel)


if __name__ == '__main__':
    commands = {'prepare': prepare, 'assemble': assemble, 'store': store, 'pages': pages}
    commands[sys.argv[1]]()
