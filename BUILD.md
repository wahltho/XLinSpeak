# Build Instructions

## Requirements
- Linux toolchain: `gcc`, `make`, `nasm`
- Docker Desktop for macOS/Windows hosts (optional)
- Optional fallback backend: `libspeechd-dev`
- Optional PulseAudio backend: `libpulse-dev` (libpulse-simple)

## Artifacts
- Build output: `src/lin.xpl`
- Packaged plugin: `XLinSpeak/lin_x64/XLinSpeak.xpl`

## Linux (native)
```bash
cd src
make
cp -f lin.xpl ../XLinSpeak/lin_x64/XLinSpeak.xpl
```

## Linux (Docker, recommended on macOS)
Use the helper script:
```bash
./build-lin-docker
```
Optional speech-dispatcher fallback:
```bash
USE_SPEECHD=1 ./build-lin-docker
```
Optional PulseAudio backend:
```bash
USE_PULSE=1 ./build-lin-docker
```

Manual one-shot equivalent:
```bash
docker run --rm --platform=linux/amd64 \
  -v "$(pwd)":/workspace -w /workspace ubuntu:22.04 bash -lc "\
  apt-get update && apt-get install -y build-essential nasm && \
  cd src && make test && make && \
  cp -f lin.xpl ../XLinSpeak/lin_x64/XLinSpeak.xpl && \
  make clean"
```

## Optional speech-dispatcher fallback
Build with:
```bash
USE_SPEECHD=1 make
```

On Ubuntu:
```bash
sudo apt-get install -y libspeechd-dev
```

## Optional PulseAudio backend (persistent stream)
Build with:
```bash
USE_PULSE=1 make
```

On Ubuntu:
```bash
sudo apt-get install -y libpulse-dev
```

## Release ZIP
Package the plugin folder structure for distribution:
```bash
./release.sh
```
Outputs: `dist/XLinSpeak-linux.<version>.zip` and `dist/XLinSpeak-<version>-manifest.json`

## Versioning
The plugin version is defined in `src/version.h`. Use a `-dev` suffix for unreleased builds and remove it for release artifacts.

## Clean
```bash
cd src
make clean
```

## Maintenance Toolkit distribution

`release.sh` now produces a versioned `XLinSpeak-linux.<version>.zip` and
`XLinSpeak-<version>-manifest.json`, using the version in `src/version.h`.
Attach both files to the matching `r<version>` GitHub release. The manifest
identifies Linux x64 support and includes SHA-256 and size for the archive and
every payload file. MTK integration requires Toolkit 0.14.0 or newer.

To create a manifest for an already published archive **without repackaging it**:

```bash
python3 tools/create_mtk_manifest.py /path/XLinSpeak-linux.1.3.1.zip --version 1.3.1
```

Use that exact downloaded release archive. Upload only the new manifest asset;
do not replace the existing archive. Piper, its voice model and audio settings
remain external prerequisites; this package does not install or configure them.
