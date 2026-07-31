# Active-region and chroma geometry

## Plane geometry

`PlaneGeometry` carries:

- integer stored plane dimensions;
- binary64 active-left and active-top coordinates;
- binary64 active width and height.

Progressive chroma conversion follows the pinned GraphBuilder structure:

1. scale the luma active region by the ideal power-of-two subsampling factor;
2. derive pixel-siting offsets from the actual integer plane-size ratio;
3. construct independent source and destination chroma active regions;
4. derive resize shift and active subwidth from those regions.

The ideal active-region factor and actual integer plane ratio can differ for
odd dimensions. The public zimg graph builder rejects some non-divisible
plane layouts; property tests covering those layouts are defensive tests of
the fork's generic formula, not a claim of upstream graph acceptance.

## Siting offsets

For horizontal subsampling ratio `s = chroma_width / luma_width`:

```text
LEFT offset   = -0.5 + 0.5s
CENTER offset = 0
```

For vertical ratio `s = chroma_height / luma_height`:

```text
TOP offset    = -0.5 + 0.5s
CENTER offset = 0
BOTTOM offset = +0.5 - 0.5s
```

The offset is subtracted from the ideally subsampled active position.

## Axis derivation

For source and destination active positions and widths:

```text
active_scale = destination_active_width / source_active_width
shift        = source_active_position
             - destination_active_position / active_scale
subwidth     = source_active_width
             * destination_plane_dimension
             / destination_active_width
```

All geometry inputs are checked for zero dimensions, non-finite values,
non-positive active sizes, and overflow-prone conversions.

## Full-frame 4:2:2 LEFT → 4:2:0 LEFT

For the validated progressive full-frame conversion:

```text
source chroma active_left       = +0.25
destination chroma active_left  = +0.25
horizontal active scale         = 960 / 1920 = 0.5

shift = source_left - destination_left / scale
      = 0.25 - 0.25 / 0.5
      = -0.25 source-chroma samples

vertical shift = 0
```

The reference executable derives and verifies each intermediate value.

`-0.25` is not embedded as a universal chroma rule. Different crops, active
regions, stored plane ratios, or LEFT/CENTER/TOP/BOTTOM sitings can produce a
different shift.

## Source interpretation

The inputs used to validate the original format-specific pipeline did not
carry authoritative chroma-location metadata. Progressive 4:2:2 LEFT was an
explicit interpretation assumption, not a fact proven by the files.

For a 4:2:2 source, LEFT and TOPLEFT are horizontally equivalent because
there is no vertical chroma subsampling. The 4:2:0 destination uses LEFT
horizontally and centered progressive vertical siting.

