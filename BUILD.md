# Build Instructions

## Requirements
- Linux toolchain: `gcc`, `make`, `nasm`
- Docker Desktop for macOS/Windows hosts (optional)
- Optional fallback backend: `libspeechd-dev`

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

## Release ZIP
Package the plugin folder structure for distribution:
```bash
./release.sh
```
Output: `dist/XLinSpeak-linux.zip`

## Clean
```bash
cd src
make clean
```
