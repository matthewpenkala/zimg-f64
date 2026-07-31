# Raw and FFmpeg integration

## Core boundary

Spline36 resampling ends at raw planar output. Containers, timestamps, codec
levels, delivery profiles, dithering, and upload requirements are downstream
concerns and are intentionally excluded from the algorithm definition.

The reference executable accepts:

```text
3840x2160, progressive, planar yuv422p10le
```

and emits:

```text
1920x1080, progressive, planar yuv420p10le
```

There is no audio path and no metadata path.

## Decode boundary

An FFmpeg decoder feeding the scaler must make matrix, transfer, range, and
chroma-location interpretation explicit. The scaler does not infer or convert
those properties; it resamples stored Y′CbCr code values.

Example synthetic-input smoke test:

```sh
ffmpeg -v error -f rawvideo -pixel_format yuv422p10le \
  -video_size 3840x2160 -framerate 24 -i fixture.yuv \
  -frames:v 3 -f null -
```

Example raw-output decode check:

```sh
ffmpeg -v error -f rawvideo -pixel_format yuv420p10le \
  -video_size 1920x1080 -framerate 24 -i output.yuv \
  -frames:v 3 -f null -
```

These commands validate raw framing only. They do not define colorimetry.

## Container and codec boundary

If raw output is encoded:

- the encoder must receive 10-bit 4:2:0 without an unintended range transform;
- timing, metadata, and chroma-location tags must be handled deliberately;
- lossy codec assessment must be separate from scaler assessment;
- a lossless codec may prove decoded identity but is not part of Spline36.

No particular delivery service, encoder, codec, or container is required by
the core build or test suite.

## File safety

`f64resize_yuv` is a reference tool, not a transactional file manager. Never
use the same path for input and output. Production orchestration should write a
unique temporary output, validate it, then promote it atomically.
