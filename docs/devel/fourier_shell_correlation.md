# Fourier Shell Correlation

This document describes Fourier shell correlation (FSC) from two volumes reconstructed independently
from even and odd projection sets. It connects the scientific assumptions and frequency-shell
geometry to the implemented UFO graph, GPU reduction, and shared Python post-processing.

The benchmark layer provides correlation curves, frequency and count vectors, fixed and half-bit
criteria, a geometric diagnostic bound, and crossing interpolation. The permanent home and public
interface of the high-level Python layer remain undecided. Masking policy, local resolution, and
directional missing-wedge correction remain outside the implemented workflow.

---

## 1. Fourier Shell Correlation

FSC measures the agreement between two three-dimensional signals as a function of spatial
frequency. A 3-D Fourier transform decomposes each volume into complex coefficients. Coefficients
with similar radial spatial frequency are grouped into shells, and a normalized complex correlation
is computed independently in every shell. Agreement is normally high at low frequencies, where
object structure dominates, and falls as noise dominates at higher frequencies.

FSC is therefore a *spectral reproducibility* measure. It does not prove that a reconstruction is an
unbiased representation of the object. A reproducible reconstruction artifact, a shared calibration
error, or noise introduced by common processing can correlate and raise the curve.

### 1.1 Notation and coordinate conventions

The following notation is used throughout this document.

| Symbol | Meaning |
|---|---|
| $V$ | A real reconstructed volume. |
| $(V_1,V_2)$ | Two real volumes to be correlated. |
| $S$ | The common underlying signal. |
| $(N_1,N_2)$ | Noise and reconstruction error in the two volumes. |
| $(F,F_1,F_2)$ | Complex 3-D discrete Fourier transforms of the corresponding volumes. |
| $(N_z,N_y,N_x)$ | Numbers of samples along the array's $(z,y,x)$ axes. |
| $(d_x,d_y,d_z)$ | Voxel spacings along $(x,y,z)$ in one common distance unit. |
| $\mathbf{k}=(k_x,k_y,k_z)$ | A Fourier coordinate in cycles per configured distance unit. |
| $\rho(\mathbf{k})=\lVert\mathbf{k}\rVert_2$ | Radial spatial frequency in the same reciprocal unit. |
| $\Delta k$ | Radial shell spacing. |
| $k_{\max}$ | Configured frequency limit used to determine the number of retained bins. |
| $S_b$ | Set of Fourier samples assigned to shell/bin $b$. |
| $B$ | Number of frequency bins. |
| $C_b$ | Real cross-power sum in shell $b$. |
| $(P_{1,b},P_{2,b})$ | Auto-power sums in shell $b$. |
| $n_b$ | Number of Fourier samples in shell $b$. |
| $(m_b,M_b)$ | Minimum and maximum Fourier magnitudes across both spectra in shell $b$. |

NumPy volumes use shape $(N_z,N_y,N_x)$ and indexing `volume[z, y, x]`, with X fastest varying.
UFO stores a real volume as `dims=(Nx,Ny,Nz)`. Its complex-interleaved FFT buffer uses
`dims=(2*Nx,Ny,Nz)` because each coefficient contains a real and an imaginary float. The axis-specific
properties `voxel-size-x/y/z` refer to these same physical axes regardless of storage order.

When the distance unit is a micrometre, frequencies are in inverse micrometres. The normalized
benchmark instead uses unit voxel spacings, so frequencies are in cycles per reconstructed voxel.

For an unshifted DFT, the signed integer frequency associated with index $j$ of an axis of length
$N$ is

$$
q_N(j)=
\begin{cases}
j, & 0\leq j < \lceil N/2\rceil,\\
j-N, & \lceil N/2\rceil \leq j < N.
\end{cases}
$$

The physical frequency vector at array index $(j_z,j_y,j_x)$ is

$$
\mathbf{k} =
\left(
\frac{q_{N_x}(j_x)}{N_xd_x},
\frac{q_{N_y}(j_y)}{N_yd_y},
\frac{q_{N_z}(j_z)}{N_zd_z}
\right).
$$

**Main idea.** Shell membership must be computed from physical frequencies, not merely from integer
distance to the array centre. This makes non-cubic volumes and anisotropic voxel spacings valid
inputs.

An `fftshift` is not required. The same shells result if the implementation evaluates the signed
frequency associated with each coefficient in UFO's native, unshifted FFT layout.

The forward transform uses the convention

$$
F(\mathbf{k})=\sum_{\mathbf{x}}V(\mathbf{x})e^{-2\pi i\mathbf{k}\cdot\mathbf{x}},
$$

where $\mathbf{x}$ ranges over the sampled real-space grid. Both volumes use the same transform
convention and sampling grid.

### 1.2 Frequency shells and output vectors

The implementation uses uniformly spaced shell centres separated by $\Delta k$. A Fourier
coefficient is assigned to its nearest radial centre using

$$
b=\left\lfloor\frac{\rho(\mathbf{k})}{\Delta k}+\frac{1}{2}\right\rfloor,
\qquad
B=\left\lfloor\frac{k_{\max}}{\Delta k}\right\rfloor,
$$

and is retained only when $0\leq b<B$. Thus shell $b$ contains

$$
S_b=\left\{\mathbf{k}\;\middle|\;
\max(0,(b-\tfrac{1}{2})\Delta k)\leq\rho(\mathbf{k})
<(b+\tfrac{1}{2})\Delta k
\right\},
\qquad 0\leq b<B.
$$

Its reported centre and sample count are

$$
k_b=k_{\mathrm{bin},b}=b\Delta k,
\qquad
n_b=n_{\mathrm{shell},b}=|S_b|.
$$

The DC shell is centred at $k_0=0$ and covers $0\leq\rho<\Delta k/2$. Its reported centre is zero,
not the midpoint of that truncated radial interval. Coefficients exactly halfway between centres
are assigned to the higher bin.

The configured $k_{\max}$ determines $B$; membership is checked through $b<B$. Consequently the last
retained shell ends at $(B-\tfrac{1}{2})\Delta k$, and not every coefficient with
$\rho<k_{\max}$ is necessarily included. Section 2.2.1 explains how voxel spacings and volume shape
determine the default $\Delta k$ and $k_{\max}$.

**Main idea.** Every output element describes one physical-frequency interval; `n_shell` records how
much Fourier evidence contributed to it.

For spectra $F_1$ and $F_2$, the sufficient shell statistics are

$$
C_b = \sum_{\mathbf{k}\in S_b}
Re\!\left(F_1(\mathbf{k})\overline{F_2(\mathbf{k})}\right),
$$

$$
P_{1,b}=\sum_{\mathbf{k}\in S_b}|F_1(\mathbf{k})|^2,
\qquad
P_{2,b}=\sum_{\mathbf{k}\in S_b}|F_2(\mathbf{k})|^2.
$$

The FSC curve is

$$
FSC_b =
\frac{C_b}{\sqrt{P_{1,b}P_{2,b}}}.
$$

**Main idea.** FSC is the normalized dot product of the two complex spectra within a shell. The
normalization removes their absolute amplitude, while the numerator retains only the aligned common
component.

Bins with $n_b=0$ or non-positive power return `NaN`; valid FSC values are not clamped.
The UFO-result parser rejects non-finite sums or bin centres before normalization.

The central result vectors are

```
fsc      : float [B]    # correlation curve
k_bin    : float [B]    # reciprocal configured-distance unit
n_shell  : int   [B]    # Fourier-sample count
```

The shell kernel naturally produces floating-point accumulators. `k_bin` and `n_shell` are derived
from volume shape, voxel spacing, and the shell-membership convention. They are identical for pairs
with identical geometry, although the current kernel calculates counts while processing each pair.
The Python result calls the frequency vector `frequency`; saved artifacts retain the name `k_bin`.

### 1.3 Conditions behind the interpretation

The conventional measurement model is

$$
V_1=S+N_1,
\qquad
V_2=S+N_2.
$$

Here, both reconstructions contain the same signal $S$, while $N_1$ and $N_2$ contain their
respective noise and reconstruction errors. After the 3-D Fourier transform, the model at frequency
$\mathbf{k}$ becomes

$$
F_1(\mathbf{k})=F_S(\mathbf{k})+F_{N_1}(\mathbf{k}),
\qquad
F_2(\mathbf{k})=F_S(\mathbf{k})+F_{N_2}(\mathbf{k}).
$$

Each Fourier coefficient is a complex number. For two coefficients $A=a+ib$ and $B=c+id$,

$$
Re\!\left(A\overline{B}\right)=ac+bd.
$$

