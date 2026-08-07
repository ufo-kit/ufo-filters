# `general-backproject` — developer reference

Reference for [`src/ufo-general-backproject-task.c`](../src/ufo-general-backproject-task.c) (2359 lines)
and its kernel templates in [`src/kernels/templates/`](../src/kernels/templates/).

This is developer documentation. `docs/` is otherwise a Sphinx `.rst` tree of *user*-facing task
reference, so this file is deliberately not wired into the Sphinx build.

`general-backproject` is the most general backprojector in the tree: it covers parallel-beam and
cone-beam geometry, laminography (tilted rotation axis), shifted and tilted detectors, volume
rotation, per-projection ("vectorized") geometry, and sweeping an arbitrary geometric parameter along
the output's third dimension. It achieves that by **generating its OpenCL kernel source at runtime**
from the property values, so that only the code paths a given geometry actually needs are emitted.

Two things to unlearn before reading the code, because both are natural assumptions and both are wrong:

- **There are no 4x4 homogeneous transformation matrices anywhere.** The geometry chain is a sequence
  of 3-vector rotations built from `(sin, cos)` pairs by three macros in
  [`general_bp_definitions.in`](../src/kernels/templates/general_bp_definitions.in). The only
  plane-like object is the detector plane, stored as a normal plus an offset
  (`detector_normal`, `detector_offset`) — i.e. `n·p + d = 0`.
- **No `-D` preprocessor options are used for specialization.** Everything is textual source
  generation. The only compiler option ever passed is `-cl-nv-maxrregcount=%u`, and only when the
  per-GPU database supplies a register cap. Even the arithmetic types are chosen by literal string
  substitution of the tokens `cfloat`, `rtype`, `stype` (`make_kernel()`, lines 1160-1164).

---

## 1. Property reference

Properties are enumerated in the `PROP_*` enum (427-461), installed in `_class_init()` (2071-2317),
and handled in `_set_property()` (1767-1873) / `_get_property()` (1875-1981). Defaults are set both in
the `GParamSpec` and again in `_init()` (2319-2358).

Every `g_param_spec_value_array` property shares one element spec, `double_region_vals` (2079-2085,
range `-INFINITY..INFINITY`, default `0.0`).

### Two distinct uses of `UfoScarray`

`UfoScarray` ([`src/common/ufo-scarray.h`](../src/common/ufo-scarray.h)) is a value array used here in
two unrelated ways — a frequent source of confusion:

1. **Scalar-or-per-projection geometry.** Either one value (broadcast to every projection) or exactly
   `num-projections` values. `ufo_scarray_get_double()` returns element 0 regardless of the requested
   index when the array holds exactly one value — that *is* the broadcast mechanism.
2. **A fixed 3-tuple `(from, to, step)`** for the three region properties.

Length is converted to a slice count by the `REGION_SIZE` macro (line 45), which is **half-open**:

```c
#define REGION_SIZE(region) (ceil ((ufo_scarray_get_double ((region), 1) - ufo_scarray_get_double ((region), 0)) /\
                             ufo_scarray_get_double ((region), 2)))
```

Note `lamino-backproject` defines its own incompatible `REGION_SIZE` (`(to-from-1)/step + 1`). There is
no shared helper in `common/ufo-math.h`.

### 1.1 Volume / region

| Property | Type | Default | Meaning |
|---|---|---|---|
| `region` | 3-tuple | `[0,0,0]` | Sweep range of the **third output axis**, whose meaning is set by `parameter`. Units are pixels for positional parameters, radians for angular ones. `step == 0` falls back to a single slice `[0,1,1]` (1518-1522). Becomes `priv->num_slices` (1529). Asserted to hold exactly 3 values (1491). |
| `x-region` | 3-tuple | `[0,0,0]` | Horizontal extent of each slice, in volume coordinates. `step == 0` ⇒ slice width = **input projection width** (1495-1500). |
| `y-region` | 3-tuple | `[0,0,0]` | Extent along the beam direction. `step == 0` ⇒ slice height = **input projection width**, not height — the code assumes a square slice (1501-1507, comment at 1502-1503). |
| `z` | `double` | `0.0` | Z coordinate of the slice plane. Bound to kernel arg `slice_z_position`. **Dead whenever `parameter == z`** (the default), because the kernel immediately overwrites `voxel_0.z` with `region[idz].x`. |
| `parameter` | enum | `z` | Which quantity is swept along the third dimension. Nicks (345-365): `z`, `axis-angle-{x,y,z}`, `volume-angle-{x,y,z}`, `detector-angle-{x,y,z}`, `detector-position-{x,y,z}`, `source-position-{x,y,z}`, `center-position-{x,z}`. |

