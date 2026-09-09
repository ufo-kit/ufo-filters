# `rgba-backproject` — developer reference

Reference for [`src/ufo-rgba-backproject-task.c`](../../src/ufo-rgba-backproject-task.c) and
[`src/kernels/rgba-backproject.cl`](../../src/kernels/rgba-backproject.cl).

This is internal developer documentation. It is deliberately not part of the Sphinx toctree: the
public task reference belongs in [`docs/filters.rst`](../filters.rst), while this file records the
implementation, memory layout, indexing, and maintenance invariants in enough detail to modify the
task safely.

The kernel listings below are snapshots of the current source. If a kernel signature or indexing
rule changes, update this document in the same change. The `.cl` file remains the executable source
of truth.

---

## 1. Purpose and constraints

`rgba-backproject` reconstructs conventional parallel-beam tomography from a stream of two-
dimensional projections. It is specialized around one observation: for this geometry, four detector
rows undergo the same in-plane rotation and differ only in their reconstructed z position. Those
four independent rows can therefore be stored in the R, G, B, and A channels of one texture pixel,
sampled with one `read_imagef`, and accumulated as one `float4`.

This is intentionally narrower than [`general-backproject`](general-backproject.md):

- the projection angles are equidistant;
- the rotation axis is parallel to detector z;
- there are no source/detector distances, cone-beam weights, detector tilts, axis tilts, or volume
  transformations;
- reconstructed slices may be rectangular and use independent positive x/y sampling steps;
- projection texture storage is RGBA half precision;
- computation and output accumulation are single precision;
- the task scales the completed projection sum by the angular sampling interval.

The specialization makes the data path much simpler than runtime-generated general-backprojection
kernels, but it also makes the buffer shapes and the four-row packing invariant fundamental to the
implementation.

### 1.1 Notation

The rest of this document uses the following symbols consistently.

| Symbol | Meaning |
|---|---|
| `P` | Configured total number of projections, `num-projections`. |
| `B` | Configured projections per singular burst or per parity, `burst`. The default is 16. |
| `b` | Number of projections in the current batch; at most `B` in singular mode and `2B` in even/odd mode. |
| `W` | Input projection width, `in_req.dims[0]`; detector x/column count. |
| `H` | Input projection height, `in_req.dims[1]`; detector z/row count. |
| `Z` | Number of slices requested by the resolved z region, `num_slices_actual`. |
| `Z4` | Internal z count padded upward to a multiple of four, `num_slices_processing`. |
| `G` | Number of RGBA z groups, `Z4 / 4`. |
| `x0`, `dx` | Resolved x-volume origin and positive sampling step. |
| `y0`, `dy` | Resolved y-volume origin and positive sampling step. |
| `Nx`, `Ny` | Reconstructed slice width and height. |
| `V` | Reconstructed volume count: one in singular mode and two in even/odd mode. |

Thus:

```text
Z  = ceil((z_stop - z_start) / z_step)
Z4 = 4 * ceil(Z / 4)
G  = Z4 / 4
```

### 1.2 Coordinate conventions

An input projection is a row-major `W × H` float image:

- detector **x** is the horizontal coordinate and fastest-changing array dimension;
- detector **z** is the vertical coordinate and selects a detector row;
- projection coordinate `(0, 0)` is the top-left pixel;
- `center-position-x` is the rotation-axis location in this full detector coordinate frame.

A reconstructed slice has two independent in-plane volume coordinates. The OpenCL kernel names its
indices `idx` and `idy` and resolves them to `x0 + idx*dx` and `y0 + idy*dy`. Consequently, output
slices may be rectangular and may sample the volume grid at non-unit spacing.

The regions define a volume-coordinate grid, not a projection crop. The projection texture always
remains `W` pixels wide. Rotation happens around volume origin `(0,0)`, after which
`center-position-x` translates the rotated x coordinate into the full detector coordinate frame.

There is no implicit `+0.5` adjustment in the kernel. A caller translating previous detector-index
bounds into volume coordinates must include that pixel-center adjustment explicitly.

---

## 2. Properties and derived state

Properties are enumerated in the `PROP_*` enum, installed by
`ufo_rgba_backproject_task_class_init`, read and written by the GObject property handlers, and given
matching instance defaults by `ufo_rgba_backproject_task_init`. Because UFO copies GPU tasks by
copying their GObject properties, all externally configurable reconstruction state survives graph
expansion to multiple GPUs.

The five array properties use [`UfoScarray`](../../src/common/ufo-scarray.c). They are initialized as
three doubles with value zero. This task reads element zero of both center arrays and all three
elements of each region; it does not implement per-projection center positions.