This is the ordinary dot product of the two coefficient vectors $(a,b)$ and $(c,d)$: it is positive
when their complex amplitudes and phases agree, negative when they oppose one another, and near zero
on average when they are unrelated. It is **not** a three-dimensional vector cross product. The
quantity $A\overline{B}$ is commonly called a *cross-spectrum* or *cross-power term*. In the FSC
numerator, only its real part is summed.

Substituting the signal-plus-noise model into one term of the FSC numerator gives

$$
\begin{aligned}
F_1\overline{F_2}
={}& |F_S|^2 \\
 &+F_S\overline{F_{N_2}} \\
 &+F_{N_1}\overline{F_S} \\
 &+F_{N_1}\overline{F_{N_2}}.
\end{aligned}
$$

The four lines have direct interpretations:

1. $|F_S|^2$ is signal compared with the same signal. It is non-negative and should accumulate
   consistently.
2. $F_S\overline{F_{N_2}}$ compares the common signal with noise from reconstruction 2.
3. $F_{N_1}\overline{F_S}$ compares noise from reconstruction 1 with the common signal.
4. $F_{N_1}\overline{F_{N_2}}$ compares the two noise realizations with each other.

For FSC to isolate the first term, the expected real contribution from each of the other three must
be approximately zero. For the noise-noise term in shell $S_b$, the condition needed by FSC is

$$
\mathbb{E}\!\left[
\sum_{\mathbf{k}\in S_b}
Re\!\left(
F_{N_1}(\mathbf{k})\overline{F_{N_2}(\mathbf{k})}
\right)
\right]\approx 0.
$$

The corresponding signal-noise conditions are

$$
\mathbb{E}\!\left[
\sum_{\mathbf{k}\in S_b}
Re\!\left(
F_S(\mathbf{k})\overline{F_{N_i}(\mathbf{k})}
\right)
\right]\approx 0,
\qquad i\in\{1,2\}.
$$

Statistical independence between zero-mean noise realizations is a sufficient way to obtain the
noise-noise condition. Strict independence is stronger than the equation actually requires: FSC
needs the two noise realizations to be *uncorrelated* in the numerator. In practical terms, knowing a
noise coefficient in reconstruction 1 should not help predict the amplitude or phase of the
corresponding noise coefficient in reconstruction 2. The signal and each noise realization must
likewise be uncorrelated for the two signal-noise terms to cancel.

For a finite shell the unrelated terms do not become exactly zero. Instead, some contributions are
positive and others negative, so their sum tends toward zero as the shell contains more samples. If
both reconstructions contain the same detector pattern or processing artifact, those contributions
can align repeatedly, survive the sum, and be mistaken for reproducible signal.

**Main idea.** The common signal produces a consistently positive term $|F_S|^2$. Independent,
zero-mean noise has no preferred agreement in complex amplitude and phase, so its signed
contributions cancel statistically across a shell. Correlated noise does not cancel and can raise
the FSC curve.

“Noise-independent” does not require different physical specimens. It means that the random error
in one reconstructed half must not predict the random error in the other. Alternating even and odd
projection indices is a practical way to create two projection half-sets while retaining angular
coverage in both. It is not a guarantee of complete independence: shared flats and darks, detector
patterns, alignment errors, ring artifacts, interpolation, reconstruction filters, masks, and a
jointly trained or applied denoiser can all create correlated errors.

The two reconstructed volumes must also represent the same object in the same coordinate system.
Differences in alignment, scale, normalization, support, or reconstruction geometry lower FSC even
when both reconstructions are individually useful. Conversely, a common bias can raise FSC.

Under an equal-noise statistical model, let $\lambda_b^2$ be signal power and
$\sigma_b^2$ be the noise power of either measurement in shell $b$. The spectral signal-to-noise
ratio and the deterministic expected-FSC proxy are related by

$$
SSNR_b=\frac{\lambda_b^2}{\sigma_b^2},
\qquad
EFSC_b=
\frac{SSNR_b}{1+SSNR_b}.
$$

Equivalently,

$$
SSNR_b=
\frac{EFSC_b}{1-EFSC_b}.
$$

**Main idea.** FSC approaches one where signal dominates and approaches zero where noise dominates.
This makes the curve a proxy for the frequency-dependent loss of usable information.

Resolution estimation chooses a correlation criterion $\tau_b$, finds an associated crossing
frequency $k_\ast$, and reports a real-space length such as

$$
d_\ast=\frac{1}{k_\ast}.
$$

**Main idea.** The FSC computation produces the spectral evidence; a threshold rule interprets where
that evidence ceases to be sufficient. The criterion and crossing policy belong to the Python layer.
The current utilities provide fixed $0.143$ and count-dependent half-bit curves and interpolate the
first crossing between adjacent finite bins where $FSC-\tau$ changes from positive to non-positive.
When there is no such crossing, they report no crossing in the sampled band. The geometric bound
in Section 3.7 is a separate consistency diagnostic.

### 1.4 FSC from even/odd reconstructions

FSC uses two separately reconstructed volumes whose noise is approximately independent. For
this project, the two volumes are reconstructed by `rgba-backproject` from alternating projection
indices using its `even_odd` operation mode.

#### 1.4.1 Algorithm

Given a projection stream and common reconstruction geometry:

1. Assign alternating projections to even and odd half-sets as early as practical in the data path.
2. Reconstruct $V_{\mathrm{even}}$ and $V_{\mathrm{odd}}$ independently, using identical output
   grids and geometry.
3. Compute $F_{\mathrm{even}}=\mathcal{F}_3(V_{\mathrm{even}})$ and
   $F_{\mathrm{odd}}=\mathcal{F}_3(V_{\mathrm{odd}})$.
4. Assign every Fourier coefficient to a physical-frequency shell.
5. Accumulate $C_b$, $P_{\mathrm{even},b}$, $P_{\mathrm{odd},b}$, $n_b$, and the magnitude extrema.
6. Transfer only the compact shell statistics to the host.
7. Normalize the sums, construct the comparison criteria, and assemble the typed Python result.

In pseudocode:

```
V_even, V_odd = reconstruct_alternating_projection_sets(projections)
F_even = fft3(V_even)
F_odd  = fft3(V_odd)

# GPU reduction emits seven compact rows; spectra remain on the device.
raw = shell_statistics(F_even, F_odd, geometry)
statistics = FSCShellStatistics.from_ufo(download_compact_result(raw))
return build_fsc_computation(statistics, geometry)
```

---

## 2. Computational Workflow

The UFO workflow keeps reconstructed volumes and spectra device-resident. Python constructs and
runs the graph, collects the compact shell statistics, and assembles the typed result. The
independent NumPy benchmark provides a CPU reference for checking the computation.

### 2.1 Shell-statistics core

FSC reduces two aligned complex spectra to shell statistics and a normalized correlation curve:

```
complex spectrum 1 ─┐
                    ├─ shell statistics ─ normalization ─ compact vectors
complex spectrum 2 ─┘
```

The device-side core implemented by `fsc-core`:

1. accepts consecutive pairs of equal-shape, complex-interleaved 3-D spectra;
2. maps each Fourier coefficient to a physical-frequency bin;
3. accumulates `cross_sum`, `power_1_sum`, `power_2_sum`, and `count` per bin, and reduces
   minimum and maximum magnitudes across both spectra;
4. emits those six quantities plus the bin centres in one compact seven-row buffer;
5. resets safely for the next same-shaped pair.

Normalization and comparison-curve calculations operate on only $B$ elements in Python. Returning
raw shell statistics exposes normalization and counting errors during validation without transferring
a full spectrum. The task computes agreement; it cannot establish that its inputs have independent
noise. That condition must be considered when forming the reconstruction half-sets.

The central primitive is therefore **shell-wise reduction of two 3-D complex spectra**. The vector
result is carried as a small two-dimensional UFO buffer with one row per statistic. The hard part is
changing millions of irregularly grouped 3-D samples into a few hundred shell accumulators
efficiently and reproducibly, not transporting the compact result.

The existing `power-spectrum`, `correlate-stacks`, and `cross-correlate` tasks perform different
2-D power or correlation operations; they do not replace the 3-D shell reduction in `fsc-core`.

### 2.2 End-to-end FSC graph

```
projection stream
      │
      ▼
rgba-backproject
operation-mode=even_odd
output-mode=volume
      │
      ▼
device stream [V_even, V_odd]
      │
      ▼
fft(dimensions=3, auto-zeropadding=false)
      │
      ▼
device stream [F_even, F_odd]
      │
      ▼
fsc-core ─ compact shell statistics ─ shared Python post-processing ─ FSCComputation
```