`x-region`/`y-region` define the **volume-coordinate grid**; the kernel computes
`voxel_0.x = mad(idx, x_region.step, x_region.from)`. They are orthogonal to `center-position-*`, which
is applied much later to convert a rotated volume coordinate into a projection-image coordinate.

### 1.2 Geometry — rotation axis

All `UfoScarray`. `center-position-*` map onto `geometry->axis->position`, `axis-angle-*` onto
`geometry->axis->angle`.

| Property | Default | Units | Meaning |
|---|---|---|---|
| `center-position-x` | `[0]` | px | Horizontal position of the rotation axis **in the projection image** — the classic "center of rotation". Added as `center_position.xz` at the final texture fetch (line 999). |
| `center-position-z` | `[0]` | px | Vertical offset of the volume origin in the projection. |
| `axis-angle-x` | `[0]` | rad | Tilt of the rotation axis about x. **This is the laminographic angle**; 0 means conventional tomography. |
| `axis-angle-y` | `[0]` | rad | Axis tilt about y (the beam direction). |
| `axis-angle-z` | `[0]` | rad | **Special — this is the tomographic angle series, not a static tilt.** See below. |

`geometry->axis->position->y` exists but is **never used**: `center_position[1]` is hard-set to `0.0f`
(line 225) with a `TODO: use only 2D center in the kernel`.

`axis-angle-z` is unlike every other property. If it does not already carry exactly `num-projections`
values, `setup()` **replaces it wholesale** (1435-1446) with the equidistant series
`i / num_projections * overall_angle`. Any scalar previously stored is discarded, not used as an
offset. It is also not a kernel argument in either header — it is uploaded per projection as the
`tomo_%02d` arguments in `process()`.

### 1.3 Geometry — source, detector, volume

| Property | Default | Meaning |
|---|---|---|
| `source-position-x` / `-z` | `[0]` | Lateral / vertical source position. Non-zero sets `shifted_source`. |
| `source-position-y` | `[-INFINITY]` | Source-to-origin distance along the beam. **`-inf` means parallel beam** — this is how parallel beam is encoded. `parallel_beam` is true only if `isinf()` holds for *every* projection index (1316-1320). |
| `detector-position-x` / `-z` | `[0]` | Lateral / vertical detector shift; non-zero sets `shifted_detector`. |
| `detector-position-y` | `[0]` | Detector distance along the beam. Together with `source-position-y` this fixes the magnification. |
| `detector-angle-x` / `-y` / `-z` | `[0]` | Detector tilts. Any non-zero value clears `perpendicular_detector` and switches on the full ray/plane-intersection path. |
| `volume-angle-x` / `-y` / `-z` | `[0]` | Rigid rotation of the reconstructed volume grid, applied *before* the tomographic rotation. Any non-zero sets `with_volume`. |

### 1.4 Sampling, output types, performance

