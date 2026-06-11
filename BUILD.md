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
  cd src && make && \
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
Output: `dist/XLinSpeak-linux.zip`

## Versioning
The plugin version is defined in `src/version.h`. Use a `-dev` suffix for unreleased builds and remove it for release artifacts.

## Clean
```bash
cd src
make clean
```