`rgba-backproject` emits the even volume first and the odd volume second; one processor-mode FFT task
instance preserves that stream order and produces two consecutive complex-interleaved spectra. No
branch, demultiplexer, CPU `stack`, or full-volume host transfer is required. Because pairing is
stateful, `fsc-core` rejects graph copying and the FSC benchmark disables expansion and selects
one GPU.

The UFO reduction task is `fsc-core`. For the sequential stream above, it recognizes consecutive
pairs, preserves the first spectrum on the device until the second arrives, reduces the pair into
shell statistics, and emits a compact result. Python performs the inexpensive normalization and
constructs the typed public result.

A common nonzero FFT scaling factor multiplies both the cross sum and each power sum by its squared
magnitude, which cancels in $C_b/\sqrt{P_{1,b}P_{2,b}}$. Raw statistics retain the FFT's scaling.
The graph uses exact input dimensions with automatic zero-padding disabled, preserving its sampling
grid and reproducible shell geometry.

The FSC computation is implemented end to end. The standalone benchmark in
`analytical_methods/benchmarks/fsc` composes the graph and provides Python post-processing. The future
home and public interface of the high-level Python layer remain undecided.

#### 2.2.1 `fsc-core` interface and bin policy

The GPU reductor has one 3-D complex-interleaved input stream. It pairs consecutive buffers as
$(F_1,F_2)$ and emits one result for every complete pair. An incomplete final pair produces a warning
and no result. All spectra processed by one task instance must have the shape established by its
first input. The task rejects graph copying because distributing this stateful stream over multiple
instances could separate pair members.

The required properties `voxel-size-x`, `voxel-size-y`, and `voxel-size-z` specify positive physical
spacings $(d_x,d_y,d_z)$. `shell-width` specifies an explicit positive $\Delta k$, or zero selects

$$
\Delta k_x=\frac{1}{N_xd_x},
\qquad
\Delta k_y=\frac{1}{N_yd_y},
\qquad
\Delta k_z=\frac{1}{N_zd_z},
$$

$$
\Delta k=\max(\Delta k_x,\Delta k_y,\Delta k_z).
$$

`max-frequency` specifies an explicit positive exclusive radial limit, or zero selects the smallest
axial Nyquist frequency:

$$
k_{\max}=\min\left(
\frac{1}{2d_x},
\frac{1}{2d_y},
\frac{1}{2d_z}
\right).
$$

The number of bins is

$$
B=\left\lfloor\frac{k_{\max}}{\Delta k}\right\rfloor.
$$

A Fourier voxel at radial physical frequency $\rho$ is assigned to its nearest shell centre,

$$
b=\left\lfloor\frac{\rho}{\Delta k}+\frac{1}{2}\right\rfloor,
$$

only when $b<B$. The centre reported for that shell is $k_b=b\Delta k$. For cubic isotropic data,
this reduces to rounded integer-radius shells expressed in physical units. The C task passes the
resolved shell width to the kernel as float32 and uses a small double-precision tolerance when
calculating the bin count; the NumPy geometry resolver follows the same convention.

The task emits a 2-D UFO requisition `dims=(B,7)`, seen by NumPy as `(7,B)`. Its float32 rows are, in
order, $C_b$, $P_{1,b}$, $P_{2,b}$, $n_b$, $k_b$, $m_b$, and $M_b$. Here $m_b$ and $M_b$ are the
minimum and maximum Fourier magnitudes across both input spectra in shell $b$. Counts are accumulated
as integers within each work-group, then converted to float32 partial records for the final reduction.
Python validates their integrality, converts them to `int64`, and computes
$C_b/\sqrt{P_{1,b}P_{2,b}}$ using float64 intermediates. The two extrema
are sufficient to construct the geometric lower bound without transferring either spectrum.

#### 2.2.2 Normalized benchmark configuration

The standalone runner in `analytical_methods/benchmarks/fsc` uses normalized frequency rather than
calibrated physical frequency. It configures the three `fsc-core` voxel sizes as

$$
d_x=d_y=d_z=1,
$$

where one distance unit means one reconstructed voxel. Consequently, the reported $k_b$ values are
in cycles per reconstructed voxel. This does not claim that a voxel is one micrometre or any other
physical length.

The reconstruction regions determine the volume shape $(N_z,N_y,N_x)$. Their meanings and the other
backprojection properties are documented in
[`rgba-backproject.md`](rgba-backproject.md). For this benchmark, all three region steps are exactly
`1.0`; their extents may differ. Cubic volumes are therefore not required. FSC requires only
that the even and odd volumes are aligned, have the same shape, and describe the same sampling grid.

For a possibly non-cubic normalized volume, the axial FFT increments are

$$
\Delta k_x=\frac{1}{N_x},
\qquad
\Delta k_y=\frac{1}{N_y},
\qquad
\Delta k_z=\frac{1}{N_z}.
$$

The `fsc-core` properties used by the benchmark have the following scientific roles:

| Configuration property | Benchmark value | Scientific role |
|---|---:|---|
| `voxel_size_x` | `1.0` | Makes the X Fourier coordinate cycles per reconstructed voxel. |
| `voxel_size_y` | `1.0` | Makes the Y Fourier coordinate cycles per reconstructed voxel. |
| `voxel_size_z` | `1.0` | Makes the Z Fourier coordinate cycles per reconstructed voxel. |
| `shell_width` | `0.0` by default | Selects the coarsest axial increment, $\Delta k=\max(\Delta k_x,\Delta k_y,\Delta k_z)$. A positive value explicitly selects a normalized radial spacing if the resulting shell configuration is valid. |
| `max_frequency` | `0.5` | Sets the exclusive radial boundary to the isotropic Nyquist frequency. |

Each Fourier coefficient retains its axis-specific coordinate. The kernel calculates

$$
\rho=\sqrt{k_x^2+k_y^2+k_z^2}
$$

and assigns the coefficient to

$$
b=\left\lfloor\frac{\rho}{\Delta k}+\frac{1}{2}\right\rfloor.
$$

With the exclusive normalized cutoff, the number of emitted bins and their centres are

$$
B=\left\lfloor\frac{0.5}{\Delta k}\right\rfloor,
\qquad
k_b=b\Delta k,
\qquad 0\leq b<B.
$$

Thus every emitted centre is below `0.5`, while the plot spans the full interval from `0` to `0.5`.
The value `0.5 cycles/voxel` is the per-axis Nyquist frequency: it represents a sinusoid with a full
period of two reconstructed voxels. In three dimensions, Cartesian corner coefficients can have a
radial magnitude above `0.5`, but shells above this boundary are not represented equally in every
direction.

The task itself remains unit-agnostic. Another caller may supply physical spacings such as micrometres
per voxel; in that case $k_b$ is expressed in inverse micrometres and the physical isotropic cutoff is
the smallest axial Nyquist frequency rather than the literal number `0.5`.

### 2.3 `rgba-backproject` output representation

The `rgba-backproject` output representation is configurable as

```
output-mode = slices | volume
```

without changing the parity mathematics or the existing `stack` task:

- `slices` preserves the current public behavior and compatibility;
- `volume` emits one device-resident 3-D buffer in singular reconstruction mode;
- `volume` emits two sequential device-resident 3-D buffers in `even_odd` mode, even first and odd
  second.

In volume mode, the output requisition is `(Nx,Ny,Z)`. During `generate`, the task obtains the
scheduler-owned output through `ufo_buffer_get_device_array`, runs `distribute_volume` directly into
that allocation, sets `UFO_BUFFER_LAYOUT_REAL`, and returns it downstream. It never requests the
output host array. The distribution kernel converts the internal padded `float4` z layout into the
unpadded planar float layout required by the FFT, after which the selected internal accumulator is
released. This is one necessary device-to-device layout conversion, not a device-to-host-to-device
round trip.

This fits UFO's single output stream and three-dimensional requisition limit; it does not require a
4-D buffer or named output ports. [`fft`](../../src/ufo-fft-task.c) with `dimensions=3` requests a
three-dimensional real input, obtains its device array, and emits a complex-interleaved spectrum, so
the two task contracts are directly compatible.

The FSC graph connects volume output directly to the FFT. The existing CPU
[`stack`](../../src/ufo-stack-task.c) task is unnecessary on this path.

