# tobacco

block-based video codec with a custom `.blunt` file format. walsh-hadamard transform, run-level entropy coding, motion estimation, and sse2 color conversion.

## features

- walsh-hadamard transform (wht) 4x4 block codec
- custom `.blunt` binary format with i-frame and p-frame support
- ycbcr 4:2:0 color space with bt.601 conversion
- full-search motion estimation with half-pel refinement
- run-level entropy coding with escape codes for large coefficients
- sse2-accelerated ycbcr/rgb color conversion and motion compensation
- configurable quality (1-100) via quantizer scaling
- multi-macroblock support for arbitrary resolutions (padded to 16x16)
- **wht-transform-coded stereo audio** (48kHz, 16/32-bit, 4x4 block codec with adpcm prediction)
- **cross-platform desktop app** (c# avalonia 12.1, .net 10) — player, converter, synthesizer

## usage

```
blunt_encode -i frames/ -o output.blunt -q 80 -w 320 -h 240 -f 30
blunt_decode -i output.blunt -o frames/
blunt_info output.blunt
```

### desktop app

```bash
cd desktop/Tobacco
dotnet run
```

three tabs: **Player** (open .blunt files, frame-by-frame playback), **Converter** (wav <-> .blunt with quality control), **Synth** (full synthesizer with oscillators, ADSR, filters, LFO, effects).

## build

```bash
git clone https://github.com/kroown/tobacco.git
cd tobacco
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

dependencies: cmake, a c11 compiler with sse2 support (gcc, msvc, clang). for the desktop app: .net 10+ sdk.

## options

### blunt_encode

| flag | description |
|------|-------------|
| `-i` | input directory or frame pattern (e.g. `frame_%04d.ppm`) |
| `-o` | output `.blunt` file |
| `-q` | quality (1-100, default 75) |
| `-w`, `-h` | frame dimensions |
| `-f` | frame rate (fps) |

### blunt_decode

| flag | description |
|------|-------------|
| `-i` | input `.blunt` file |
| `-o` | output directory for decoded frames (ppm) |

### blunt_info

prints header info, frame count, and file size for a `.blunt` file.

## format

```
+----------------------+
| file header          |  64 bytes (magic, version, dimensions, quality, mb grid)
+----------------------+
| frame 0 header       |  17 bytes (frame_num, type, data_size, timestamp)
| frame 0 bitstream    |  run-level coded wht coefficients
+----------------------+
| frame 1 header       |
| frame 1 bitstream    |
+----------------------+
| ...                  |
+----------------------+
```

each frame is macroblock-aligned (16x16). luma has 16 sub-blocks per macroblock, chroma has 4 sub-blocks per plane (4:2:0 subsampling).

## architecture

```
include/
  blunt.h             public api, format structs, constants
  blunt_simd.h        simd dispatch declarations
  blunt_tables.h      zigzag, quantizer, huffman declarations
src/
  blunt_encoder.c     bitstream writer, run-level encoding, motion estimation
  blunt_decoder.c     bitstream reader, run-level decoding, frame reconstruction
  blunt_audio.c       audio frame codec (encode/decode blocks, adpcm prediction)
  blunt_tables.c      quantizer table initialization, canonical huffman builder
  blunt_simd.c        wht forward/inverse, sse2 color conversion, motion compensation
tools/
  blunt_encode.c      cli encoder (ppm -> blunt)
  blunt_decode.c      cli decoder (blunt -> ppm/pgm)
  blunt_info.c        file inspector
tests/
  test_roundtrip.c    encode/decode roundtrip validation (all frames > 20db)
desktop/
  Tobacco/            c# avalonia cross-platform desktop app
    Codec/            blunt codec port (bitstream, wht, audio, video, wav)
    Audio/            synthesizer engine (oscillators, adsr, filters, effects)
    Views/            player, converter, synth ui
```

## test results

at quality=80, the codec achieves the following ycbcr psnr on standard test patterns:

| pattern | psnr |
|---------|------|
| gradient | 51.37 db |
| moving bar | 61.57 db |
| checkerboard | 99.99 db |
| color bars | 99.99 db |

audio codec (48khz stereo, quality=75):

| channel | psnr |
|---------|------|
| ch0 | 84.8 db |
| ch1 | 84.9 db |