| Property | Type and default | Operational meaning |
|---|---|---|
| `burst` | `uint`, default `16`, range `1..128` | Projections per singular batch or per parity. Even/odd mode therefore holds up to `2B` projections. The final batch may be shorter. |
| `num-projections` | `uint`, default `0`, range `0..32768` | Required total `P`. Although zero is allowed by the property specification, `setup` rejects it. The stream is expected to provide exactly this many projections. |
| `overall-angle` | `double`, default `π` | Total angular interval in radians. May be negative. No degrees-to-radians conversion occurs. |
| `x-region` | double `GValueArray`, default `[0,0,0]` | Half-open x-volume grid `(from,to,step)`. Zero step selects `Nx=W`, `x0=-W/2`, `dx=1`; explicit steps must be positive. |
| `y-region` | double `GValueArray`, default `[0,0,0]` | Independent half-open y-volume grid. Zero step selects `Ny=W`, `y0=-W/2`, `dy=1`. |
| `center-position-x` | double `GValueArray`, default `[0,0,0]` | Element zero is the full-detector horizontal rotation-axis coordinate. Fractional values are supported by linear sampling. |
| `center-position-z` | double `GValueArray`, default `[0,0,0]` | Element zero is added to the relative z-region start and stop. |
| `region` | double `GValueArray`, default `[0,0,0]` | Relative `(from,to,step)` detector-row selection. Stop is exclusive. A step almost equal to zero activates the fallback `(0,1,1)`. |
| `addressing-mode` | enum, default `clamp` | OpenCL sampler addressing: `none`, `clamp_to_edge`, or `clamp`. |
| `operation-mode` | enum, default `singular` | `singular` reconstructs one normalized volume; `even_odd` reconstructs separate unnormalized even and odd volumes with two parity-specific backprojection launches. |
| `output-mode` | enum, default `slices` | `slices` emits two-dimensional planes. `volume` emits one planar three-dimensional device buffer per reconstructed volume. |