| Property | Type | Default | Meaning |
|---|---|---|---|
| `num-projections` | `uint` | `0` | **Mandatory** — `setup()` errors out if unset (1429-1433). |
| `overall-angle` | `double` | `2π` | Total angular range. May be negative for descending steps. Drives the synthesized `axis-angle-z` series and `norm_factor = fabs(overall_angle)/num_projections`. |
| `addressing-mode` | enum | `clamp` | Sampler addressing for fetches outside the detector. Values from [`ufo-addressing.h`](../src/common/ufo-addressing.h): `none`, `clamp_to_edge`, `clamp`, `repeat`, `mirrored_repeat` (the pspec blurb lists only four). |
| `compute-type` | enum | `float` | Arithmetic precision in the kernel. `float` \| `double`. Substituted for the token `cfloat`, so `cfloat3` becomes `float3`/`double3`. Also selects the C-side `cl_float`/`cl_double` path via function-pointer tables (1483-1484). |
| `result-type` | enum | `float` | Type of the per-voxel accumulator. `half` \| `float` \| `double`. Substituted for `rtype`. |
| `store-type` | enum | `float` | Element type of the output volume. `half` \| `float` \| `double` \| `uchar` \| `ushort` \| `uint`. Substituted for `stype`. |
| `gray-map-min` / `-max` | `double` | `0` | Gray window for integer store types. `setup()` rejects `min >= max` for `uchar`/`ushort`/`uint` (1448-1453). |
| `burst` | `uint` | `0` (auto) | Projections folded into one kernel invocation. `0` looks the value up per GPU name from `ufo_get_node_props_table()` ([`ufo-conebeam.c`](../src/common/ufo-conebeam.c)): generic 8, GTX Titan 24, GTX 1080 Ti 24, Quadro RTX 8000 30. |

> **Caveat on `store-type`.** The output `UfoBuffer` is sized by the framework in units of `float` from
> the 2-D requisition, but `generate()` copies `get_type_size(store_type)` bytes per element. Only
> `store-type = float` produces a downstream-correct buffer; `double` overruns and the narrow types
> under-fill, leaving the consumer to reinterpret bytes as floats. Flagged in-source by the
> `TODO: handle other data types` at line 1718.

---

## 2. Geometry model

### 2.1 Coordinate system

Right-handed. **The length unit is one detector pixel** — every position property is in pixels.

- **x** — horizontal in the projection image (detector column direction)
- **y** — beam propagation direction; source at `source_position.y` (negative, or `-inf`), detector at `detector_position.y`
- **z** — vertical in the projection image (detector row direction); the nominal rotation axis

The origin is the nominal center of rotation. The chain maps a **voxel index** `(idx, idy, idz)` to a
**detector pixel coordinate**; `center_position.xz` is added last (line 999) to land in projection-image
coordinates.

### 2.2 Data structures

From [`src/common/ufo-ctgeometry.h`](../src/common/ufo-ctgeometry.h):

```c
typedef struct { UfoScarray *x, *y, *z; }        UfoScpoint;
typedef struct { UfoScpoint *position, *angle; } UfoScvector;
typedef struct {
    UfoScpoint  *source_position;
    UfoScpoint  *volume_angle;
    UfoScvector *axis;        /* axis->position == "center"; axis->angle == tilt + tomo series */
    UfoScvector *detector;
} UfoCTGeometry;
```

`ufo_ctgeometry_new()` builds the **parallel-beam default**: every component is a 1-element zero
scarray except `source_position->y`, initialized to `-INFINITY`.

[`src/common/ufo-conebeam.c`](../src/common/ufo-conebeam.c) is only 39 lines and contains just the
per-GPU properties table — despite the name, **no cone-beam weighting code lives there**. The FDK
weight is emitted as kernel text by `make_transformations()`.

### 2.3 Rotation primitives

`general_bp_definitions.in` in full — `trig` is always a `(sin, cos)` pair:

```c
#define rotate_x(trig, point) ((cfloat3)(((point).x), mad ((trig).y, (point).y, - ((trig).x * (point).z)), mad ((trig).x, (point).y, ((trig).y * (point).z))))
#define rotate_y(trig, point) ((cfloat3)(mad ((trig).y, (point).x, ((trig).x * (point).z)), ((point).y), mad (-(trig).x, (point).x, ((trig).y * (point).z))))
#define rotate_z(trig, point) ((cfloat3)(mad ((trig).y, (point).x, - ((trig).x * (point).y)), mad ((trig).x, (point).x, ((trig).y * (point).y)), ((point).z)))
```