This connection was validated on 2026-09-01 for even/odd reconstruction. Slice and volume outputs
agreed exactly for the test data, including parity order and a z depth
that exercised RGBA padding. The direct device graph
`rgba-backproject → fft dimensions=3 → null` also completed successfully without an intervening
`stack` task. Isolated numerical 3-D FFT checks at $8^3$ and $32^3$ also agreed with NumPy. This is
functional and interface validation; runtime and peak memory must be measured separately for the
actual dataset and graph.

### 2.4 Python responsibilities and typed result

The current high-level implementation lives in `analytical_methods/benchmarks/fsc`. Its UFO runner
constructs the graph, provides reconstruction geometry and shell settings, selects one GPU, disables
scheduler expansion, and collects compact results through `Ufo.OutputTask`. The NumPy runner provides
an independent CPU calculation for comparison.

Both runners use the shared `fsc_utils.py` module. It parses or receives `FSCShellStatistics`,
normalizes the shell sums, constructs the comparison curves, and plots the result. The module also
provides curve-crossing interpolation; the NumPy runner uses it to report fixed and half-bit
crossings and geometric-bound diagnostics. NumPy is sufficient for this compact host computation.

The shared result has the following fields:

```
FSCComputation
    frequency         : float32[B]   # reciprocal configured-distance unit
    fsc               : float64[B]
    n_shell           : int64[B]
    fixed_threshold   : float64[B]
    half_bit_threshold: float64[B]
    geometric_bound   : float64[B]
    geometry          : ShellGeometry
```

The `frequency` vector is called `k_bin` in `FSCShellStatistics` and the saved NPZ artifact.
`ShellGeometry` holds `delta_k_xyz`, `shell_width`, `max_frequency`, and `number_of_bins`. Run metadata
such as reconstruction shape, configuration, device information, and timings is recorded separately;
there are no method or directional-axis fields in `FSCComputation`.

UFO buffers contain float32 storage. `fsc-core` therefore transports its seven output rows as float32,
including the shell counts. Counts are accumulated as integers locally and converted into float32
partial records before the final reduction. Counts remain exactly representable for the target
$512^3$ volumes with the current shell policy. Python validates that the received values are integral
and exposes `n_shell` as `int64`; this conversion cannot recover precision already lost during
transport. Larger volumes or shells with more than $2^{24}$ samples require revisiting that contract.

The permanent home and public API of this Python layer are still to be selected. The benchmark
utilities demonstrate the required functionality without committing that integration to a particular
repository or command-line interface.

### 2.5 Runtime and memory considerations

With float32 real storage and a full complex-interleaved float32 spectrum:

| Object | Storage |
|---|---:|
| One $512^3$ real volume | 512 MiB |
| Two $512^3$ real reconstruction volumes | 1 GiB before any RGBA depth padding |
| One full $512^3$ complex spectrum | 1 GiB |
| Task-owned copy of the first spectrum plus the second input spectrum | 2 GiB |
| Local shell table per work-group | $24B$ bytes |
| Partial shell records for $G$ work-groups | $32\times G\times B$ bytes |
| Compact seven-row output | $28B$ bytes |

For the automatic unit-spacing policy at $512^3$, $B=256$. Each local shell table then occupies
6 KiB, and one compact result occupies 7 KiB. These are storage sizes, not measured peak memory.

The workflow performs two full-volume 3-D FFTs after reconstruction. Peak device memory also depends
on projection batches, RGBA reconstruction accumulators, FFT workspace, scheduler buffering, and
overlap between pipeline stages. The volume-output path releases each internal reconstruction
accumulator after distributing that volume, while `fsc-core` retains its own device copy of the first
spectrum until its partner can be reduced. Buffer allocations may remain available for reuse.

These lifetimes mean that adding the table entries does not establish an exact pipeline peak.
Runtime and peak-memory measurements must use the actual graph and reconstruction shape. The design
keeps volumes and spectra on the GPU, calculates shell membership without a full shell-map volume,
and transfers only the compact output to Python. Full spectra are retained; the implications of a
possible Hermitian half-spectrum optimization are described at the end of Section 3.9.

---

## 3. `fsc-core` Implementation

This section connects the scientific definitions in Section 1 to the concrete UFO task and OpenCL
kernels. It starts with the input stream, writes the computation as an ordinary sequential Python
loop, and then shows how the same loop is divided safely among GPU workers.
The authoritative sources are [`ufo-fsc-core-task.c`](../../src/ufo-fsc-core-task.c) and
[`fsc-core.cl`](../../src/kernels/fsc-core.cl); the excerpts below explain their current behavior.

At a high level, the implementation performs this reduction:

```
first complex spectrum F1 ── device copy ──┐
                                           ├─ partial shell histograms ─ final shell sums
second complex spectrum F2 ────────────────┘                              │
                                                                          ▼
                                             [C, P1, P2, n, k, min, max] on device
                                                                          │
                                                        compact transfer  ▼
                                                               Python normalization
                                                                          │
                                                                          ▼
                                                                 FSCComputation
```

The GPU does not calculate the final division. It produces the sufficient statistics
$(C_b,P_{1,b},P_{2,b},n_b,k_b,m_b,M_b)$ for every shell. Python then calculates
$FSC_b=C_b/\sqrt{P_{1,b}P_{2,b}}$. This division concerns only a few hundred values, so moving it to
Python does not require downloading either reconstructed volume or either full spectrum.

Here and throughout the implementation, a **partial** shell statistic means a subtotal calculated
from only the Fourier voxels assigned to one work-group. It is not an approximation, a fraction of a
complex number, an incomplete spectrum pair, or an unfinished output. Every voxel whose shell index
passes the radial cutoff is included in one work-group's subtotal, and the second kernel adds all
group subtotals to obtain the complete shell statistics.

### 3.1 Input and pairing lifecycle

`fsc-core` is a one-input GPU reductor. The input is a stream, not two named input ports:

```
F1, F2, F1, F2, ...
```

Consecutive buffers form pairs. This fits the FSC pipeline because `rgba-backproject` emits the
even volume followed by the odd volume, and the 3-D `fft` task preserves that order.

Each input must be a three-dimensional `UfoBuffer` with
`UFO_BUFFER_LAYOUT_COMPLEX_INTERLEAVED`. UFO requisitions count 32-bit float values, whereas the
kernel reads `float2` complex numbers. A logical spectrum of shape $(N_z,N_y,N_x)$ therefore has the
UFO requisition

```
dims[0] = 2 * Nx     # real and imaginary float for every x coefficient
dims[1] = Ny
dims[2] = Nz
```

For example, a logical $512^3$ complex spectrum has requisition
$(1024,512,512)$. The kernel casts the underlying float buffer to `float2 *`, so its logical `nx` is
`dims[0] / 2` and it sees exactly $512^3$ complex coefficients.

Before processing the first buffer, `get_requisition`:

1. rejects a real layout, a non-3-D buffer, or an invalid interleaved width;
2. requires positive finite voxel sizes;
3. resolves $\Delta k$, $k_{\max}$, and $B$ using Section 2.2.1;
4. records the first spectrum's shape for the lifetime of the task instance;
5. configures the work-group geometry and partial buffer;
6. requests a compact output with UFO dimensions $(B,5)$.

UFO releases an input buffer back to its producer as soon as `process` returns. The task must
therefore not save the pointer to the first input. Instead, the first call obtains a task-owned
device allocation and enqueues a device-to-device copy:

```
if (!priv->have_first) {
    if (priv->first_spectrum == NULL)
        priv->first_spectrum = ufo_buffer_dup (inputs[0]);

    first_mem = ufo_buffer_get_device_array (priv->first_spectrum, queue);
    UFO_RESOURCES_CHECK_CLERR (clEnqueueCopyBuffer (
        queue, input_mem, first_mem, 0, 0, ufo_buffer_get_size (inputs[0]),
        0, NULL, NULL));
    priv->have_first = TRUE;
    return TRUE;
}
```

`ufo_buffer_dup` creates a buffer with the same requisition; it does not copy the data. The explicit
`clEnqueueCopyBuffer` performs the copy entirely on the device. Returning `TRUE` tells the UFO
reductor scheduler to consume another input.

On the second call, `process` validates the shape again, runs both kernels, sets `have_first` back to
`FALSE`, and returns `FALSE`. For a reductor, that return value tells the scheduler to enter the
`generate` phase. `generate` returns `TRUE` exactly once to publish the compact result, then returns
`FALSE` so processing can continue with the next pair.

A differently shaped spectrum is discarded with a warning and resets the current pair. A real or
non-3-D input fails during requisition. If the stream ends after only $F_1$, `generate` warns about
the incomplete pair and emits nothing.