This task deliberately exposes a restricted addressing enum. The backprojection kernel computes
`rho` in detector-pixel coordinates and passes it directly to `read_imagef`; the packed-row texture
coordinate is likewise expressed in texels. The
[sampler](https://registry.khronos.org/OpenCL/specs/unified/refpages/man/html/samplers.html) is therefore created with `normalized_coords = CL_FALSE`. OpenCL permits `repeat` and `mirrored_repeat` only with
normalized coordinates. Offering either mode here could make `clCreateSampler` fail with `CL_INVALID_VALUE`
or produce undefined sampling behavior. Enabling normalized coordinates would require rescaling the
kernel's sampling coordinates by the image dimensions and provides no benefit to this direct
detector-coordinate implementation, so both modes are intentionally excluded.

### 2.1 In-plane regions

For each explicit region `(from,to,step)`:

```text
require finite(from, to, step)
require step > 0 and to > from
length = ceil((to - from) / step)
```

An exact zero step is the general-backproject-compatible default sentinel. The tuple bounds are then
ignored and that axis resolves to `from=-W/2`, `step=1`, and `length=W`. The two independently
resolved lengths form output requisition `(Nx,Ny)`. Coordinates are not constrained to detector
bounds because rotation and sampler addressing determine which detector values are available.

To reproduce the former detector-index square `[177,816)` around axis `540.4`, include the old
pixel-center conversion in both explicit regions:

```text
from = 177 - 540.4 + 0.5 = -362.9
to   = 816 - 540.4 + 0.5 =  276.1
x-region = y-region = (-362.9, 276.1, 1.0)
Nx = Ny = 639
```

Once resources have been sized, a different projection shape or resolved region origin, step, or
length is rejected. This prevents different bursts from accumulating on incompatible grids.

### 2.2 Z interval

Let the user tuple be `(r_from, r_to, r_step)` and let `cz` be element zero of
`center-position-z`.

```text
if r_step is almost zero:
    r_from = 0
    r_to   = 1
    r_step = 1

require int(r_to) > int(r_from)
require int(r_step) > 0

z_start = r_from + cz
z_stop  = r_to   + cz
z_step  = r_step

require 0 <= z_start
require z_stop <= H

Z  = ceil((z_stop - z_start) / z_step)
Z4 = 4 * ceil(Z / 4)
```

`Z` may be any positive value. Only internal processing uses `Z4`. Slice output stops after `Z`
planes per volume; volume output reports depth `Z` and bounds-checks writes from the final RGBA group.

Although these properties are doubles, `process` passes `z_start` and `z_step` to the packing
kernel as `cl_int`. The early ordering checks also cast the relative tuple values to `cl_int`, while
`Z` is calculated from the stored doubles. Fractional values can therefore make the host-side slice
count disagree with the integer detector rows used by the kernel. Treat the z tuple and
`center-position-z` as integer-valued pixel coordinates.

### 2.3 Angular lookup table

`setup` constructs one interleaved host array of `2P` floats. With angular increment

```text
delta = overall_angle / P
```

projection `i`, for `0 <= i < P`, receives:

```text
host_buffer_angles[2*i + 0] = cos(i * delta)
host_buffer_angles[2*i + 1] = sin(i * delta)
```

The device buffer holds one mode-dependent batch: `B` pairs in singular mode and `2B` in even/odd
mode. Before each backprojection it is overwritten with the pairs for the current batch and
interpreted by OpenCL as `constant float2 *angle_lut`.

There is no angular offset property. After all batches have accumulated, either distribution kernel
applies the same angular sampling factor used by `general-backproject` in singular mode:

```text
normalization_factor = abs(overall_angle) / P
```

Using the absolute angular range preserves intensity sign when projections are ordered along a
negative rotation direction. Even/odd mode deliberately uses a factor of `1.0` and remains
unnormalized.

### 2.4 Batch arithmetic

`batch_capacity` is `B` in singular mode and `2B` in even/odd mode. For every incoming projection,
`process` derives:

```text
batch_start      = floor(processed_proj_count / batch_capacity) * batch_capacity
actual_burst     = min(batch_capacity, P - batch_start)
idx_actual_burst = processed_proj_count - batch_start
```

Kernels run when `idx_actual_burst + 1 == actual_burst`. Even/odd batches start at an even global
projection and alternate even/odd projections in texture layers. With the default `B=16`, a complete
even/odd batch contains 32 projections. For `P=3001`, there are 93 complete batches followed by a
25-projection tail containing 13 even projections and 12 odd projections.

The production API has two enum values: `singular=0` and `even_odd=1`. The former benchmark-stage
values `even_odd_single` and `even_odd_dual` are not aliases. Named configurations using
`even_odd_dual` must use `even_odd`; numeric configurations using value 2 must use value 1.

---

## 3. UFO task lifecycle

The task mode is `UFO_TASK_MODE_REDUCTOR | UFO_TASK_MODE_GPU`. It consumes all projections first.
`output-mode=slices` then emits one two-dimensional output per requested z slice and volume;
`output-mode=volume` emits one three-dimensional output per reconstructed volume.

```text
construct task
  -> init defaults and private pointers
  -> pipeline sets GObject properties
  -> setup(resources)
       retain context and kernels
       create sampler
       build host angle LUT
       allocate batch-capacity-sized device angle LUT
  -> for every incoming projection:
       get_requisition(input)
         validate W, H, x/y grids, z region and device limits
         allocate dimension-dependent buffers on first call
         report output shape Nx x Ny or Nx x Ny x Z
       process(input)
         copy projection into its ring-buffer slot
         if batch is complete:
           accumulate: ring buffer -> RGBA texture
           upload current angle pairs
           singular: backproject -> one coalesced volume
           even/odd: backproject_even then backproject_odd -> two coalesced volumes
  -> input stream ends
  -> repeated generate(output)
       slices: distribute selected coalesced volume -> reusable planar volume
               copy one planar slice -> output; repeat Z times per volume
       volume: distribute selected coalesced volume directly -> output device buffer
               release that coalesced accumulator; repeat once per volume
  -> finalize
       release every retained/allocated resource
```

### 3.1 Construction and setup

`ufo_rgba_backproject_task_new` constructs the GObject. `init` creates the five scarrays, installs
scalar defaults, resets counters, and nulls resource pointers. No OpenCL allocation is possible yet
because neither resources nor input dimensions are known.

`setup` is called after properties have been copied/set and before input processing:

1. retain `UfoResources` and its OpenCL context;
2. load and retain `accumulate`, the operation-specific backprojection kernel(s), and either
   `distribute` or `distribute_volume` according to `output-mode`;
3. create an unnormalized, linearly filtered sampler using `addressing-mode`;
4. reject `P == 0`;
5. allocate and fill the host angle table;
6. allocate the device table for `batch_capacity` `float2` values.

Properties affecting these resources—especially `num-projections`, `burst`, `overall-angle`, and
`addressing-mode`—must be finalized before setup. Changing them later does not rebuild the sampler or
lookup tables.

### 3.2 Requisition and dimension discovery

`get_requisition` runs before each `process` call. The first call is where `W` and `H` become known.
It resolves `Nx`, `Ny`, `Z`, and `Z4`, performs overflow/device-limit checks, allocates the
dimension-dependent OpenCL objects, and records the dimensions. Slice mode reports `(Nx,Ny)`;
volume mode reports `(Nx,Ny,Z)`. Only slice mode allocates `device_final_slices`.

Subsequent calls return the same output shape. If projection dimensions or any resolved x/y origin,
step, or length changes, the task reports an error rather than mixing coordinate grids or
reallocating midway through a reduction.

### 3.3 Per-projection processing

The ring-buffer slot is `idx_actual_burst`. A complete `W × H` projection is always stored; z-row
selection happens later in `accumulate`.

- Host-resident input is copied directly into the slot with a blocking `clEnqueueWriteBuffer`.
- Device-resident (and other non-host) input is obtained as a device array and copied into the slot
  with `clEnqueueCopyBuffer`.

At the batch boundary, `accumulate` is submitted asynchronously and the angle-table write is queued
non-blockingly. Singular mode then submits one blocking `backproject` call. Even/odd mode submits
`backproject_even` asynchronously followed by blocking `backproject_odd`. The command queue is in
order, so packing and angle transfer complete before backprojection, the even kernel completes before
the odd kernel, and the complete batch finishes before `process` returns.

The first batch overwrites every element of each selected coalesced volume. Later batches add to it.
This is why the buffers do not require a separate zero-fill.

### 3.4 Generation

Generation is refused if the framework reports fewer than `P` processed projections. Singular mode
uses `abs(overall_angle) / P`; even/odd mode uses `1.0`.

In slice mode, the first call for each volume launches `distribute` into the reusable padded planar
buffer. Every successful call then copies one plane into a scheduler-owned two-dimensional output.
`generated` counts planes across volumes, so even/odd output is all even slices followed by all odd
slices. The `Z4-Z` padding planes are never emitted.

In volume mode, `generated` counts volumes. Each call launches `distribute_volume` directly into the
scheduler-owned three-dimensional device buffer. That kernel checks every channel against `Z`, so
the output is tightly packed and unpadded. The blocking profiler call makes it safe to release the
selected coalesced accumulator immediately. Even/odd output is the even volume followed by the odd
volume. No host array is requested and no second full-volume copy is performed.

### 3.5 Finalization

`finalize` releases the projection ring buffer, texture, angle buffer, any coalesced accumulator not
already released by volume generation, the optional planar volume, retained kernels, sampler,
context, and resources object. It also frees all five scarrays and the host angle table. The
scheduler owns every input and output `UfoBuffer`; this task does not retain them.

---

## 4. Resource and buffer inventory

All UFO buffers contain 32-bit floats. The half-precision object below is a private OpenCL image, not
an output `UfoBuffer`.

| Resource | Created in | Flags/type and logical shape | Size | Written by | Read by | Released in |
|---|---|---|---|---|---|---|
| `host_buffer_angles` | `setup` | Host `float[2P]`, interleaved `(cos,sin)` | `2P * sizeof(float)` | Setup loop, once | Burst LUT uploads | `finalize` with `g_free` |
| `device_buffer_angles` | `setup` | `CL_MEM_READ_ONLY`, logical `float2[batch_capacity]` | `2*batch_capacity*sizeof(float)` | `clEnqueueWriteBuffer` before each backprojection | Backprojection kernel(s) | `finalize` |
| `device_buffer_projections` | first `get_requisition` | `CL_MEM_READ_ONLY`, logical `float[batch_capacity][H][W]` | `batch_capacity*H*W*sizeof(float)` | Host upload or device-to-device copy, one slot per input | `accumulate` | `finalize` |
| `device_texture_projections` | first `get_requisition` | `CL_MEM_OBJECT_IMAGE2D_ARRAY`, `CL_RGBA`, `CL_HALF_FLOAT`, `CL_MEM_READ_WRITE`; logical `[batch_capacity][G][W][4]` | approximately `batch_capacity*G*W*4*sizeof(half)` | `accumulate` via `write_imagef` | Backprojection kernel(s) via `read_imagef` | `finalize` |
| `device_coalesced_slices[V]` | first `get_requisition` | `CL_MEM_READ_WRITE`, each logical `float4[G][Ny][Nx]` | each `G*Nx*Ny*sizeof(float4)`, equal to `Z4*Nx*Ny*sizeof(float)` | Backprojection: assign first batch, add later batches | Backprojection, then selected distribution kernel | `finalize`, or immediately after direct volume distribution |
| `device_final_slices` | first `get_requisition`, slices only | `CL_MEM_WRITE_ONLY`, logical `float[Z4][Ny][Nx]` | `Z4*Nx*Ny*sizeof(float)` | `distribute`, including normalization | `clEnqueueCopyBufferRect` | `finalize` |
| Slice output `UfoBuffer` | UFO scheduler, per plane | Two-dimensional float buffer `[Ny][Nx]` | `Nx*Ny*sizeof(float)` | Final rectangular copy | Downstream task | UFO scheduler |
| Volume output `UfoBuffer` | UFO scheduler, per volume | Three-dimensional float buffer `[Z][Ny][Nx]` | `Z*Nx*Ny*sizeof(float)` | `distribute_volume`, including normalization | Downstream task such as 3-D FFT | UFO scheduler |
| `sampler` | `setup` | Unnormalized coordinates, linear filter, configured addressing | Driver object | Immutable | `backproject` | `finalize` |
| Selected kernels | `setup` | Resources-owned kernels retained by this task | Driver objects | Kernel arguments are reset before calls | Profiler submission | `finalize` |
| OpenCL context | `setup` | Context from `UfoResources`, explicitly retained | Driver object | — | All OpenCL allocations | `finalize` |
| `x_region`, `y_region`, `center_position_x`, `center_position_z`, `region` | `init` | `UfoScarray`, three doubles each | Host objects | Property setters | Requisition/process/setup | `finalize` |

`CL_MEM_READ_ONLY` and `CL_MEM_WRITE_ONLY` describe kernel access. Host enqueue operations can still
write the read-only ring/LUT buffers or use the write-only final buffer as a copy source.

### 4.1 Texture shape

The OpenCL descriptor is:

```text
image type  = IMAGE2D_ARRAY
width       = W
height      = G = Z4/4
array size  = batch_capacity
format      = RGBA half float
```

For texture coordinate `(x, g, p)`, the four channels hold selected detector rows
`4g + {0,1,2,3}` for projection slot `p`, after applying `z_start` and `z_step`.

The texture retains the full detector width because the computed detector coordinate `rho` can lie
outside the requested volume grid. Cropping this image to `Nx` would change the coordinate frame and produce
incorrect samples.

`write_imagef` accepts a `float4` and converts it to half storage. `read_imagef` returns interpolated
single-precision values reconstructed from that half representation. Projection values are therefore
quantized to half before backprojection, while the sum remains float.

### 4.2 Allocation checks

Before allocation, all products and sums used by the memory estimate are checked for `gsize`
overflow. The estimate includes:

```text
ring buffer + texture + V coalesced volumes + one output-sized volume + device angle LUT
```

The extra output-sized term represents `device_final_slices` in slice mode and the scheduler-owned
volume being filled in volume mode. Each allocation is compared independently with the device's
maximum allocation size, capped by the task at `2^32` bytes even when the device reports more. The
total estimate is compared with global device memory.

The image descriptor is also checked against:

```text
W <= CL_DEVICE_IMAGE2D_MAX_WIDTH
G <= CL_DEVICE_IMAGE2D_MAX_HEIGHT
batch_capacity <= CL_DEVICE_IMAGE_MAX_ARRAY_SIZE
```

These checks estimate image storage as tightly packed RGBA half data. A driver may use additional
image metadata or row alignment; `clCreateImage` remains the authoritative allocation check.

---

## 5. Kernel stages

The three stages use no explicit local work size; the OpenCL implementation chooses it. Explicit
width/height kernel arguments define flat-buffer strides independently of launch geometry.

| Stage | Calls | Global work size | Result |
|---|---:|---|---|
| `accumulate` | Once per batch | `(W, G, b)` | Packs selected rows from `b` ring slots into `b` texture layers. |
| `backproject` | Once per singular batch | `(Nx, Ny, G)` | Adds all batch projections to the singular `float4` volume. |
| `backproject_even`, `backproject_odd` | Once each per even/odd batch | `(Nx, Ny, G)` | Add alternating texture layers to the corresponding parity volume. |
| `distribute` | Once per volume in slice mode | `(Nx, Ny, G)` | Converts `float4[G][Ny][Nx]` into the reusable planar `float[Z4][Ny][Nx]`. |
| `distribute_volume` | Once per volume in volume mode | `(Nx, Ny, G)` | Converts directly into the scheduler output `float[Z][Ny][Nx]`, guarding padded channels. |

### 5.1 `accumulate`: ring buffer to texture array

```c
kernel void
accumulate(
    global float *in,
    write_only image2d_array_t out,
    const int row_start,
    const int row_step,
    const int projection_height,
    const int projection_width) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    const int size_x = projection_width;
    if (idx >= size_x)
        return;
    // Projection offset is calculated using the idz (index of the projection in its batch), means
    // with this offset a new projection starts in the flat array for a batch of projections.
    const int proj_offset = idz * size_x * projection_height;
    const int flat_y = 4 * idy;
    // row_i points to the strided input rows. Each of the [(flat_y + i) * row_step] marks increasing
    // offsets between the strided four rows. Adding these offsets to the row_start give starting
    // indices of the strided rows. Total number of rows to be processed in this way is controlled by
    // the global work size.
    int row_0 = row_start + (flat_y + 0) * row_step;
    int row_1 = row_start + (flat_y + 1) * row_step;
    int row_2 = row_start + (flat_y + 2) * row_step;
    int row_3 = row_start + (flat_y + 3) * row_step;
    // Adding each (row_i * size_x) to projection_offset provides the final starting index of the
    // rows to be processed in the flat array for the batch.
    float val_0 = (row_0 >= 0 && row_0 < projection_height) ? in[proj_offset + (row_0 * size_x) + idx] : 0.0f;
    float val_1 = (row_1 >= 0 && row_1 < projection_height) ? in[proj_offset + (row_1 * size_x) + idx] : 0.0f;
    float val_2 = (row_2 >= 0 && row_2 < projection_height) ? in[proj_offset + (row_2 * size_x) + idx] : 0.0f;
    float val_3 = (row_3 >= 0 && row_3 < projection_height) ? in[proj_offset + (row_3 * size_x) + idx] : 0.0f;
    float4 pixel = (float4)(val_0, val_1, val_2, val_3);
    write_imagef(out, (int4)(idx, idy, idz, 0), pixel);
}
```

The kernel sees the ring buffer as a flat array but its logical shape is `[b][H][W]`, with x
fastest. Work item `(idx, idy, idz)` means:

| Work-item coordinate | Meaning |
|---|---|
| `idx` | Detector column `x`, in `[0,W)`. |
| `idy` | Packed z-group `g`, in `[0,G)`. |
| `idz` | Projection slot `p`, in `[0,b)`. |

The projection base offset is:

```text
proj_offset = p * W * H
```

The four logical z indices carried by one RGBA value are:

```text
logical_z(c) = 4*g + c for c in {0,1,2,3}
detector_row(c) = row_start + logical_z(c) * row_step
```

The corresponding flat ring-buffer address is:

```text
ring_index(p, row, x) = p*W*H + row*W + x
```

| Channel | Detector row | Ring-buffer element |
|---|---|---|
| R / `.x` | `row_start + (4g+0)*row_step` | `p*W*H + row_0*W + x` |
| G / `.y` | `row_start + (4g+1)*row_step` | `p*W*H + row_1*W + x` |
| B / `.z` | `row_start + (4g+2)*row_step` | `p*W*H + row_2*W + x` |
| A / `.w` | `row_start + (4g+3)*row_step` | `p*W*H + row_3*W + x` |

If a calculated detector row is outside `[0,H)`, that channel is explicitly set to zero. This is a
detector-boundary check, not a requested-`Z` check: channels introduced only by four-slice padding
can still read and reconstruct valid neighboring rows when those rows lie inside the detector. They
are harmless because `generate` does not emit them.

Finally, the `float4` is written to image coordinate `(x, g, p)`. The image converts it to RGBA half
storage.

### 5.2 `backproject`: texture array to coalesced volume

```c
kernel void
backproject(
    read_only image2d_array_t projections,
    global float4 *slices,
    constant float2 *angle_lut,
    const float axis,
    const uint burst,
    sampler_t sampler,
    const int slice_width,
    const int slice_height,
    const float2 x_region,
    const float2 y_region,
    const uint first_burst) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    if (idx >= slice_width || idy >= slice_height)
        return;
    const float volume_x = mad ((float) idx, x_region.y, x_region.x);
    const float volume_y = mad ((float) idy, y_region.y, y_region.x);
    float4 sum = 0.0f;
    for (int proj = 0; proj < burst; proj++) {
        // angle_lut is an array of two ordered floating point values, denoting the cosine and sine
        // of rotation angles.
        const float2 angle = angle_lut[proj];
        float roh = axis + (volume_x * angle.x + volume_y * angle.y);
        sum += read_imagef(projections, sampler, (float4)(roh, idz + 0.5f, proj, 0));
    }
    const size_t plane = (size_t) slice_width * (size_t) slice_height;
    const size_t output_index = ((size_t) idz * plane) + ((size_t) idy * slice_width + idx);
    if (first_burst)
        slices[output_index] = sum;
    else
        slices[output_index] += sum;
}
```

Work item `(idx, idy, idz)` owns one in-plane output position and one RGBA z group:

```text
idx in [0,Nx)
idy in [0,Ny)
idz in [0,G)
```

The indices are mapped onto the independently configured volume grids:

```text
volume_x = x0 + idx*dx
volume_y = y0 + idy*dy
```

These values are already relative to the geometric rotation origin. The kernel performs no implicit
axis subtraction or pixel-center shift.

For projection slot `p`, the lookup pair is `(cos(theta_p), sin(theta_p))`. The detector coordinate
is:

```text
rho_p = axis + volume_x*cos(theta_p) + volume_y*sin(theta_p)
```

The texture sample is:

```text
read_imagef(projections, sampler, (rho_p, idz + 0.5, p, 0))
```

- `rho_p` is the unnormalized horizontal texture coordinate and is linearly interpolated;
- `idz + 0.5` addresses the center of packed z row `idz`;
- `p` selects the current projection's image-array layer;
- the returned `float4` contains four independent z-slice samples.

The loop accumulates `b` samples into `sum`. The flat coalesced-volume index is:

```text
plane = Nx * Ny
base(idx, idy, idz) = idz*plane + idy*Nx + idx
```

For the batch beginning at global projection zero, `first_burst` is true and the kernel assigns
`slices[base] = sum`. Every later burst performs `slices[base] += sum`. Because each work item owns a
unique `base`, no atomics are required.

### 5.3 Even/odd backprojection

Even/odd mode uses two kernels with the same argument ABI as singular backprojection. Both traverse
the same combined texture batch, but the even kernel starts at layer 0 and the odd kernel at layer 1;
each advances by two and writes only its own accumulator.

```c
kernel void
backproject_even(
    read_only image2d_array_t projections,
    global float4 *slices,
    constant float2 *angle_lut,
    const float axis,
    const uint burst,
    sampler_t sampler,
    const int slice_width,
    const int slice_height,
    const float2 x_region,
    const float2 y_region,
    const uint first_burst) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    if (idx >= slice_width || idy >= slice_height)
        return;
    const float volume_x = mad ((float) idx, x_region.y, x_region.x);
    const float volume_y = mad ((float) idy, y_region.y, y_region.x);
    float4 sum = 0.0f;
    for (uint proj = 0; proj < burst; proj += 2) {
        const float2 angle = angle_lut[proj];
        const float roh = axis + (volume_x * angle.x + volume_y * angle.y);
        sum += read_imagef(projections, sampler, (float4)(roh, idz + 0.5f, proj, 0));
    }
    const size_t plane = (size_t) slice_width * (size_t) slice_height;
    const size_t output_index = ((size_t) idz * plane) + ((size_t) idy * slice_width + idx);
    if (first_burst)
        slices[output_index] = sum;
    else
        slices[output_index] += sum;
}

kernel void
backproject_odd(
    read_only image2d_array_t projections,
    global float4 *slices,
    constant float2 *angle_lut,
    const float axis,
    const uint burst,
    sampler_t sampler,
    const int slice_width,
    const int slice_height,
    const float2 x_region,
    const float2 y_region,
    const uint first_burst) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    if (idx >= slice_width || idy >= slice_height)
        return;
    const float volume_x = mad ((float) idx, x_region.y, x_region.x);
    const float volume_y = mad ((float) idy, y_region.y, y_region.x);
    float4 sum = 0.0f;
    for (uint proj = 1; proj < burst; proj += 2) {
        const float2 angle = angle_lut[proj];
        const float roh = axis + (volume_x * angle.x + volume_y * angle.y);
        sum += read_imagef(projections, sampler, (float4)(roh, idz + 0.5f, proj, 0));
    }
    const size_t plane = (size_t) slice_width * (size_t) slice_height;
    const size_t output_index = ((size_t) idz * plane) + ((size_t) idy * slice_width + idx);
    if (first_burst)
        slices[output_index] = sum;
    else
        slices[output_index] += sum;
}
```

Because every combined batch begins at an even global projection, texture-layer parity equals global
projection parity. For a tail with an odd layer count, the even loop consumes one more layer than the
odd loop. `first_burst` independently initializes both coalesced accumulators. The host queues the
even kernel asynchronously and the odd kernel with a blocking profiler call; the in-order queue
prevents overlap or reordering between them.

### 5.4 Distribution: coalesced volume to planar output

```c
kernel void
distribute(
    global float4 *in,
    global float *out,
    const int slice_width,
    const int slice_height,
    const float normalization_factor) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    if (idx >= slice_width || idy >= slice_height)
        return;
    const size_t plane = (size_t) slice_width * (size_t) slice_height;
    const size_t base_index = (size_t) idy * slice_width + idx;
    float4 values = in[((size_t) idz * plane) + base_index] * normalization_factor;
    out[((size_t) (4 * idz + 0) * plane) + base_index] = values.x;
    out[((size_t) (4 * idz + 1) * plane) + base_index] = values.y;
    out[((size_t) (4 * idz + 2) * plane) + base_index] = values.z;
    out[((size_t) (4 * idz + 3) * plane) + base_index] = values.w;
}
```

`distribute` is retained unchanged for slice output and uses the same `(Nx,Ny,G)` launch. It reads:

```text
values = coalesced[idz*Nx*Ny + idy*Nx + idx] * normalization_factor
```

and writes the channels to four separate z planes:

```text
final[(4*idz + 0)*Nx*Ny + idy*Nx + idx] = values.x
final[(4*idz + 1)*Nx*Ny + idy*Nx + idx] = values.y
final[(4*idz + 2)*Nx*Ny + idy*Nx + idx] = values.z
final[(4*idz + 3)*Nx*Ny + idy*Nx + idx] = values.w
```

| Coalesced element | Channel | Final z plane |
|---|---|---:|
| `float4[idz][idy][idx]` | `.x` | `4*idz + 0` |
| same | `.y` | `4*idz + 1` |
| same | `.z` | `4*idz + 2` |
| same | `.w` | `4*idz + 3` |

The result is tightly packed in z-major plane order and ready for one-slice rectangular copies.
`normalization_factor` is `abs(overall_angle)/P` for singular reconstruction and `1.0` for even/odd
reconstruction.

Volume output uses a separate bounds-aware kernel and writes directly into the scheduler-owned
unpadded device buffer:

```c
kernel void
distribute_volume(
    global float4 *in,
    global float *out,
    const int slice_width,
    const int slice_height,
    const int num_slices,
    const float normalization_factor) {
    const int idx = get_global_id(0);
    const int idy = get_global_id(1);
    const int idz = get_global_id(2);
    if (idx >= slice_width || idy >= slice_height)
        return;
    const size_t plane = (size_t) slice_width * (size_t) slice_height;
    const size_t base_index = (size_t) idy * slice_width + idx;
    const int slice = 4 * idz;
    float4 values = in[((size_t) idz * plane) + base_index] * normalization_factor;
    if (slice + 0 < num_slices)
        out[((size_t) (slice + 0) * plane) + base_index] = values.x;
    if (slice + 1 < num_slices)
        out[((size_t) (slice + 1) * plane) + base_index] = values.y;
    if (slice + 2 < num_slices)
        out[((size_t) (slice + 2) * plane) + base_index] = values.z;
    if (slice + 3 < num_slices)
        out[((size_t) (slice + 3) * plane) + base_index] = values.w;
}
```

The launch still has depth `G`, but every channel checks `slice < Z`. Consequently, a five-slice
request writes planes 0–4 only even though the final work group carries reconstructed values for
internal planes 4–7. The blocking submission finishes all writes before the output is handed to a
downstream queue and before the source accumulator is released.

### 5.5 Worked z-padding example

Suppose:

```text
H = 20
z_start = 10
z_stop = 15       (exclusive)
z_step = 1
Z = 5
Z4 = 8
G = 2
```

Packing produces:

| Packed group | R | G | B | A | Emitted later? |
|---:|---:|---:|---:|---:|---|
| `g=0` | row 10 | row 11 | row 12 | row 13 | all four |
| `g=1` | row 14 | row 15 | row 16 | row 17 | only row 14 |

Rows 15–17 are inside the detector, so they are not zeroed. They are backprojected into padding
planes 5–7. Slice mode unpacks them into private padded storage but emits only planes 0–4; volume
mode suppresses their writes with `num_slices=5`. If a calculated padded row were outside `[0,H)`,
its channel would instead contain zero.

This behavior keeps RGBA packing and backprojection branch-free with respect to requested depth and
permits any positive `Z`.

### 5.6 Worked incomplete-burst example

For singular mode with `P=5` and `B=3`:

1. Projections 0, 1, and 2 fill ring slots 0, 1, and 2.
2. `accumulate` launches with depth 3 and fills texture layers 0–2.
3. Angles 0–2 are uploaded; `backproject(..., burst=3, first_burst=1)` assigns the volume.
4. Projections 3 and 4 overwrite ring slots 0 and 1.
5. `accumulate` launches with depth 2 and overwrites texture layers 0–1. Old layer 2 remains but is
   irrelevant.
6. Angles 3–4 are uploaded; `backproject(..., burst=2, first_burst=0)` adds the tail contribution and
   never reads layer 2.

No clearing of unused ring slots or image layers is required because every kernel's depth/loop bound
is the current `b`.

---

## 6. Final output

### 6.1 Slice stream

After `distribute`, `device_final_slices` is logical `float[Z4][Ny][Nx]`. For generated plane `k`, C
uses `clEnqueueCopyBufferRect` with:

```text
row_pitch   = Nx * sizeof(float)
slice_pitch = Ny * row_pitch

src_origin = {0 bytes, 0 rows, k slices}
dst_origin = {0 bytes, 0 rows, 0 slices}
region     = {row_pitch bytes, Ny rows, 1 slice}

source row pitch   = row_pitch
source slice pitch = slice_pitch
dest row pitch     = row_pitch
dest slice pitch   = 0              # legal because depth is one
```

OpenCL buffer-rectangle x origins and widths are expressed in bytes, whereas y and z are rows and
slices. The source z origin therefore selects `k*Nx*Ny*sizeof(float)` bytes without manually forming a
flat byte offset. The destination is a two-dimensional UFO output buffer and receives one complete
`Nx × Ny` plane.

`generated` starts at zero and increments after each queued copy. The termination test uses `Z`, not
`Z4`, which is the final guard preventing padding slices from escaping the task.

In even/odd mode, `generated / Z` selects the coalesced accumulator and `generated % Z` selects its
plane. This produces all even planes first and all odd planes second while reusing one private
planar buffer.

### 6.2 Device-resident volume stream

Volume mode does not allocate `device_final_slices` and does not enqueue a rectangular copy. The
scheduler creates an output with requisition `(Nx,Ny,Z)` before reduction begins, but its OpenCL
storage remains lazy because `process` never requests it. During `generate`, the task calls
`ufo_buffer_get_device_array`, launches `distribute_volume` directly into that memory, marks the
layout real, and returns the buffer downstream.

The coalesced `float4` buffer cannot itself be the UFO output: its z groups are padded and its
channel-interleaved layout is not the planar float layout expected by a three-dimensional FFT. The
single direct distribution pass is therefore required. Once that blocking pass finishes, the
selected coalesced accumulator has no remaining readers and is released. Singular mode generates
one output; even/odd mode generates even first and odd second.

---

## 7. Failure modes and operational guidance

### 7.1 Expected validation failures

- `num-projections` remains zero when `setup` runs;
- an x/y tuple is non-finite, has a non-positive explicit step, has `stop <= start`, or resolves
  beyond the supported OpenCL integer dimension;
- the z tuple has non-increasing integer-converted bounds or a non-positive integer-converted step;
- resolved z start is negative or resolved exclusive stop exceeds `H`;
- the resolved z region contains no slices;
- projection dimensions or a resolved x/y origin, step, or length changes after allocation;
- a byte-size calculation overflows;
- a buffer exceeds the effective per-allocation limit;
- estimated total storage exceeds global device memory;
- texture width, height, or layer count exceeds image limits;
- an OpenCL allocation or sampler creation fails.

Texture creation requires support for an RGBA `CL_HALF_FLOAT` image array. The kernels themselves
use `read_imagef` and `write_imagef`, which operate on float values and therefore do not require
OpenCL C half arithmetic or `cl_khr_fp16`. Lack of the image format surfaces at `clCreateImage`; the
task does not perform a separate supported-format query.

If fewer than `P` projections arrive, `generate` logs a warning and emits nothing. Supplying a stream
whose length differs from `P` is a pipeline configuration error.

### 7.2 Installed-kernel mismatch

Kernels are loaded at runtime, not embedded in the shared module. UFO searches the current directory
before its installed kernel directory; `UFO_KERNEL_PATH` is searched later. An older installed
`rgba-backproject.cl` can therefore be selected even when the plugin library was rebuilt.

After changing a kernel signature, either reinstall the kernel or run the pipeline from
`src/kernels/`, and compare the source with the installed copy. `CL_INVALID_ARG_INDEX` messages are a
strong indication that host code and kernel source have different ABIs.

### 7.3 Logging and profiling

- Use `G_MESSAGES_DEBUG=all` to see the `rgba_bp` debug messages for burst size, dimensions, region,
  allocation-derived counts, burst dispatch, and generated slices.
- Use `ufo-launch -t` to record UFO and OpenCL trace files.
- `accumulate`, the selected backprojection kernel(s), and the selected distribution kernel are
  submitted through the UFO profiler; transfer commands are not represented as kernel events.
- Run from `src/kernels/` or reinstall before trusting a profile after kernel edits.

### 7.4 Host and device ingestion

Host input remains host-resident and is uploaded directly into its ring slot. Device input is copied
device-to-device into the same ring. The ring deliberately decouples input-buffer lifetime from the
burst kernels: UFO may recycle an input buffer as soon as `process` returns, whereas the ring belongs
to the task until finalization.

The ring stores all `H` rows even when a small z region is requested. This keeps projection slots
contiguous and makes row selection entirely a packing-kernel concern, at the cost of
`batch_capacity*W*H` float storage and a full-projection ingestion copy.

---

## 8. Maintenance invariants

Preserve or deliberately revise all of these together:

1. **Kernel ABI:** C currently sets 6 `accumulate` arguments, 11 arguments for each retained
   backprojection kernel, 5 for `distribute`, and 6 for
   `distribute_volume`, in the exact source order.
2. **Ring stride:** one projection always occupies exactly `W*H` floats; a ring slot begins at
   `slot*W*H`.
3. **Texture layout:** width `W`, height `Z4/4`, array layers `batch_capacity`, RGBA half storage; channels map to
   consecutive selected z rows.
4. **Coordinate frames:** x/y regions provide volume coordinates around geometric origin;
   `center-position-x` is added only when producing the full-detector coordinate `rho`.
5. **Rectangular grids:** x and y resolve independently, and both internal volume buffers use
   `Nx*Ny` plane strides.
6. **Explicit strides:** flat-buffer indexing uses `projection_width`, `slice_width`, and
   `slice_height`, not an assumed padded global size.
7. **Batch initialization:** the first batch assigns every coalesced element; subsequent batches add.
   Removing `first_burst` requires deterministic zero initialization.
8. **In-order dependencies:** asynchronous packing and LUT transfer precede backprojection on the
   same command queue; even/odd mode queues the even kernel before the blocking odd kernel.
9. **Z padding:** internal sizes and kernel z work are based on `Z4`; slice termination and direct
   volume write bounds are based on `Z`.
10. **Precision:** projection texture values are half precision, but LUTs, interpolation results,
    accumulation, final storage, and UFO outputs are float.
11. **Normalization:** singular reconstruction applies `abs(overall-angle) / num-projections`
    exactly once in the selected distribution kernel; even/odd mode passes `1.0`.
12. **Resource ownership:** every retained context/kernel/object and every allocated buffer/scarray
    must retain its matching release in `finalize`; input and output UFO buffers must not be retained.
13. **Dimension stability:** projection dimensions and resolved x/y grids are fixed after the first
    requisition; changing them would require a new reduction or complete synchronized reallocation.
14. **Documentation snapshot:** whenever `rgba-backproject.cl`, buffer shapes, or function flow
    changes, update the source listing, formulas, tables, and examples here.
15. **Output representation:** slice mode must retain its historical two-dimensional ordering;
    volume mode must report `(Nx,Ny,Z)`, write only actual z planes, and never request a host array.