Host-side angles are converted to `(sin, cos)` by `fill_sincos_*` (macro at 48-54).

### 2.4 Transformation chain, in emitted order

**Stage 0 — voxel grid** (body lines 11-13, static template text):

```c
voxel_0.x = mad(idx, x_region.y, x_region.x);   /* x_region = (from, step) */
voxel_0.y = mad(idy, y_region.y, y_region.x);
voxel_0.z = slice_z_position;
```

**Stage 1 — parameter injection**, `make_parameter_assignment()` (754-775):

- `parameter == z` → `voxel_0.z = region[idz].x;` (overwrites `slice_z_position`)
- positional → e.g. `source_position.y = region[idz].x;`
- angular → `axis_angle_x = region[idz];` — the whole `(sin,cos)` pair, which is why angular
  parameters store sin/cos in the region buffer, and why the scalar header declares the angle
  arguments **non-`const`**.

**Stage 2 — rotation-angle-independent block**, `make_static_transformations()` (807-851):

| Condition | Emitted |
|---|---|
| `!parallel_beam` | magnification pre-scale `voxel_0 *= -source_position.y / (detector_position.y - source_position.y)` |
| `!perpendicular_detector` | `detector_normal = (0,-1,0)` then `rotate_z`, `rotate_y`, `rotate_x` by the detector angles (**Z→Y→X**), then `detector_offset = -dot(detector_position, detector_normal)` |
| `perpendicular_detector && !parallel_beam` | `project_tmp = detector_position.y - source_position.y` |
| `with_volume` | volume rotation of `voxel_0`, again **Z→Y→X** |
| `!(perpendicular_detector \|\| parallel_beam)` | `tmp_transformation = -(detector_offset + dot(source_position, detector_normal))` |

For a plain parallel-beam, perpendicular-detector, unrotated-volume setup this block is **empty**.

**Stage 3 — per-projection block**, `make_transformations()` (909-1048), replicated `burst` times:

| Order | Condition | Emitted |
|---|---|---|
| 3a | always | `voxel = rotate_z(tomo_%02d, voxel_0);` — the tomographic rotation |
| 3b | `with_axis` | `rotate_y(axis_angle_y)` then `rotate_x(axis_angle_x)` — the laminographic tilt |
| 3c | `!parallel_beam` | FDK distance weight `coeff = (source.y - detector.y)/(source.y - voxel.y)`, cited in-source as eq. 30 of *"Direct cone beam SPECT reconstruction with camera tilt"* |
| 3d | always | ray/plane intersection — four variants, below |
| 3e | `!perpendicular_detector \|\| shifted_detector` | `voxel -= detector_position;` and, if tilted, the **inverse** detector rotation (X→Y→Z, negated sines) |
| 3f | always | `result += read_imagef(projection_%02d, sampler, (voxel.xz + center_position.xz)).x` |
| 3g | `!parallel_beam` | ` * coeff * coeff` appended |

`make_projection_computation()` (863-891) variants:

- **perpendicular + parallel** — nothing at all; `voxel.xz` *is* the detector coordinate. Only a comment is emitted.
- **perpendicular + cone** — `voxel = mad(project_tmp/(voxel.y - source.y), (voxel [- source]), source);`
- **tilted + parallel** — intersect a y-parallel ray with the detector plane: `voxel.y = -(voxel.z*n.z + voxel.x*n.x + offset)/n.y;`
- **tilted + cone** — `voxel -= source; voxel = mad(tmp_transformation/dot(voxel, n), voxel, source);`

**Stage 4 — normalize and store** (body lines 26-30, text from `make_type_conversion()`, 705-719):
float-ish store types get `(stype)(norm_factor * result)`; integer store types get a clamped gray
mapping `clamp(gray_limit.y * (norm_factor*result - gray_limit.x), 0, MAX)`.

### 2.5 Geometry flags — what actually selects the code

Derived in `node_setup()` (1305-1336). These booleans, not the property values, decide which fragments
get emitted:

