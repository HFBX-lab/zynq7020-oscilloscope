"""Fetch the pinned official LVGL source into this D-drive project only."""
from pathlib import Path
import hashlib
import json
import tarfile
import urllib.request

root = Path(__file__).resolve().parents[1]
version = '9.2.2'
url = f'https://codeload.github.com/lvgl/lvgl/tar.gz/refs/tags/v{version}'
cache = root / 'cache'
cache.mkdir(exist_ok=True)
archive = cache / f'lvgl-{version}.tar.gz'
if not archive.exists():
    partial = archive.with_suffix('.partial')
    print(f'Downloading {url}', flush=True)
    with urllib.request.urlopen(url, timeout=120) as response, partial.open('wb') as out:
        while chunk := response.read(1024 * 1024):
            out.write(chunk)
    partial.replace(archive)
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
manifest = root / 'dependencies.lock.json'
if manifest.exists():
    previous = json.loads(manifest.read_text(encoding='utf-8'))
    if previous['sha256'] != digest:
        raise SystemExit('Archive hash differs from recorded lock. Refusing extraction.')
dest = root / 'third_party'
dest.mkdir(exist_ok=True)
if not (dest / f'lvgl-{version}' / 'lvgl.h').exists():
    # Keep the library, configuration template and license, not examples/media/docs.
    with tarfile.open(archive) as tar:
        selected = [m for m in tar.getmembers()
                    if len(Path(m.name).parts) > 1
                    and (Path(m.name).parts[1] == 'src'
                         or Path(m.name).parts[1] in ('lvgl.h', 'lv_version.h', 'lv_conf_template.h', 'LICENCE.txt', 'LICENSE.txt'))]
        tar.extractall(dest, members=selected, filter='data')
manifest.write_text(json.dumps({'library': 'LVGL', 'version': version, 'url': url,
                                'sha256': digest, 'license': 'MIT',
                                'hash_note': 'Recorded at first official-source download; not an independent upstream signature.'},
                               indent=2) + '\n', encoding='utf-8')
print(f'LVGL v{version} ready; SHA256={digest}', flush=True)