Pairing is stateful, so graph expansion could send $F_1$ and $F_2$ to different copies of the task.
The task's copy method consequently returns an error instructing the caller to disable expansion and
select one GPU.

### 3.2 The same calculation as a sequential Python loop

Ignoring parallel execution for a moment, the essential calculation can be written as:

```
cross_sum = np.zeros(B, dtype=np.float64)
power_1_sum = np.zeros(B, dtype=np.float64)
power_2_sum = np.zeros(B, dtype=np.float64)
n_shell = np.zeros(B, dtype=np.int64)
minimum_magnitude = np.full(B, np.inf)
maximum_magnitude = np.zeros(B)

# Inside the loop these are running sums over the voxels visited so far.
# After the loop they are the complete sums over all Fourier voxels.

for z in range(nz):
    qz = z if z <= nz // 2 else z - nz
    kz = qz * delta_kz

    for y in range(ny):
        qy = y if y <= ny // 2 else y - ny
        ky = qy * delta_ky

        for x in range(nx):
            qx = x if x <= nx // 2 else x - nx
            kx = qx * delta_kx

            radius = np.sqrt(kx * kx + ky * ky + kz * kz)
            b = int(np.floor(radius / shell_width + 0.5))

            if b < B:
                a = first[z, y, x]
                c = second[z, y, x]

                cross_sum[b] += a.real * c.real + a.imag * c.imag
                power_1_sum[b] += a.real * a.real + a.imag * a.imag
                power_2_sum[b] += c.real * c.real + c.imag * c.imag
                n_shell[b] += 1
                minimum_magnitude[b] = min(
                    minimum_magnitude[b], abs(a), abs(c))
                maximum_magnitude[b] = max(
                    maximum_magnitude[b], abs(a), abs(c))

# All voxels have now been visited, so these arrays contain final shell sums.
minimum_magnitude[n_shell == 0] = np.nan
maximum_magnitude[n_shell == 0] = np.nan
denominator = np.sqrt(power_1_sum * power_2_sum)
fsc = np.full(B, np.nan)
valid = (n_shell > 0) & (power_1_sum > 0) & (power_2_sum > 0)
fsc[valid] = cross_sum[valid] / denominator[valid]
```

The OpenCL implementation performs the same operations. Its main complication is that many GPU
workers execute loop iterations concurrently and may try to add to the same shell at the same time.

While the sequential loop is running, `cross_sum[b]` is a partial sum in the ordinary mathematical
sense: it contains contributions only from voxels visited so far. When the loop finishes, the same
array contains the final sum. There is only one accumulator, so this version does not need a
separate intermediate `partials` array.

The GPU organization can be represented more directly by dividing the voxel indices into the same
sets of work handled by its work-groups:

```
partial_cross = np.zeros((num_groups, B), dtype=np.float64)
partial_power_1 = np.zeros((num_groups, B), dtype=np.float64)
partial_power_2 = np.zeros((num_groups, B), dtype=np.float64)
partial_count = np.zeros((num_groups, B), dtype=np.int64)
partial_minimum = np.full((num_groups, B), np.inf)
partial_maximum = np.zeros((num_groups, B), dtype=np.float64)

# Stage 1: every group calculates a complete subtotal for its assigned voxels.
for group, indices in enumerate(indices_per_group):
    for index in indices:
        b, cross, power_1, power_2, magnitude_1, magnitude_2 = \
            voxel_contribution(index)

        if b < B:
            partial_cross[group, b] += cross
            partial_power_1[group, b] += power_1
            partial_power_2[group, b] += power_2
            partial_count[group, b] += 1
            partial_minimum[group, b] = min(
                partial_minimum[group, b], magnitude_1, magnitude_2)
            partial_maximum[group, b] = max(
                partial_maximum[group, b], magnitude_1, magnitude_2)

# Stage 2: combine all group subtotals into the complete shell statistics.
cross_sum = partial_cross.sum(axis=0)
power_1_sum = partial_power_1.sum(axis=0)
power_2_sum = partial_power_2.sum(axis=0)
n_shell = partial_count.sum(axis=0)
minimum_magnitude = partial_minimum.min(axis=0)
maximum_magnitude = partial_maximum.max(axis=0)
minimum_magnitude[n_shell == 0] = np.nan
maximum_magnitude[n_shell == 0] = np.nan
```

This second example is conceptual: `indices_per_group` represents the strided index assignment
described in Section 3.4.4, and `voxel_contribution` represents the coordinate, shell, cross-term,
and power calculations from the first example.

Let $W_g$ be the Fourier voxels assigned to work-group $g$. For shell $S_b$, the group's partial
cross sum is

$$
C_{g,b}=\sum_{\mathbf{k}\in S_b\cap W_g}
Re(F_1(\mathbf{k})\overline{F_2(\mathbf{k})}).
$$

The corresponding partial power sums, count, and magnitude extrema are

$$
P_{1,g,b}=\sum_{\mathbf{k}\in S_b\cap W_g}|F_1(\mathbf{k})|^2,
\qquad
P_{2,g,b}=\sum_{\mathbf{k}\in S_b\cap W_g}|F_2(\mathbf{k})|^2,
$$

$$
n_{g,b}=|S_b\cap W_g|.
$$

$$
m_{g,b}=\min_{\mathbf{k}\in S_b\cap W_g}
\left(|F_1(\mathbf{k})|,|F_2(\mathbf{k})|\right),
$$

$$
M_{g,b}=\max_{\mathbf{k}\in S_b\cap W_g}
\left(|F_1(\mathbf{k})|,|F_2(\mathbf{k})|\right).
$$

The sets $W_g$ divide the full volume without overlap, so the second stage recovers the complete
scientific statistics:

$$
C_b=\sum_g C_{g,b},
\qquad
P_{1,b}=\sum_g P_{1,g,b},
\qquad
P_{2,b}=\sum_g P_{2,g,b},
\qquad
n_b=\sum_g n_{g,b}.
$$

The extrema combine differently from the additive quantities:

$$
m_b=\min_g m_{g,b},
\qquad
M_b=\max_g M_{g,b}.
$$

Thus, **partial means subtotal over one group's subset; reduction means combining those subtotals**.
This is the same arithmetic as the first Python loop, with an extra intermediate dimension for the
work-group.

The kernel chooses the positive representative for an even-length axis's Nyquist index, whereas
`numpy.fft.fftfreq` conventionally chooses the negative representative. Since `fsc-core` uses only
the squared radius, the sign of this one coordinate does not change its shell.

### 3.3 OpenCL concepts used by the reduction

An OpenCL **work-item** is one execution of a kernel body. It is analogous to one lightweight loop
worker. Every work-item receives a unique **global ID**.

A **work-group** is a fixed-size team of work-items. Members of a group:

- have different local IDs from $0$ to `local_size - 1`;
- can share a small, fast local-memory allocation;
- can synchronize with each other using `barrier`.

Different work-groups cannot use a barrier to synchronize with one another. They may execute in any
order or simultaneously.

The important OpenCL address spaces here are:

| Address space | Visibility | Use in `fsc-core` |
|---|---|---|
| `global` | All work-items and later kernels | $F_1$, $F_2$, the partial histograms, and final output |
| `local` | Work-items in one work-group only | One temporary six-vector shell histogram per group |
| Private | One work-item only | Coordinates, coefficients, and individual contributions |

The C task selects

```
local_size = min(256, device maximum, kernel maximum)
```

and rounds that value down to the kernel's preferred hardware multiple when possible. It then
selects

$$
G=\min\left(
4\times\text{compute units},
\left\lceil\frac{N_xN_yN_z}{\text{local size}}\right\rceil
\right)
$$

work-groups, with at least one group. The first kernel consequently launches
$G\times\text{local size}$ work-items.

Every group needs six local arrays of $B$ unsigned 32-bit words:

```
shells[0            : B]     cross sums C
shells[B            : 2 * B] first-spectrum powers P1
shells[2 * B        : 3 * B] second-spectrum powers P2
shells[3 * B        : 4 * B] integer counts n
shells[4 * B        : 5 * B] minimum magnitudes m
shells[5 * B        : 6 * B] maximum magnitudes M
```

The allocation therefore requires $6B\times4=24B$ bytes of local memory. The kernel addresses it as
one flat `uint` array. The task rejects
the configuration if this allocation plus the kernel's static local memory exceeds the device
limit.

Each group ultimately writes $B$ `float8` partial records to global memory. Six lanes carry data and
two are padding. With $G$ groups, the partial buffer is therefore $G\times B\times32$ bytes. This
buffer is compact compared with either full spectrum.

### 3.4 First kernel: one partial histogram per work-group