```
with_axis              = is_axis_parameter(p) || axis->angle->{x,y} not both ~0
with_volume            = is_volume_parameter(p) || volume_angle not ~0
shifted_detector       = detector->position->{x,z} not both ~0
shifted_source         = source_position->{x,z} not both ~0
perpendicular_detector = p is not a detector rotation/position parameter && detector->angle ~0
parallel_beam          = isinf(source_position->y[i]) for ALL i
vectorized             = any of 16 geometry scarrays has exactly num_projections values
```

`shifted_source` is threaded through every generator signature but is only actually consulted inside
`make_projection_computation()`; `shifted_detector` only in stage 3e.

---

## 3. Kernel construction

### 3.1 `make_template()` (622-663)

Concatenates three fragments loaded at runtime via `ufo_resources_get_kernel_source()`:

```
definitions  +  header (scalar | vector)  +  body
```

For vectorized kernels with a non-`z` parameter it first renames that parameter's kernel argument by
appending `_global`, so the private per-angle variable can reuse the plain name. It also replaces a
`%memspace%` token that **no longer appears in either header** — a no-op kept from an earlier revision.

### 3.2 `make_kernel()` (1069-1180) and the nine slots

`g_strsplit(template, "%tmpl%", 9)` — eight occurrences (2 in the header, 6 in the body) give 9 parts,
reassembled at 1150-1159:

| After | Filled with | Generator |
|---|---|---|
| `parts[0]` | `burst` × `read_only image2d_t projection_%02d,` | `make_args()` |
| `parts[1]` | `burst` × `const cfloat2 tomo_%02d,` | `make_args()` |
| `parts[2]` | vectorized only: private variable declaration | `make_parameter_initial_assignment()` |
| `parts[3]` | third-axis parameter assignment from `region[idz]` | `make_parameter_assignment()` |
| `parts[4]` | **scalar only**: the angle-independent block | `make_static_transformations()` |
| `parts[5]` | the `burst`-times unrolled per-projection block | `make_transformations()` |
| `parts[6]` | type conversion (the `+=` branch) | `make_type_conversion()` |
| `parts[7]` | *the same string again* (the `=` branch) | idem |

Pragmas are prepended when needed: `cl_khr_fp64` if either `compute-type` or `result-type` is `double`,
`cl_khr_fp16` if either is `half`.

Then three whole-source substitutions (1160-1164): `cfloat` → compute type, `rtype` → result type,
`stype` → store type. Because `cfloat2`/`cfloat3` contain the token as a substring, they become
`float2`/`double3` automatically.

### 3.3 How `burst` unrolls

`burst` is the unroll factor, consumed in three places:

1. **Signature width** — `burst` image arguments and `burst` trig arguments.
2. **Body replication** — `make_transformations()` builds one snippet then loops `burst` times.
   Two distinct tokens are used deliberately (comment at 1026): `%02d` for *name* suffixes and `%d` →
   `iteration + i` for *data* indices, because `%02d` alone would produce octal-looking indices and
   break for `burst > 7`.
3. **Buffer count** — `create_images()` allocates `burst` `image2d_t` objects.

Size guards: the per-iteration snippet buffer is 16 KiB and the total is `burst * 16 KiB`, with an
overflow check.

### 3.4 Scalar vs. vectorized

| | scalar | vectorized |
|---|---|---|
| header | angles/positions passed **by value** | all 11 become `global cfloat2*` / `global cfloat3*` |
| indexing | `[%d]` stripped entirely | `[%d]` → `iteration + i`, so each burst step indexes its own projection |
| static block | emitted once at `parts[4]` | emitted **inside every burst iteration** — "for vectorized kernel all static transformations become non-static" (comment at 950) |
| upload | 11 `clSetKernelArg` by value | 11 `cl_mem` buffers built by `transfer_angular_argument_*` (sin/cos pairs, `2N`) and `transfer_positional_argument_*` (`4N`, xyzw) |

