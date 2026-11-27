# SPDX-FileCopyrightText: 2026 Universidad Politécnica de Madrid (UPM) - CVAR-UPM
# SPDX-License-Identifier: BSD-3-Clause
"""Render the README and navigable indexes for the static Conda channels."""
import html
import json
import os
import re
from pathlib import Path
from urllib.parse import quote, urljoin, urlsplit

import markdown

ROOT = Path('site')
CHANNELS = ROOT / 'conda/jazzy'
REPO = 'https://github.com/' + os.environ.get('GITHUB_REPOSITORY', 'cvar-upm/blockpluginengine')
REF = os.environ.get('GITHUB_SHA', 'main')
STYLE = '''
:root { color-scheme: light dark; font-family: system-ui, sans-serif; line-height: 1.65; }
body { max-width: 940px; margin: 0 auto; padding: 2rem 1.25rem; overflow-wrap: anywhere; }
a { color: #1769aa; } @media (prefers-color-scheme: dark) { a { color: #82baff; } }
nav, section.downloads { border: 1px solid #8886; border-radius: 10px; padding: 1rem 1.25rem; margin-bottom: 2rem; }
nav a { margin-right: 1.2rem; } h1, h2 { line-height: 1.25; }
pre { padding: 1rem; overflow-x: auto; background: #8881; border-radius: 6px; overflow-wrap: normal; }
code { font-family: ui-monospace, monospace; } img { max-width: 100%; }
table { border-collapse: collapse; width: 100%; } th, td { border-bottom: 1px solid #8886; text-align: left; padding: .5rem; }
footer { margin-top: 3rem; border-top: 1px solid #8886; padding-top: 1rem; }
'''


def esc(value):
    return html.escape(str(value), quote=True)


def page(path, title, body):
    home = os.path.relpath(ROOT / 'index.html', path.parent)
    path.write_text(f'''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>{esc(title)} | BPE</title><style>{STYLE}</style></head><body>
<nav><a href="{esc(home)}">BPE home</a><a href="{esc(REPO)}">Source repository</a><a href="{esc(REPO)}/releases">GitHub releases</a></nav>
<main>{body}</main><footer>Universidad Politécnica de Madrid (UPM) · CVAR-UPM · BSD-3-Clause</footer></body></html>''', encoding='utf-8')


def readme():
    content = markdown.markdown(Path('README.md').read_text(encoding='utf-8'), extensions=['extra', 'toc', 'sane_lists'])
    # Repository-relative links are not files deployed on Pages.
    def link(match):
        attr, delimiter, value = match.groups()
        value = html.unescape(value)
        if not value.startswith(('#', '//', '/')) and not urlsplit(value).scheme:
            base = f'{REPO}/blob/{REF}/' if attr == 'href' else f'https://raw.githubusercontent.com/{os.environ.get("GITHUB_REPOSITORY", "cvar-upm/blockpluginengine")}/{REF}/'
            value = urljoin(base, value)
        return f'{attr}={delimiter}{esc(value)}{delimiter}'
    return re.sub(r'\b(href|src)=(["\'])(.*?)\2', link, content)


def channel(name, title, description):
    root = CHANNELS / name
    rows = []
    for platform in ('linux-64', 'linux-aarch64', 'noarch'):
        directory = root / platform
        data = json.loads((directory / 'repodata.json').read_text())
        packages = {**data.get('packages', {}), **data.get('packages.conda', {})}
        entries = []
        for filename, info in sorted(packages.items()):
            if not (directory / filename).is_file():
                raise ValueError(f'Missing package: {directory / filename}')
            entries.append(f'<tr><td>{esc(info["name"])}</td><td>{esc(info["version"])}</td><td>{esc(info["build"])}</td><td><a href="{esc(quote(filename))}">Download</a></td></tr>')
        listing = '<table><thead><tr><th>Package</th><th>Version</th><th>Build</th><th>File</th></tr></thead><tbody>' + ''.join(entries) + '</tbody></table>' if entries else '<p>No packages for this platform.</p>'
        page(directory / 'index.html', f'{title} — {platform}', f'<h1>{esc(title)} — {platform}</h1><p><a href="../index.html">Back to channel</a> · <a href="repodata.json">repodata.json</a></p>{listing}')
        rows.append(f'<li><a href="{platform}/">{platform}</a> — {len(entries)} packages · <a href="{platform}/repodata.json">repodata.json</a></li>')
    base = os.environ.get('PAGES_BASE_URL', 'https://cvar-upm.github.io/blockpluginengine').rstrip('/')
    url = f'{base}/conda/jazzy/{name}'
    command = f'pixi add -c {url} ros-jazzy-block-plugin-engine'
    page(root / 'index.html', title, f'<h1>{esc(title)}</h1><p>{esc(description)}</p><h2>Channel URL</h2><pre>{esc(url)}</pre><p>Add to an existing RoboStack Jazzy workspace:</p><pre>{esc(command)}</pre><h2>Platforms</h2><ul>{"".join(rows)}</ul>')


if __name__ == '__main__':
    channel('releases', 'Official releases', 'Published, validated BPE versions for ROS 2 Jazzy.')
    channel('latest', 'Latest development build', 'Most recent successfully validated development build from main; not an official release.')
    downloads = '''<section class="downloads"><h2>Binary packages — ROS 2 Jazzy</h2><p>Native AMD64 and ARM64 builds, installable with Pixi or Conda.</p><p><a href="conda/jazzy/releases/">Official releases</a> · <a href="conda/jazzy/latest/">Latest development build</a></p></section>'''
    content = readme()
    # Keep the README heading first, then show download navigation prominently.
    end = content.find('</h1>')
    if end >= 0:
        end += len('</h1>')
        content = content[:end] + downloads + content[end:]
    else:
        content = downloads + content
    page(ROOT / 'index.html', 'Block Plugin Engine', content)