The first kernel is `fsc_accumulate_partials`. The following annotated blocks contain all of its
executable code, including its helper functions.

#### 3.4.1 Portable float atomics in local memory

```
inline void
atomic_add_float_local (volatile __local uint *address, float value)
{
    uint previous = *address;
    uint expected;

    do {
        expected = previous;
        previous = atomic_cmpxchg (address, expected,
                                   as_uint (as_float (expected) + value));
    } while (previous != expected);
}

inline void
atomic_min_float_local (volatile __local uint *address, float value)
{
    const uint candidate = as_uint (value);
    uint previous = *address;
    uint expected;

    do {
        expected = previous;

        if (candidate >= expected)
            return;

        previous = atomic_cmpxchg (address, expected, candidate);
    } while (previous != expected);
}

inline void
atomic_max_float_local (volatile __local uint *address, float value)
{
    const uint candidate = as_uint (value);
    uint previous = *address;
    uint expected;

    do {
        expected = previous;

        if (candidate <= expected)
            return;

        previous = atomic_cmpxchg (address, expected, candidate);
    } while (previous != expected);
}
```

OpenCL 1.2 provides atomic compare-and-exchange for integers, but it does not provide a portable
`atomic_add` for floats. The helper therefore uses the same 32 bits in two ways:

- `as_float(expected)` interprets the current bits as a float;
- it adds `value` as a floating-point operation;
- `as_uint(...)` reinterprets the new float's bits as an unsigned integer;
- `atomic_cmpxchg` replaces the stored bits only if nobody changed them after they were read.

`as_float` and `as_uint` are bit reinterpretations, not numerical conversions. For example, the bit
pattern representing `1.5f` is carried through the integer atomic operation unchanged.

Suppose two work-items both read a shell sum of $10$. Worker A wants to add $2$, while worker B wants
to add $3$:

1. both calculate proposed values from the expected value $10$;
2. A successfully changes $10$ to $12$;
3. B's comparison against $10$ fails and returns the current value $12$;
4. B retries, calculates $12+3$, and successfully stores $15$.

Without the compare-and-exchange loop, both workers could write based on $10$, losing one
contribution. The order of successful additions remains nondeterministic, so the last few
floating-point bits may vary between devices or runs.

The minimum and maximum helpers use the same retry pattern, but compare non-negative float bit
patterns instead of adding them. IEEE-754 bit ordering agrees with numerical ordering for finite
non-negative values, which includes Fourier magnitudes. The minimum region begins at positive
infinity and the maximum region begins at zero.

#### 3.4.2 Kernel arguments and worker identities

```
kernel void
fsc_accumulate_partials (global const float2 *first,
                         global const float2 *second,
                         global float8 *partials,
                         local uint *shells,
                         uint nx,
                         uint ny,
                         uint nz,
                         float delta_kx,
                         float delta_ky,
                         float delta_kz,
                         float shell_width,
                         uint num_bins,
                         ulong num_voxels)
{
    const size_t local_id = get_local_id (0);
    const size_t local_size = get_local_size (0);
    const size_t group_id = get_group_id (0);
    const size_t global_id = get_global_id (0);
    const size_t global_size = get_global_size (0);
```

`first` and `second` are the two full spectra in device global memory. One `float2` stores the real
and imaginary components of one coefficient. `partials` is the global $G\times B$ intermediate
array. `shells` is a separate local-memory allocation for every work-group.

Only dimension zero of the OpenCL launch is used. This does not make the data one-dimensional:
`global_id` selects work from the flattened 3-D spectrum, and the kernel reconstructs $(x,y,z)$
below.

#### 3.4.3 Cooperative local-memory initialization

```
    for (size_t item = local_id; item < 6 * num_bins; item += local_size) {
        const bool is_minimum = item >= 4 * num_bins && item < 5 * num_bins;
        shells[item] = is_minimum ? as_uint (INFINITY) : 0;
    }

    barrier (CLK_LOCAL_MEM_FENCE);
```

The local shell table is scratch memory and may initially contain arbitrary bits. Its initialization
is shared: local worker $0$ initializes entries $0,L,2L,\ldots$, worker $1$ initializes
$1,L+1,2L+1,\ldots$, and so on for local size $L$. Additive values, counts, and maxima start at zero;
the minimum region starts at positive infinity so the first observed magnitude always replaces it.

The barrier means: *every work-item in this work-group must finish its local-memory writes before any
work-item in the group continues*. Without it, one worker could add a contribution while another
worker was still clearing the same shell, erasing the contribution. This barrier says nothing about
other work-groups; each has its own independent `shells` allocation. Every work-item in the group
must encounter the barrier, which is why it is outside the initialization loop.

Integer zero has the same all-zero bit pattern as floating-point `0.0f`. The `as_uint` call stores
the bit pattern of floating-point infinity without numerically converting it.

#### 3.4.4 Strided traversal and shell assignment

```
    for (size_t index = global_id; index < num_voxels; index += global_size) {
        const uint x = (uint) (index % nx);
        const size_t yz = index / nx;
        const uint y = (uint) (yz % ny);
        const uint z = (uint) (yz / ny);
        const int qx = x <= nx / 2 ? (int) x : (int) x - (int) nx;
        const int qy = y <= ny / 2 ? (int) y : (int) y - (int) ny;
        const int qz = z <= nz / 2 ? (int) z : (int) z - (int) nz;
        const float kx = (float) qx * delta_kx;
        const float ky = (float) qy * delta_ky;
        const float kz = (float) qz * delta_kz;
        const float radius = sqrt (kx * kx + ky * ky + kz * kz);
        const float shell = floor (radius / shell_width + 0.5f);
```

If there are fewer work-items than voxels, every work-item handles multiple indices separated by
`global_size`. Thus global worker $r$ processes
$r,r+\text{global size},r+2\text{global size},\ldots$. Together, the workers cover every coefficient
exactly once without requiring one work-item per voxel.

Because $x$ is the fastest-varying dimension, a flat index is decoded as

$$
x=index\bmod N_x,
$$

$$
y=\left\lfloor\frac{index}{N_x}\right\rfloor\bmod N_y,
\qquad
z=\left\lfloor\frac{index}{N_xN_y}\right\rfloor.
$$

The `q` calculations implement the signed, unshifted FFT coordinates from Section 1.1. Multiplying
by $(\Delta k_x,\Delta k_y,\Delta k_z)$ produces the physical frequency $\mathbf{k}$. The `sqrt` line
is $\rho(\mathbf{k})=\lVert\mathbf{k}\rVert_2$, and `shell` implements

$$
b=\left\lfloor\frac{\rho}{\Delta k}+\frac{1}{2}\right\rfloor.
$$

This avoids both a full-volume `fftshift` and a full-volume shell-index map.

#### 3.4.5 Scientific contributions and atomic accumulation

```
        if (shell < (float) num_bins) {
            const uint bin = convert_uint (shell);
            const float2 a = first[index];
            const float2 b = second[index];
            const float cross = a.x * b.x + a.y * b.y;
            const float power_a = dot (a, a);
            const float power_b = dot (b, b);
            const float magnitude_a = sqrt (power_a);
            const float magnitude_b = sqrt (power_b);
            const float minimum = fmin (magnitude_a, magnitude_b);
            const float maximum = fmax (magnitude_a, magnitude_b);

            atomic_add_float_local (&shells[bin], cross);
            atomic_add_float_local (&shells[num_bins + bin], power_a);
            atomic_add_float_local (&shells[2 * num_bins + bin], power_b);
            atomic_inc ((volatile __local uint *) &shells[3 * num_bins + bin]);
            atomic_min_float_local (&shells[4 * num_bins + bin], minimum);
            atomic_max_float_local (&shells[5 * num_bins + bin], maximum);
        }
    }
```

Only shells satisfying $b<B$ are retained. For coefficients
$a=a_r+ia_i$ and $c=c_r+ic_i$:

$$
Re(a\overline{c})=a_rc_r+a_ic_i.
$$

The code `a.x * b.x + a.y * b.y` is exactly this cross term. Similarly,
`dot(a,a)` is $a_r^2+a_i^2=|a|^2$, and `dot(b,b)` is $|c|^2$.
The kernel calls the second coefficient `b`; the equations call it $c$ here to avoid confusing that
coefficient with the shell index $b$.

The square roots recover $|a|$ and $|c|$ from their powers. The two additional atomic operations
retain the smallest and largest magnitude seen across either spectrum in the group's part of the
shell. These extrema are later used by Python for the geometric lower bound.

For work-group $g$, these atomic updates construct partial scientific quantities