For a positional parameter under vectorization, the whole global 3-tuple is loaded per angle and only
the swept component overwritten — so the other two components stay under tomographic-angle control
(comment at 938-942).

### 3.5 Argument index map

`REAL_SIZE_ARG_INDEX = 1`, `STATIC_ARG_OFFSET = 18`:

```
 0                     sampler
 1                     real_size (int3)          — re-bound per chunk in process()
 2,3,4                 x_region, y_region, slice_z_position
 5,6                   axis_angle_x, axis_angle_y
 7,8,9                 volume_angle_x/y/z
10,11,12               detector_angle_x/y/z
13,14,15               center_position, source_position, detector_position
16,17                  norm_factor, gray_limit
18 .. 18+B-1           projection_00 .. projection_{B-1}
18+B .. 18+2B-1        tomo_00 .. tomo_{B-1}
18+2B                  iteration
18+2B+1                volume  (chunk buffer)
18+2B+2                region  (per-chunk constant buffer)
```

`axis_angle_z` is deliberately absent — it is the tomographic series, delivered as `tomo_*`.

### 3.6 The rest kernel

If `num_projections % burst != 0`, `node_setup()` compiles a **second, separate kernel** with
`burst' = num_projections % burst` (1370-1392). Both are named `backproject`. `process()` picks between
them by projection index (1640-1648). Because `count` never reaches `num_projections`, the
divide-by-zero that the sibling `rgba-backproject` guards against explicitly cannot fire here — but
the safety is *implicit*, worth knowing if this logic is ever copied.

A commented-out `g_printf ("%s", kernel_code);` at line 1390 is the intended hook for dumping generated
source.

### 3.7 A fully assembled kernel

Parallel beam, scalar geometry, `burst = 2`, `parameter = z`, all types `float`, no axis tilt, no volume
rotation, perpendicular detector. Note the static-transformation block comes out **empty**, and
`voxel_0.z = slice_z_position` is dead because stage 1 immediately overwrites it.

```c
#define rotate_x(trig, point) ((float3)(((point).x), mad ((trig).y, (point).y, - ((trig).x * (point).z)), mad ((trig).x, (point).y, ((trig).y * (point).z))))
#define rotate_y(trig, point) ((float3)(mad ((trig).y, (point).x, ((trig).x * (point).z)), ((point).y), mad (-(trig).x, (point).x, ((trig).y * (point).z))))
#define rotate_z(trig, point) ((float3)(mad ((trig).y, (point).x, - ((trig).x * (point).y)), mad ((trig).x, (point).x, ((trig).y * (point).y)), ((point).z)))
kernel void backproject (const sampler_t sampler,
                         const int3 real_size,
                         const float2 x_region,
                         const float2 y_region,
                         const float slice_z_position,
                         float2 axis_angle_x,
                         float2 axis_angle_y,
                         float2 volume_angle_x,
                         float2 volume_angle_y,
                         float2 volume_angle_z,
                         float2 detector_angle_x,
                         float2 detector_angle_y,
                         float2 detector_angle_z,
                         float3 center_position,
                         float3 source_position,
                         float3 detector_position,
                         const float norm_factor,
                         const float2 gray_limit,
			 read_only image2d_t projection_00,
			 read_only image2d_t projection_01,
			 const float2 tomo_00,
			 const float2 tomo_01,

                         const int iteration,
                         global float *volume,
                         constant float2 *region)
{
    int idx = get_global_id (0);
    int idy = get_global_id (1);
    int idz = get_global_id (2);
    float3 voxel_0, voxel, detector_normal;
    float project_tmp, coeff, detector_offset, tmp_transformation;
    float result = 0.0;

    if (idx < real_size.x && idy < real_size.y && idz < real_size.z) {
        voxel_0.x = mad((float) idx, x_region.y, x_region.x);
        voxel_0.y = mad((float) idy, y_region.y, y_region.x);
        voxel_0.z = slice_z_position;

        // assign z-coordinate to the chosen parameter i.e. param = region[idz];
        voxel_0.z = region[idz].x;

        // Start rotation angle independent transformations and temporary assignments

        // End rotation angle independent transformations and temporary assignments

        // Start rotation angle dependent transformation, pixel fetch and slice weighing
	/* Tomographic rotation angle 00 */
	voxel = rotate_z (tomo_00, voxel_0);
	// Compute the voxel projected on the detector plane in the global coordinates
	// V = S + u * (V - S)
	// Perpendicular detector in combination with parallel beam geometry, i.e.
	// voxel.xz is directly the detector coordinate, no transformation necessary
	result += read_imagef (projection_00, sampler, (voxel.xz + center_position.xz)).x;

	/* Tomographic rotation angle 01 */
	voxel = rotate_z (tomo_01, voxel_0);
	// Compute the voxel projected on the detector plane in the global coordinates
	// V = S + u * (V - S)
	// Perpendicular detector in combination with parallel beam geometry, i.e.
	// voxel.xz is directly the detector coordinate, no transformation necessary
	result += read_imagef (projection_01, sampler, (voxel.xz + center_position.xz)).x;

        // End rotation angle dependent transformation, pixel fetch and slice weighing

        if (iteration) {
            volume[idz * real_size.x * real_size.y + idy * real_size.x + idx] += (float) (norm_factor * result);
        } else {
            volume[idz * real_size.x * real_size.y + idy * real_size.x + idx] = (float) (norm_factor * result);
        }
    }
}
```

