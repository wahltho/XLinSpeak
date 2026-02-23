# X-Plane 12 XLinSpeak

Plugin to provide text to speach on Linux

Key features:
* **X-Plane 12.00** _minimum_: There is no attempt to make this work on older versions of X-Plane.
* **64-bit only**: X-Plane is now 64-bit only.
* **X-Plane SDK 400** _minimum_: This is base SDK version for 12.00. Vulkan/Metal and OpenGL are supported.

## Building
cd src

make

## Linux build via Docker (recommended on macOS)
Requires Docker Desktop and builds an x86_64 Linux plugin:
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

## Piper backend (Linux)
The plugin uses Piper CLI with a background queue. Configure via environment:
* `PIPER_BIN` (default: `piper`)
* `PIPER_MODEL` (path to Piper voice model)
* `PIPER_ARGS` (default: `--output_file -`)
* `PIPER_SINK` (default: `aplay -q`)

Notes:
* `PIPER_ARGS` and `PIPER_SINK` are split on spaces (no shell quoting).
* Output is piped from Piper to the sink, so `PIPER_ARGS` must emit audio to stdout.

## Optional speech-dispatcher fallback
If you want the old speech-dispatcher backend as a fallback, build with:
`USE_SPEECHD=1 make`