$$
C_{g,b}=\sum_{\mathbf{k}\in S_b\cap W_g}Re(F_1(\mathbf{k})\overline{F_2(\mathbf{k})}),
$$

$$
P_{1,g,b}=\sum_{\mathbf{k}\in S_b\cap W_g}|F_1(\mathbf{k})|^2,
\qquad
P_{2,g,b}=\sum_{\mathbf{k}\in S_b\cap W_g}|F_2(\mathbf{k})|^2,
$$

$$
n_{g,b}=|S_b\cap W_g|,
$$

along with partial magnitude extrema $m_{g,b}$ and $M_{g,b}$. The latter are minima and maxima, not
sums. Empty group-shell combinations retain the initialization values infinity and zero.

where $W_g$ is the set of voxel indices processed by work-group $g$. These are not yet the final
$C_b$, $P_{1,b}$, $P_{2,b}$, and $n_b$ because every other group owns another part of the volume.

#### 3.4.6 Publishing one partial record per shell

```
    barrier (CLK_LOCAL_MEM_FENCE);

    for (size_t bin = local_id; bin < num_bins; bin += local_size) {
        partials[group_id * num_bins + bin] =
            (float8) (as_float (shells[bin]),
                      as_float (shells[num_bins + bin]),
                      as_float (shells[2 * num_bins + bin]),
                      convert_float (shells[3 * num_bins + bin]),
                      as_float (shells[4 * num_bins + bin]),
                      as_float (shells[5 * num_bins + bin]),
                      0.0f,
                      0.0f);
    }
}
```

The second barrier ensures that every work-item in the group has finished all atomic additions
before any member reads the completed local histogram.

The workers then cooperate again, this time to copy the $B$ shell records to global memory.
`as_float` recovers the float values stored as bit patterns. The count uses `convert_float` instead
because it is a numerical integer-to-float conversion. The resulting `float8` is ordered as
$(C_{g,b},P_{1,g,b},P_{2,g,b},n_{g,b},m_{g,b},M_{g,b},0,0)$.

The flat destination `group_id * num_bins + bin` is equivalent to
`partials[group_id, bin]` in a two-dimensional Python array. In terms of the GPU-shaped Python
example from Section 3.2, one `float8` record combines
`partial_cross[group_id, bin]`, `partial_power_1[group_id, bin]`,
`partial_power_2[group_id, bin]`, `partial_count[group_id, bin]`, `partial_minimum[group_id, bin]`,
and `partial_maximum[group_id, bin]`. The buffer is named `partials`
because each record is one work-group's subtotal rather than the complete value for that shell.

### 3.5 Second kernel: combine work-group partials

No barrier inside the first kernel can synchronize different work-groups. The global merge is
therefore a second kernel:

```
kernel void
fsc_reduce_partials (global const float8 *partials,
                     global float *output,
                     uint num_groups,
                     uint num_bins,
                     float shell_width)
{
    const size_t bin = get_global_id (0);

    if (bin >= num_bins)
        return;

    float4 total = (float4) (0.0f);
    float minimum = INFINITY;
    float maximum = 0.0f;

    for (uint group = 0; group < num_groups; group++) {
        const float8 partial = partials[group * num_bins + bin];
        total += partial.lo;
        minimum = fmin (minimum, partial.s4);
        maximum = fmax (maximum, partial.s5);
    }

    output[bin] = total.x;
    output[num_bins + bin] = total.y;
    output[2 * num_bins + bin] = total.z;
    output[3 * num_bins + bin] = total.w;
    output[4 * num_bins + bin] = (float) bin * shell_width;
    output[5 * num_bins + bin] = total.w > 0.0f ? minimum : as_float (0x7fc00000u);
    output[6 * num_bins + bin] = total.w > 0.0f ? maximum : as_float (0x7fc00000u);
}
```

This launch has $B$ work-items. Global work-item $b$ owns shell $b$ and loops over all $G$ partial
records for that shell:

$$
C_b=\sum_{g=0}^{G-1}C_{g,b},
\qquad
P_{1,b}=\sum_{g=0}^{G-1}P_{1,g,b},
$$

$$
P_{2,b}=\sum_{g=0}^{G-1}P_{2,g,b},
\qquad
n_b=\sum_{g=0}^{G-1}n_{g,b}.
$$

It separately calculates $m_b=\min_g m_{g,b}$ and $M_b=\max_g M_{g,b}$. The unused group's
infinity/zero initialization is neutral for these operations. If the final count is zero, the kernel
writes quiet `NaN` values for both extrema. A populated all-zero shell instead retains the meaningful
pair $(0,0)$.

The first and second kernels are submitted to the same in-order UFO command queue. The second kernel
therefore cannot begin until the first kernel has finished writing `partials`. This kernel-launch
boundary supplies the device-wide ordering that a work-group barrier cannot provide.

Every output element is overwritten. In the flat device buffer, seven blocks of $B$ floats are laid
out consecutively:

| Flat output range | NumPy row | Meaning |
|---|---:|---|
| `output[0:B]` | 0 | $C_b$ |
| `output[B:2B]` | 1 | $P_{1,b}$ |
| `output[2B:3B]` | 2 | $P_{2,b}$ |
| `output[3B:4B]` | 3 | $n_b$, transported as float32 |
| `output[4B:5B]` | 4 | $k_b=b\Delta k$ |
| `output[5B:6B]` | 5 | $m_b$, minimum magnitude across both spectra |
| `output[6B:7B]` | 6 | $M_b$, maximum magnitude across both spectra |

UFO describes this buffer as `dims=(B,7)` because `dims[0]` is the fastest-varying dimension.
NumPy consequently exposes the same memory as shape `(7,B)`.

### 3.6 Host-side kernel orchestration

During `setup`, the task obtains and retains both kernels:

```
/* Null checks and GError propagation are omitted from this focused excerpt. */
priv->accumulate_kernel = ufo_resources_get_kernel (
    resources, "fsc-core.cl", "fsc_accumulate_partials", NULL, error);
UFO_RESOURCES_CHECK_SET_AND_RETURN (
    clRetainKernel (priv->accumulate_kernel), error);

priv->reduce_kernel = ufo_resources_get_kernel (
    resources, "fsc-core.cl", "fsc_reduce_partials", NULL, error);
UFO_RESOURCES_CHECK_SET_AND_RETURN (
    clRetainKernel (priv->reduce_kernel), error);
```

During the first requisition, `configure_execution` queries the selected device's compute-unit,
work-group, and local-memory limits. It calculates the local-table and partial-buffer sizes with
overflow checks, rejects an impossible local allocation, and allocates
`num_groups * num_bins` `cl_float8` records.

For a complete pair, `process` obtains device pointers to $F_1$, $F_2$, and the compact output. It
passes the spectrum geometry, physical frequency increments, shell width, bin count, and total voxel
count as kernel arguments. The launches are:

```
local_bytes = (gsize) priv->num_bins * 6 * sizeof (cl_uint);
global_size = (size_t) priv->num_groups * priv->local_size;

/* Arguments 0..12 bind F1, F2, partials, local memory, geometry and binning. */
ufo_profiler_call (profiler, queue, priv->accumulate_kernel, 1,
                   &global_size, &priv->local_size);

/* Arguments 0..4 bind partials, output, group count, bin count and shell width. */
reduce_size = (size_t) priv->num_bins;
ufo_profiler_call (profiler, queue, priv->reduce_kernel, 1,
                   &reduce_size, NULL);
```

Both calls go through UFO's profiler, so both stages appear in profiling traces. The first-spectrum
copy and both kernels use the same in-order queue. The copy completes before a later kernel reads
$F_1$, and the partial kernel completes before the final kernel reads `partials`. The host does not
need to wait between these commands, and none of them asks UFO for a host array.

After launching both kernels, `process` marks the pair complete:

```
priv->have_first = FALSE;
priv->result_ready = TRUE;
priv->generated = FALSE;
return FALSE;
```

The scheduler then calls `generate`. The kernel has already written directly into the
scheduler-owned output buffer, so `generate` only publishes it once:

```
if (priv->result_ready && !priv->generated) {
    priv->generated = TRUE;
    return TRUE;
}

if (priv->result_ready && priv->generated) {
    priv->result_ready = FALSE;
    priv->generated = FALSE;
    return FALSE;
}
```

This lifecycle also explains why `fsc-core` is a reductor rather than a normal processor: the first
input produces no output, while the second completes and emits one reduction result.

### 3.7 Python normalization and typed output