The ragged indentation is an artifact: generators emit their own leading tabs on top of the template's
8-space indent.

---

## 4. Execution flow

`UFO_TASK_MODE_REDUCTOR | UFO_TASK_MODE_GPU`, one 2-D input. The scheduler calls `process()` once per
projection, then drains `generate()` until it returns `FALSE`.

### `setup()` (1411-1462)

Fails if `num-projections` is unset; synthesizes the tomographic angle series; validates the gray map
for integer store types; loads the per-GPU properties table; creates one shared sampler
(`normalized_coords = FALSE`, user addressing mode, `CL_FILTER_LINEAR`).

### `get_requisition()` (1464-1587)

Sets the **output dims for one slice** on every call (1495-1507). Everything else runs once, guarded by
`if (!priv->kernel)`:

1. `node_setup()` builds and compiles the kernel(s).
2. `num_slices = ceil((region_stop - region_start)/region_step)`.
3. **Chunking** — the volume is split so no single allocation exceeds
   `MIN(device max alloc, 4 GiB)`. The 4 GiB cap is a deliberate workaround: cards that *claim* more
   still return OpenCL errors (comment 1537-1538). Errors out with "Volume size doesn't fit to memory"
   if `projections_size + volume_size` exceeds global memory.
4. Allocates chunk buffers, `burst` projection images, and one region buffer per chunk.
5. Binds static kernel arguments for both the main and rest kernels.

### `process()` (1610-1706) — the accumulation pattern

`num_processed` is a read-only `UfoTaskNode` property incremented by the scheduler **after** each call,
so inside `process()` it is the 0-based index of the current projection.

1. Select main vs. rest kernel and compute `index`, the slot within the current burst.
2. Compute work sizes (see §5).
3. Upload this projection's tomographic angle as `(sin, cos)` into argument `STATIC_ARG_OFFSET + burst + index`.
4. `copy_to_image()` — a **device-to-device** `clEnqueueCopyBufferToImage` from the input buffer's
   device array into `projections[index]`. The projection never round-trips through the host.
5. **Launch only when the burst is full** (`index + 1 == burst`), once per chunk.

The key idea is `iteration = count + 1 - burst`, the global index of the first projection in this
burst. The kernel uses it two ways: as the `!= 0` flag choosing `+=` over `=` in the store, and (when
vectorized) as the base index into the per-projection geometry buffers. **This is why the chunk buffers
are never explicitly zeroed** — the very first burst *writes*, every later burst *adds*.

### `generate()` (1708-1764)

