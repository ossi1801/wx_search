#!/usr/bin/env python3
"""Package binaries built by b.sh and bw.sh into verified release ZIPs."""
from pathlib import Path
import argparse
import hashlib
import os
import re
import zipfile

ROOT = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--linux-build-dir', type=Path,
                        default=Path(os.environ.get('BUILD_DIR', ROOT / 'build-host')))
    parser.add_argument('--windows-build-dir', type=Path, default=ROOT / 'build_windows')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'release')
    args = parser.parse_args()
    match = re.search(r'project\(searchwx\s+VERSION\s+([\d.]+)',
                      (ROOT / 'CMakeLists.txt').read_text())
    if not match:
        parser.error('Cannot find the searchwx version in CMakeLists.txt.')
    version = match[1]
    artifacts = [
        ('linux-x64', args.linux_build_dir / 'searchwx', args.linux_build_dir),
        ('windows-x64', args.windows_build_dir / 'search.exe',
         args.windows_build_dir / 'cmake'),
    ]
    # Validate both builds before creating or replacing any release archives.
    for platform, binary, cache_dir in artifacts:
        if not binary.is_file():
            parser.error(f'Missing {binary}; run b.sh and bw.sh first.')
        cache_path = cache_dir / 'CMakeCache.txt'
        if not cache_path.is_file():
            parser.error(f'Missing {cache_path}; rebuild {platform}.')
        cache = cache_path.read_text().splitlines()
        if f'CMAKE_PROJECT_VERSION:STATIC={version}' not in cache:
            parser.error(f'{platform} needs rebuilding for {version}.')
        if platform == 'windows-x64' and 'EXPLORER_WINDOWS_SHELL:BOOL=ON' not in cache:
            parser.error('The Windows release must include the integrated shell.')

    release = args.output_dir
    release.mkdir(parents=True, exist_ok=True)
    for platform, binary, _ in artifacts:
        target = release / f'Explorer-{version}-{platform}.zip'
        temporary = target.with_suffix('.zip.tmp')
        try:
            with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED,
                                 compresslevel=9) as archive:
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
        finally:
            temporary.unlink(missing_ok=True)
        print(f'Verified {target}')
    checksums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n'
                        for p in sorted(release.glob('*.zip')))
    temporary = release / 'SHA256SUMS.tmp'
    temporary.write_text(checksums)
    temporary.replace(release / 'SHA256SUMS')


if __name__ == '__main__':
    main()