Only the compact $(7,B)$ array crosses to the host. Python separates its rows, validates that the
transported count values are non-negative integers, checks the magnitude extrema, and exposes the
shared result used by both the UFO and NumPy benchmark paths:

```
FSCComputation
    frequency         : float32[B]
    fsc               : float64[B]
    n_shell           : int64[B]
    fixed_threshold   : float64[B]
    half_bit_threshold: float64[B]
    geometric_bound   : float64[B]
    geometry          : ShellGeometry
```

Normalization uses float64 intermediates:

```
cross = raw[0].astype(np.float64)
power_1 = raw[1].astype(np.float64)
power_2 = raw[2].astype(np.float64)
n_shell = np.rint(raw[3]).astype(np.int64)
frequency = raw[4].astype(np.float32)
minimum_magnitude = raw[5].astype(np.float64)
maximum_magnitude = raw[6].astype(np.float64)

denominator = np.sqrt(power_1 * power_2)
valid = (n_shell > 0) & (power_1 > 0.0) & (power_2 > 0.0)
fsc = np.full(cross.shape, np.nan, dtype=np.float64)
np.divide(cross, denominator, out=fsc, where=valid)
```

An empty shell or non-positive denominator remains `NaN`. Valid values are not clamped. Shared
utilities construct the fixed $0.143$ curve, the count-dependent half-bit curve, and the geometric
lower bound from `minimum_magnitude`, `maximum_magnitude`, and `n_shell`. Empty, all-zero, or
insufficient-count shells have a `NaN` geometric bound. The raw GPU statistics remain useful for
testing shell membership, counts, extrema, and reduction errors independently of the final curves.

For the half-bit curve, the shared utility uses

$$
s=\frac{\sqrt{2}-1}{2},
\qquad
h=2\sqrt{s},
$$

$$
T_{1/2,b}=\frac{s\sqrt{n_b}+h+1}{(s+1)\sqrt{n_b}+h}.
$$

For populated shells with more than ten samples, the geometric lower bound is

$$
G_b=\frac{2\sqrt{M_bm_b}}{M_b+m_b}.
$$

The implementation evaluates this through the ratio $m_b/M_b$ for numerical stability. If
$m_b=0<M_b$, the bound is zero. If $M_b=0$, the shell is all-zero and its bound remains `NaN`.
Unlike the fixed and half-bit curves, this geometric curve is a consistency bound and its crossing
is not reported as a spatial resolution.

### 3.8 Worked example with two work-groups

Consider an illustrative flattened spectrum with eight coefficients, three shells, local size $2$,
and two work-groups. There are four global work-items:

| Work-group | Work-items | Indices handled by striding |
|---:|---:|---|
| 0 | 0 and 1 | worker 0: 0, 4; worker 1: 1, 5 |
| 1 | 2 and 3 | worker 2: 2, 6; worker 3: 3, 7 |

Assume physical-frequency calculation assigns the coefficients as follows. The contribution tuple is
$(Re(a\overline{c}),|a|^2,|c|^2)$:

| Index | Group | Shell | $a=F_1$ | $c=F_2$ | Contribution |
|---:|---:|---:|---:|---:|---:|
| 0 | 0 | 0 | $2$ | $1$ | $(2,4,1)$ |
| 1 | 0 | 1 | $1$ | $1$ | $(1,1,1)$ |
| 4 | 0 | 2 | $1$ | $-1$ | $(-1,1,1)$ |
| 5 | 0 | 1 | $1$ | $i$ | $(0,1,1)$ |
| 2 | 1 | 0 | $1$ | $1$ | $(1,1,1)$ |
| 3 | 1 | 1 | $2$ | $1$ | $(2,4,1)$ |
| 6 | 1 | 1 | $i$ | $i$ | $(1,1,1)$ |
| 7 | 1 | 2 | $1+i$ | $1-i$ | $(0,2,2)$ |

The first kernel produces one **partial**, or subtotal, shell-statistics table per group:

| Group | Shell | Partial $C_{g,b}$ | Partial $P_{1,g,b}$ | Partial $P_{2,g,b}$ | Partial $n_{g,b}$ | $m_{g,b}$ | $M_{g,b}$ |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0 | 2 | 4 | 1 | 1 | 1 | 2 |
| 0 | 1 | 1 | 2 | 2 | 2 | 1 | 1 |
| 0 | 2 | -1 | 1 | 1 | 1 | 1 | 1 |
| 1 | 0 | 1 | 1 | 1 | 1 | 1 | 1 |
| 1 | 1 | 3 | 5 | 2 | 2 | 1 | 2 |
| 1 | 2 | 0 | 2 | 2 | 1 | $\sqrt{2}$ | $\sqrt{2}$ |

No row in this table is yet a final shell statistic. For example, group 0's shell-1 cross subtotal
is $1$, but it does not include the shell-1 contributions assigned to group 1. The second kernel adds
the two partial rows for each shell and produces the complete statistics:

| Shell $b$ | $C_b$ | $P_{1,b}$ | $P_{2,b}$ | $n_b$ | $m_b$ | $M_b$ | $FSC_b$ |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 3 | 5 | 2 | 2 | 1 | 2 | $3/\sqrt{10}\approx0.949$ |
| 1 | 4 | 7 | 4 | 4 | 1 | 2 | $4/\sqrt{28}\approx0.756$ |
| 2 | -1 | 3 | 3 | 2 | 1 | $\sqrt{2}$ | $-1/3\approx-0.333$ |

Before normalization, the device output is:

```
row 0, cross sum: [ 3, 4, -1]
row 1, power F1: [ 5, 7,  3]
row 2, power F2: [ 2, 4,  3]
row 3, count:    [ 2, 4,  2]
row 4, k_bin:    [ 0, Δk, 2Δk]
row 5, minimum:  [ 1, 1,  1]
row 6, maximum:  [ 2, 2, √2]
```

This example is deliberately small, but the $512^3$ case uses exactly the same structure: more
strided loop iterations, the same six local arrays per work-group, one partial `float8` per group
and shell, and one final work-item per shell.

### 3.9 Scientific equations and implementation locations

| Scientific quantity | Implementation |
|---|---|
| Signed index $q_N(j)$ | `qx`, `qy`, and `qz` conditional calculations |
| Physical frequency $\mathbf{k}$ | `kx`, `ky`, and `kz` |
| Radius $\rho(\mathbf{k})$ | `sqrt(kx * kx + ky * ky + kz * kz)` |
| Shell $b$ | `floor(radius / shell_width + 0.5f)` |
| Partial $C_{g,b}$ | First local-memory region and `cross` |
| Partial $P_{1,g,b}$ | Second local-memory region and `power_a` |
| Partial $P_{2,g,b}$ | Third local-memory region and `power_b` |
| Partial $n_{g,b}$ | Fourth local-memory region and `atomic_inc` |
| Partial $m_{g,b}$ | Fifth local-memory region and `atomic_min_float_local` |
| Partial $M_{g,b}$ | Sixth local-memory region and `atomic_max_float_local` |
| Final $(C_b,P_{1,b},P_{2,b},n_b)$ | Sum of `partials[group, bin]` in the second kernel |
| Final $m_b$ and $M_b$ | Minimum and maximum of `partials[group, bin]` in the second kernel |
| Bin centre $k_b$ | `(float) bin * shell_width` |
| $FSC_b$ | Python `cross / sqrt(power_1 * power_2)` |
| Geometric bound | Shared Python calculation from $m_b$, $M_b$, and $n_b$ |

The complete execution can now be read as:

```
UFO receives F1
    └─ copies F1 into task-owned device memory

UFO receives F2
    └─ first kernel
         ├─ group 0: private voxel calculations → shared local shell table ─┐
         ├─ group 1: private voxel calculations → shared local shell table ─┤
         └─ ...                                                             ├─ global partials[G,B]
                                                                            │
       second kernel                                                        │
         └─ one worker per shell sums partials[:,b] ◄────────────────────────┘
              └─ writes device output [C, P1, P2, n, k, min, max]

UFO emits one compact buffer
    └─ shared Python utilities normalize, construct criteria, and create FSCComputation
```

The implementation retains a full complex copy of $F_1$, calculates shell membership on demand, and
does not allocate an `fftshift` volume or shell-map volume. It deliberately uses full spectra in
this first version. A Hermitian half-spectrum optimization would have to preserve conjugate weights
and the meaning of `n_shell` and is therefore deferred.

Floating-point addition is not associative. Local atomic ordering and the later group summation can
produce small rounding differences relative to a sequential Python or NumPy calculation. Tests
therefore require exact counts but compare floating-point shell sums and FSC values with tolerances.