Emits exactly one 2-D slice per call. Warns and produces nothing at all if fewer than
`num-projections` projections arrived. Selects the chunk by `generated / num_slices_per_chunk` and the
slice within it by `generated % num_slices_per_chunk`, then `clEnqueueCopyBufferRect`s it into the
output buffer.

---

## 5. Performance design

- **Projections live in textures** — `burst` separate `image2d_t` objects, `CL_INTENSITY` + `CL_FLOAT`,
  fetched through one shared sampler with `CL_FILTER_LINEAR`. Hardware bilinear interpolation is the
  whole reason for using images. No `image3d_t` anywhere. The sampler is passed as **argument 0**
  rather than declared `constant sampler_t` in-kernel, so the addressing mode is runtime-configurable
  without recompiling.
- **`burst` is register blocking.** One work-item accumulates `burst` projections into a register
  before touching global memory, amortizing the voxel-grid setup, the static transformations, and — the
  main win — the read-modify-write of `volume[...]` over `burst` projections instead of one. The
  per-GPU table pairs high burst values with `-cl-nv-maxrregcount=32` to stop occupancy collapsing
  under the resulting register pressure.
- **Launches:** exactly `num_chunks` per full burst, i.e. usually **one launch per `burst` projections**.
- **Explicit local work size** (1650-1666) — unusual in this repo. It round-robin doubles across the
  three dimensions until the device's max work-group size is consumed (1024 → `{16,8,8}`; 256 →
  `{8,8,4}`), rounds global sizes up with `NEXT_DIVISOR`, and masks the excess with the
  `idx < real_size.x && ...` guard in the body.
- **`ufo_profiler_call_blocking()` is genuinely required here** (line 1701), for two independent
  reasons: kernel arguments are re-bound *inside* the chunk loop, and `clSetKernelArg` on a kernel with
  an in-flight NDRange is undefined behavior; and the projection image ring has only `burst` slots, so
  the next `process()` call would overwrite slot 0. There is no event/fence chaining — blocking *is*
  the synchronization.

Two resource defects worth knowing if you touch this code:

1. `set_static_vector_arguments_*` allocates `priv->vector_arguments` on **every** call and is called
   twice from `get_requisition()` (main kernel and rest kernel). The first array of 11 `cl_mem` handles
   is leaked.
2. `finalize()` releases all 11 vector-argument handles without null checks, while `priv->projections`
   is checked per element.

---

## 6. Contrast with `rgba-backproject`

[`ufo-rgba-backproject-task.c`](../src/ufo-rgba-backproject-task.c) is a narrow, heavily optimized
parallel-beam backprojector. The comparison is the clearest way to see what generality costs.

| | `rgba-backproject` | `general-backproject` |
|---|---|---|
| Texture format | `CL_RGBA` + `CL_HALF_FLOAT` — 4 detector rows packed per texel | `CL_INTENSITY` + `CL_FLOAT`; every fetch uses `.x` only |
| Texture object | one `image2d_array_t`, `array_size = burst` | `burst` separate `image2d_t` arguments |
| Fetch efficiency | one `read_imagef` serves **four slices** | one fetch serves one voxel |
| Kernels | three (`accumulate` → `backproject` → `distribute`) | one generated kernel (plus rest kernel) |
| Accumulator | `float4` buffer, explicitly zero-filled | scalar buffer; initialized by the `iteration == 0` store |
| Work-item mapping | one item per **four** z-slices | one item per voxel |
| Geometry | parallel beam only, no tilts | arbitrary cone beam, laminography, tilts, per-projection vectors |
| Properties | 8 | 32 |

**Why `general-backproject` cannot use the RGBA trick.** It relies on four slices sharing the *same*
detector coordinate for a given angle — the detector column a voxel projects onto is independent of z.
That holds only for pure parallel-beam, perpendicular-detector, untilted-axis geometry. Under cone-beam
magnification, a tilted detector, or a tilted axis, the four slices project to four unrelated detector
coordinates and the packing buys nothing. `general-backproject` trades that ~4x texture-bandwidth win
for generality and recovers throughput instead through `burst` unrolling and per-GPU register tuning.
