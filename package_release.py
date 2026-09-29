#!/usr/bin/env python3
"""Package the current CMake version after b.sh and bw.sh have completed."""
from pathlib import Path
import hashlib
import re
import zipfile

ROOT = Path(__file__).resolve().parent
version = re.search(r'project\(searchwx VERSION ([\d.]+)', (ROOT / 'CMakeLists.txt').read_text())[1]
release = ROOT / 'release'
release.mkdir(exist_ok=True)
artifacts = [('linux-x64', ROOT / 'build-host/searchwx'),
             ('windows-x64', ROOT / 'build_windows/search.exe')]
# Fail before producing any archives if a build is missing or out of version.
for platform, binary in artifacts:
    if not binary.is_file():
        raise SystemExit(f'Missing {binary}; run b.sh and bw.sh first.')
    cache_dir = binary.parent if platform == 'linux-x64' else binary.parent / 'cmake'
    cache = (cache_dir / 'CMakeCache.txt').read_text()
    if f'CMAKE_PROJECT_VERSION:STATIC={version}\n' not in cache:
        raise SystemExit(f'{platform} needs rebuilding for {version}.')
    if platform == 'windows-x64' and 'EXPLORER_WINDOWS_SHELL:BOOL=ON\n' not in cache:
        raise SystemExit('The Windows release must include the integrated shell.')
for platform, binary in artifacts:
    target = release / f'Explorer-{version}-{platform}.zip'
    temporary = target.with_suffix('.zip.tmp')
    with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        archive.write(binary, binary.name)
        archive.write(ROOT / 'README.md', 'README.md')
        for doc in sorted((ROOT / 'docs').iterdir()):
            if doc.is_file():
                archive.write(doc, f'docs/{doc.name}')
    with zipfile.ZipFile(temporary) as archive:
        if archive.testzip() or archive.read(binary.name) != binary.read_bytes():
            raise SystemExit(f'Archive verification failed: {temporary}')
        if platform == 'linux-x64' and not (archive.getinfo(binary.name).external_attr >> 16) & 0o111:
            raise SystemExit('Linux executable permissions were lost.')
    temporary.replace(target)
    print(f'Verified {target.name}')
checksums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n'
                    for p in sorted(release.glob('*.zip')))
(release / 'SHA256SUMS').write_text(checksums)
