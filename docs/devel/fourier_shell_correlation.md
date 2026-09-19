# Fourier Shell Correlation

This document records the scientific model and the computational architecture for Fourier
shell correlation (FSC) in UFO. The immediate scope is the computation of correlation curves and their supporting frequency and
sample-count vectors. Choosing a resolution threshold, locating a threshold crossing, reporting a
single resolution value, masking policy, local resolution, and directional missing-wedge correction
are deliberately deferred.

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
| $(N_z,N_x,N_y)$ | Numbers of samples along the logical $(z,x,y)$ axes. |
| $(\Delta_z,\Delta_x,\Delta_y)$ | Voxel spacings in micrometres. |
| $\mathbf{k}=(k_z,k_x,k_y)$ | A Fourier coordinate in cycles per micrometre. |
| $\rho(\mathbf{k})=\lVert\mathbf{k}\rVert_2$ | Radial spatial frequency in inverse micrometres. |
| $S_b$ | Set of Fourier samples assigned to shell/bin $b$. |
| $B$ | Number of frequency bins. |
| $C_b$ | Real cross-power sum in shell $b$. |
| $(P_{1,b},P_{2,b})$ | Auto-power sums in shell $b$. |
| $n_b$ | Number of Fourier samples in shell $b$. |

Logical volume shape is written as $(Z,X,Y)$ to match the project terminology. A UFO
`UfoRequisition` instead stores at most three dimensions with `dims[0]` fastest varying. The eventual
implementation must make the mapping between logical axes, array axes, and requisition dimensions
explicit; formulas in this document do not depend on that storage-order choice.

For an unshifted DFT, the signed integer frequency associated with index $j$ of an axis of length
$N$ is

$$
q_N(j)=
\begin{cases}
j, & 0\leq j < \lceil N/2\rceil,\\
j-N, & \lceil N/2\rceil \leq j < N.
\end{cases}
$$

The physical frequency vector at index $(j_z,j_x,j_y)$ is

$$
\mathbf{k} =
\left(
\frac{q_{N_z}(j_z)}{N_z\Delta_z},
\frac{q_{N_x}(j_x)}{N_x\Delta_x},
\frac{q_{N_y}(j_y)}{N_y\Delta_y}
\right).
$$

**Main idea.** Shell membership must be computed from physical frequencies, not merely from integer
distance to the array centre. This makes non-cubic volumes and anisotropic voxel spacings valid
inputs.

An `fftshift` is not required. The same shells result if the implementation evaluates the signed
frequency associated with each coefficient in UFO's native, unshifted FFT layout.

### 1.2 Frequency shells and output vectors

Let $e_0,\ldots,e_B$ be monotonically increasing bin edges in inverse micrometres. Shell $b$ is

$$
S_b = \left\{\mathbf{k}\;\middle|\;e_b \leq \rho(\mathbf{k}) < e_{b+1}\right\}.
$$

Its nominal bin centre and sample count are

$$
k_{\mathrm{bin},b}=\frac{e_b+e_{b+1}}{2},
\qquad
n_{\mathrm{shell},b}=|S_b|.
$$

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

Bins with $n_b=0$, zero power, or a non-finite denominator are invalid and must not silently
produce a plausible number. The benchmark returns `NaN` for these bins and does not clamp valid FSC
values.

The initial result contract is

```
fsc      : float [B]    # correlation curve
k_bin    : float [B]    # physical frequency, 1/um
n_shell  : int   [B]    # Fourier-sample count
```

The shell kernel naturally produces floating-point accumulators. `k_bin` and `n_shell` are derived
from volume shape, voxel spacing, bin edges, and the shell-membership convention; they do not need to
be recomputed for every pair with identical geometry.

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

Under the paper's equal-noise statistical model, let $\lambda_b^2$ be signal power and
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

Resolution estimation later chooses a correlation criterion $\tau$, finds an associated crossing
frequency $k_\ast$, and reports a real-space length such as

$$
d_\ast=\frac{1}{k_\ast}.
$$

**Main idea.** The FSC computation produces the spectral evidence; a threshold rule interprets where
that evidence ceases to be sufficient. The choice of $\tau$ and the precise crossing policy is part of a higher layer than UFO.

### 1.4 Classic-FSC

Classic FSC uses two separately reconstructed volumes whose noise is approximately independent. For
this project, the two volumes are reconstructed by `rgba-backproject` from alternating projection
indices using an `even_odd*` operation mode.

#### 1.4.1 Algorithm

Given a projection stream and common reconstruction geometry:

1. Assign alternating projections to even and odd half-sets as early as practical in the data path.
2. Reconstruct $V_{\mathrm{even}}$ and $V_{\mathrm{odd}}$ independently, using identical output
   grids and geometry.
3. Compute $F_{\mathrm{even}}=\mathcal{F}_3(V_{\mathrm{even}})$ and
   $F_{\mathrm{odd}}=\mathcal{F}_3(V_{\mathrm{odd}})$.
4. Assign every Fourier coefficient to a physical-frequency shell.
5. Accumulate $C_b$, $P_{\mathrm{even},b}$, $P_{\mathrm{odd},b}$, and $n_b$.
6. Normalize the accumulated statistics to obtain $FSC_b$.
7. Transfer only the compact result vectors to the host and attach physical-frequency and count
   types.

In pseudocode:

```
V_even, V_odd = reconstruct_alternating_projection_sets(projections)
F_even = fft3(V_even)
F_odd  = fft3(V_odd)

C, P_even, P_odd, n_shell = shell_statistics(F_even, F_odd, geometry)
fsc = C / sqrt(P_even * P_odd)
k_bin = physical_bin_centres(geometry)
return FSCResult(fsc, k_bin, n_shell)
```

Classic FSC requires no phase correction and no SFSC variance mapping. It is the experimental
reference against which both SFSC variants will be compared.

### 1.5 SFSC

SFSC estimates an FSC-like curve from one reconstructed volume. It creates paired measurements by
interleaving the even and odd samples along one spatial axis at a time. In three dimensions this
produces three pairs and therefore three directional curves.

For a selected axis $d$, let $n$ index the reduced grid. The interleaved split is

$$
V_{d,e}[n]=V_d[2n],
\qquad
V_{d,o}[n]=V_d[2n+1].
$$

All unsplit axes retain their lengths. The selected axis has half as many samples and twice the
voxel spacing. This is a decimation, not a split into two contiguous spatial halves.

#### 1.5.1 Why phase correction is necessary

Even and odd samples are displaced by one original-grid voxel, or one half of a reduced-grid sample.
With the DFT convention

$$
F(\mathbf{k})=\sum_{\mathbf{x}}V(\mathbf{x})
e^{-2\pi i\mathbf{k}\cdot\mathbf{x}},
$$

a spatial translation $\mathbf{a}$ produces a Fourier phase ramp. The odd spectrum is aligned to
the even spectrum using

$$
F_{d,o}^{\mathrm{aligned}}(\mathbf{k})=
F_{d,o}(\mathbf{k})
e^{-2\pi i\mathbf{k}\cdot\mathbf{a}_d},
$$

where $\mathbf{a}_d$ is one original voxel along the split axis, equivalently $0.5$ samples in
that pair's reduced grid.

**Main idea.** The even and odd sub-volumes sample the same structure from offset lattices. The phase
ramp removes that deterministic displacement before their Fourier coefficients are compared.

The sign of the programmed ramp is coupled to the FFT sign convention, which spectrum is translated,
and the order of the cross-product. It must be validated with a synthetic translated impulse or
plane wave rather than inferred from array-index names alone.

For a one-dimensional signal of even length $N$, decimation also aliases the upper and lower halves
of the original spectrum. After phase alignment, the split spectra have the form

$$
F_e[k]=\frac{F[k]+F[k+N/2]}{2},
\qquad
F_o^{\mathrm{aligned}}[k]=\frac{F[k]-F[k+N/2]}{2}.
$$

**Main idea.** The upper-half spectrum enters the two split volumes with opposite signs. SFSC agrees
with conventional FSC only when the statistics of this aliased term are controlled by assumptions or
by the corrected preprocessing path.

#### 1.5.2 Basic SFSC

Basic SFSC applies when the following conditions are sufficiently accurate:

1. signal and noise are statistically independent;
2. noise is approximately white Gaussian noise, so its Fourier power is constant with frequency;
3. signal power decays rapidly enough that the aliased high-frequency term
   $|F[k+N/2]|^2$ is small in the frequency range being interpreted;
4. the half-sample phase correction is applied.

Let $s_b$ denote the raw, phase-corrected correlation of an even/odd split pair. Under these
assumptions, the paper derives a doubled effective noise variance for SFSC. The corresponding estimate
of conventional FSC is

$$
\widehat{FSC}_b=
\frac{2s_b}{1+s_b}.
$$

**Main idea.** The raw split correlation is not yet on the same scale as conventional FSC; the
nonlinear mapping compensates for the variance change introduced by decimation.

Algorithm for each axis $d\in\{z,x,y\}$:

1. Split the single volume into $V_{d,e}$ and $V_{d,o}$ using interleaved samples.
2. Record the pair geometry: the split-axis length is halved and its voxel spacing is doubled.
3. Compute a 3-D FFT of both sub-volumes.
4. Apply the half-sample phase ramp to the odd spectrum.
5. Compute the common shell statistics and raw curve $s_{d,b}$.
6. Apply $2s_{d,b}/(1+s_{d,b})$.
7. Retain the corrected directional curve rather than immediately discarding it through averaging.

After all axes have been processed, an optional global estimate is

$$
\widehat{FSC}_{\mathrm{global},b}=
\frac{1}{3}\sum_{d\in\{z,x,y\}}
\widehat{FSC}_{d,b},
$$

provided the three curves have been defined on the same physical-frequency grid.

**Main idea.** Averaging follows the paper's global SFSC definition, but retaining all three curves
preserves directional information that is particularly valuable for tomography.

Basic SFSC does **not** upsample merely to restore the original array shape. Its split pairs are
smaller along their respective split axes. Their physical frequency coordinates remain well-defined
by the changed sample count and spacing.

#### 1.5.3 Corrected SFSC

Corrected SFSC extends the method to colored noise and to signals whose spectra do not decay rapidly
enough for the basic aliasing assumption.

##### Noise whitening

Let $P_N(\mathbf{k})$ be an estimate of the noise power spectrum. Whitening is

$$
F_w(\mathbf{k})=
\frac{F(\mathbf{k})}
{\sqrt{P_N(\mathbf{k})+\epsilon}},
$$

where $\epsilon>0$ prevents division by zero.

If the estimate is correct, the whitened noise has approximately unit Fourier variance:

$$
Cov(N_w)\approx I.
$$

**Main idea.** Whitening equalizes noise power across frequency. Without it, colored noise can remain
correlated through the split construction and can make basic SFSC systematically overestimate or
underestimate conventional FSC.

The paper estimates noise from a region believed to contain only background. That is a scientific
input, not a universally valid implementation detail. Masking and spatially non-uniform noise can
make a background-derived estimate unrepresentative of the region of interest.

##### Fourier zero-padding upsampling

The whitened volume is upsampled by embedding its spectrum into the centre of a larger zero-filled
Fourier grid and applying an inverse 3-D FFT:

$$
V_{wu}=\mathcal{F}_3^{-1}\!\left(
pad_{0}\left(\mathcal{F}_3(V_w),2N_z,2N_x,2N_y\right)
\right).
$$

The inverse-transform scaling must be chosen consistently so that the upsampled real-space amplitude
and the assumed whitened-noise variance match the derivation.

**Main idea.** This is band-limited Fourier interpolation. The newly created high-frequency
coefficients are exactly zero, suppressing the aliased upper-spectrum contribution that invalidates
basic SFSC for slowly decaying spectra. Nearest-neighbour, linear, or spline interpolation is not an
equivalent operation.

The paper's reference implementation pads every array dimension. For a 3-D volume this creates a
$(2Z,2X,2Y)$ intermediate before the three directional splits. An axis-only construction such as
$(2Z,X,Y)$, followed by a split along $z$, has a much smaller peak-memory cost and restores the
original shape for that pair, but its equivalence to the paper's multidimensional correction has not
yet been established. It is therefore an optimization/research question, not the current
methodological baseline.

##### Numerator noise-bias correction

After whitening and zero-padding, the paper subtracts the known noise contribution from the *mean*
cross-power in each shell. Define

$$
\overline{C}_b=\frac{C_b}{n_b},
\qquad
\overline{P}_{i,b}=\frac{P_{i,b}}{n_b}.
$$

The corrected curve is

$$
\widehat{FSC}_b=
\frac{\overline{C}_b-\gamma_b}
{\sqrt{\overline{P}_{1,b}\overline{P}_{2,b}}},
\qquad
\gamma_b=\frac{1}{4}\sigma_b^2.
$$

After ideal whitening under the paper's FFT normalization, $\sigma_b^2=1$ and therefore
$\gamma_b=1/4$.

**Main idea.** Zero-padding deliberately sets the added high-frequency noise to zero. Subtracting
$\gamma_b$ removes the remaining predictable noise term from the cross-correlation numerator.

The literal value `0.25` is not portable across arbitrary FFT normalizations. The implementation must
either reproduce the reference normalization or derive $\gamma_b$ in the normalization actually
used by UFO, and validate it statistically on whitened noise.

Corrected-SFSC algorithm:

1. Estimate $P_N(\mathbf{k})$ from a scientifically justified noise-only input or region.
2. Compute the volume's 3-D FFT and whiten it using $P_N$.
3. Fourier-zero-pad the whitened spectrum to twice the size in every dimension, with consistent
   inverse-FFT scaling, and transform back to real space.
4. For each axis, form the interleaved even/odd pair from the upsampled volume.
5. FFT each pair and phase-align the odd spectrum.
6. Accumulate shell cross-power, two auto-powers, and counts.
7. Apply the numerator bias correction using the validated $\gamma_b$, then normalize.
8. Return all three directional curves and optionally their common-grid average.

The basic and corrected behaviors are intended to remain distinct SFSC configurations. They share
the same split, phase-alignment, shell-statistics, and result machinery, but corrected SFSC adds noise
estimation, whitening, Fourier upsampling, and the numerator-bias correction. The basic variance
mapping $2s/(1+s)$ is not stacked on top of the corrected numerator formula.

---

## 2. Computational Workflows

The architectural goal is to keep reconstructed volumes and spectra device-resident. Python/tofu
will construct and run UFO graphs, select the experimental method, and assemble a typed result. It
should receive only compact shell statistics or final vectors, not full $512^3$ volumes.

### 2.1 Common computational core

All three methods reduce to the same central operation once two aligned complex spectra are
available:

```
complex spectrum 1 ─┐
                    ├─ shell statistics ─ normalization/correction ─ compact vectors
complex spectrum 2 ─┘
```

The common device-side core now implemented by `fsc-core`:

1. accepts consecutive pairs of equal-shape, complex-interleaved 3-D spectra;
2. maps each Fourier coefficient to a physical-frequency bin;
3. accumulates `cross_sum`, `power_1_sum`, `power_2_sum`, and `count` per bin;
4. emits those statistics plus the bin centres in one compact buffer;
5. resets safely for the next same-shaped pair.

SFSC phase correction belongs before this method-independent task. The classic normalization and
future method-specific corrections operate on only $B$ elements and remain in Python. Returning raw
shell statistics exposes normalization and counting errors during validation without transferring a
full spectrum.

The central primitive is therefore **shell-wise reduction of two 3-D complex spectra**. The vector
result is carried as a small two-dimensional UFO buffer with one row per statistic. The hard part is
changing millions of irregularly grouped 3-D samples into a few hundred shell accumulators
efficiently and reproducibly, not transporting the compact result.

### 2.2 Classic-FSC workflow

```
projection stream
      │
      ▼
rgba-backproject
operation-mode=even_odd*
output-mode=volume
      │
      ▼
device stream [V_even, V_odd]
      │
      ▼
fft(dimensions=3)
      │
      ▼
device stream [F_even, F_odd]
      │
      ▼
fsc-core ─ compact shell statistics ─ Python normalization ─ FSCResult
```

The scientific split and the device-only connection to the existing 3-D FFT now exist.
`rgba-backproject` emits the even volume first and the odd volume second; one processor-mode FFT task
instance preserves that stream order and produces two consecutive complex-interleaved spectra. No
branch, demultiplexer, CPU `stack`, or full-volume host transfer is required. Because pairing is
stateful, `fsc-core` rejects graph copying and the classic benchmark disables expansion and selects
one GPU.

The common UFO primitive is `fsc-core`. For the sequential stream above, it recognizes consecutive
pairs, preserves the first spectrum on the device until the second arrives, reduces the pair into
shell statistics, and emits a compact result. Python performs the inexpensive normalization and
constructs the typed public result.

#### 2.2.1 Classic-FSC readiness checklist

| Building block | Status | Evidence or remaining responsibility |
|---|---|---|
| Alternating even/odd projection reconstruction | Available | Both `rgba-backproject` `even_odd*` operation modes emit even then odd. |
| Device-resident 3-D volume output | Available | `output-mode=volume` reports `(Nx,Ny,Z)`, requests only the output device array, and marks it real. |
| Direct volume-to-FFT connection | Available | `fft dimensions=3` accepts the 3-D real requisition and consumes the device buffer without `stack`. |
| Sequential spectrum pairing | Available in `fsc-core` | The first complex spectrum is copied device-to-device and retained until the second arrives. |
| Physical shell assignment | Available in `fsc-core` | Native unshifted FFT indices are assigned to conservative physical shells without `fftshift` or a shell-map volume. |
| Shell-wise complex correlation and reduction | Available in `fsc-core` | A staged local-histogram reduction produces cross-correlation, two power sums, and `n_shell`. |
| FSC normalization and compact output | Available across UFO and Python | `fsc-core` emits five compact rows; Python normalizes the three sums into FSC. |
| Typed host result | Available in the benchmark layer | `FSCShellStatistics` constructs `FSCResult` after only compact data crosses to the host. The eventual tofu API remains future work. |

Thus, the classic computational track has all required building blocks. The standalone benchmark
under `benchmarks/fsc` composes them while the eventual `tofu fsc` interface remains future work.

#### 2.2.2 `fsc-core` interface and bin policy

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
this is the paper's rounded integer-radius convention expressed in physical units.

The task emits a 2-D UFO requisition `dims=(B,5)`, seen by NumPy as `(5,B)`. Its float32 rows are, in
order, $C_b$, $P_{1,b}$, $P_{2,b}$, $n_b$, and $k_b$. Counts are accumulated as integers and converted
only in the compact output. Python validates their integrality, converts them to `int64`, and computes
$C_b/\sqrt{P_{1,b}P_{2,b}}$ using float64 intermediates.

### 2.3 Basic-SFSC workflow

```
projection stream
      │
      ▼
rgba-backproject, singular reconstruction ─ one device-resident 3-D volume
      │
      ├─ interleaved z split ─ FFT pair ─ z phase ─ fsc-core ─ 2s/(1+s) ─ fsc_z
      ├─ interleaved x split ─ FFT pair ─ x phase ─ fsc-core ─ 2s/(1+s) ─ fsc_x
      └─ interleaved y split ─ FFT pair ─ y phase ─ fsc-core ─ 2s/(1+s) ─ fsc_y
```

The three branches may execute sequentially to reduce peak memory or concurrently to reduce latency
when device memory permits. This is intentionally left open for benchmarking. A single source volume
may be broadcast to branches by graph construction, but UFO buffer lifetimes and multi-GPU graph
expansion must be checked before assuming that broadcast is a zero-copy operation.

### 2.4 Corrected-SFSC workflow

```
singular 3-D volume ─ FFT ─ noise whitening ─ Fourier zero padding ─ IFFT
                                                              │
                                                              ▼
                                                   upsampled real volume
                                                              │
          ┌───────────────────────────────────────────────────┼───────────────┐
          ▼                                                   ▼               ▼
    z split + FFTs                                      x split + FFTs  y split + FFTs
          │                                                   │               │
       z phase                                             x phase          y phase
          │                                                   │               │
     fsc-core + gamma                                  same correction  same correction
          │                                                   │               │
        fsc_z                                               fsc_x           fsc_y
```

Noise-power estimation may require a separate noise-only volume or mask/region selection supplied by
the higher layer. That scientific policy should not be hidden inside the generic shell-correlation
task. Whitening itself is a shape-preserving Fourier-domain multiplication and is suitable for a UFO
GPU task. Fourier zero-padding changes the requisition and therefore needs a dedicated task or a
purpose-built corrected-SFSC component; it cannot be expressed by the current generic OpenCL task
alone.

### 2.5 `rgba-backproject` output representation

The `rgba-backproject` output representation is now configurable as

```
output-mode = slices | volume
```

without changing the parity mathematics or the existing `stack` task:

- `slices` preserves the current public behavior and compatibility;
- `volume` emits one device-resident 3-D buffer in singular reconstruction mode;
- `volume` emits two sequential device-resident 3-D buffers in `even_odd*` mode, even first and odd
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

No change to the existing `stack` task is proposed. The volume-output capability belongs with the new
FSC-oriented development and `rgba-backproject`, where the data is already accumulated on the GPU.
The CPU [`stack`](../../src/ufo-stack-task.c) is no longer present in the classic device path.

This connection was validated on 2026-09-01 for both `even_odd_single` and `even_odd_dual`. In each
case, slice and volume outputs agreed exactly for the test data, including parity order and a z depth
that exercised RGBA padding. The direct device graph
`rgba-backproject → fft dimensions=3 → null` also completed successfully without an intervening
`stack` task. Isolated numerical 3-D FFT checks at $8^3$ and $32^3$ also agreed with NumPy. This is
functional and interface validation; it does not replace the later $512^3$ runtime and peak-memory
benchmark.

### 2.6 Available and missing UFO building blocks

| Operation | Current UFO support | Intended use or limitation |
|---|---|---|
| Even/odd projection reconstruction | Available in `rgba-backproject` `even_odd*` modes | Reuse for classic FSC. The scientific split is already present. |
| Single reconstruction | Available in `rgba-backproject` singular mode | Reuse as SFSC input. |
| 3-D device FFT/IFFT | Available in `fft` and `ifft` with `dimensions=3` | Reuse. Direct consumption of `rgba-backproject` volume output is validated. FFT size, padding, layout, and normalization must still be fixed explicitly for reproducibility. |
| 2-D slices to 3-D volume | `stack` is available but CPU-based | Useful as a correctness prototype, not for the desired device-only path; do not modify it for FSC. |
| Device-resident volume output from `rgba-backproject` | Available with `output-mode=volume` | Emits one singular volume or even then odd volumes as real 3-D device buffers; direct 3-D FFT compatibility is validated. |
| Interleaved split along a selected 3-D axis | Missing | Needs a requisition-changing GPU task or an integrated SFSC preprocessor. |
| Half-sample phase ramp | No dedicated task | Simple GPU arithmetic; may be separate for validation or fused into shell cross-power. |
| Noise-power estimation | No general FSC-ready 3-D task | Method depends on noise input/region policy; shell-averaged noise power can reuse the future shell-reduction machinery. |
| Fourier-domain whitening | No dedicated 3-D task | Shape-preserving GPU multiplication; feasible as a small dedicated kernel/task. |
| Fourier zero-padding upsampling | Existing FFT size options do not by themselves express the complete corrected-SFSC operation | Needs spectrum embedding, normalization-aware IFFT, and changed requisition. |
| Physical shell map/bin metadata | Available in `fsc-core` | Shell membership is calculated from unshifted indices in the accumulation kernel; no full-size map is materialized. |
| Complex shell correlation/reduction | Available in `fsc-core` | The method-independent GPU reductor pairs spectra and emits compact raw statistics and bin centres. |
| Existing `power-spectrum` and correlation tasks | Available but not FSC building blocks | `power-spectrum` is a shape-preserving 2-D auto-power operation; `correlate-stacks` computes 2-D squared differences; `cross-correlate` performs 2-D alignment correlation. None performs physical 3-D shell reduction. |
| Compact 1-D device output | Supported by UFO requisitions and `OutputTask` | Vector shape is not a blocker. UFO storage is float32, so typing is completed in Python. |
| Pipeline composition | Available through JSON/Python task graphs | Use tofu/Python to compose methods; no hybrid aggregate UFO task is required initially. |

### 2.7 Python/tofu responsibility and typed result

Tofu is the natural orchestration layer because it already builds UFO graphs from Python and consumes
`Ufo.OutputTask` buffers as NumPy arrays. Its responsibilities should be:

- choose classic, basic-SFSC, or corrected-SFSC configuration;
- wire the appropriate reconstruction and preprocessing branches;
- provide voxel spacing, binning policy, and any noise-estimation input or mask;
- collect only compact device results through an output task;
- construct the public typed result and preserve method/axis metadata;
- compare curves, record timing and memory measurements, and later apply resolution criteria.

A conceptual host result is

```
FSCResult
    fsc      : float32 [B]
    k_bin    : float32 [B]   # 1/um
    n_shell  : int64   [B]
    method   : classic | sfsc-basic | sfsc-corrected
    axis     : none | z | x | y | mean
    metadata : shape, voxel spacing, bin edges, normalization, corrections
```

Classic FSC contains one `FSCResult`. An SFSC result contains `z`, `x`, and `y` `FSCResult` values and
may additionally contain `mean`. Keeping a complete result per axis avoids assuming prematurely that
the directional `k_bin` and `n_shell` vectors are identical.

UFO buffers contain float32 storage. `fsc-core` therefore transports its five output rows as float32,
including the shell counts. Counts are accumulated as integers on the device and are exactly
representable after conversion for the target $512^3$ volumes. The Python layer validates that the
received values are integral and exposes `n_shell` as `int64`. Larger future volume sizes must revisit
this transport rather than silently assuming float32 exactness.

For SFSC, the three axes can have different pair shapes and shell populations. `k_bin` and `n_shell`
are common only when their physical grids and bin policies actually coincide. $Z=X=Y$ is convenient
but not a mathematical requirement, and equality of array lengths alone is insufficient when voxel
spacings differ.

### 2.8 Runtime and memory implications at $512^3$

With float32 real storage and a full complex-interleaved float32 spectrum:

| Object | Approximate storage |
|---|---:|
| One $512^3$ real volume | 512 MiB |
| One full $512^3$ complex spectrum | 1 GiB |
| One $1024^3$ real volume | 4 GiB |
| One full $1024^3$ complex spectrum | 8 GiB |

These values exclude FFT work buffers, reconstruction accumulators, split volumes, shell-index data,
and concurrent branches.

The transform workloads also differ:

- classic FSC requires two full-volume 3-D FFTs after reconstruction;
- basic SFSC requires six 3-D FFTs, but every input is half-sized along its split axis, for a total of
  three original-volume-equivalents of transformed samples;
- corrected SFSC using full $2\times$ upsampling in all dimensions has a much larger peak working
  set and transform cost; materializing all six split spectra concurrently is not viable at the
  target size on typical devices.

These estimates make streaming pair processing, buffer reuse, phase fusion, and sequential SFSC axes
important candidates for benchmarking. They do not justify changing the scientific algorithm before
the full correction has been validated against the reference implementation.

### 2.9 Decisions established by this methodology

The following choices are sufficiently clear to carry into later implementation planning:

1. Classic FSC, basic SFSC, and corrected SFSC remain distinct experimental methods.
2. Basic and corrected behavior will eventually be selected through SFSC configuration rather than
   hidden behind one unconditional preprocessing path.
3. `rgba-backproject` remains the source of singular and even/odd reconstructions.
4. Full reconstructed volumes and spectra should remain on the device; only reduced shell data should
   cross to the host.
5. Existing UFO 3-D FFT/IFFT tasks should be reused where their layout and normalization satisfy the
   validated workflow.
6. `fsc-core` is the common complex shell-statistics primitive for classic FSC and future SFSC pairs.
7. Python/tofu owns graph orchestration and the typed public result; PyTorch is not required merely to
   normalize a few compact vectors.
8. The current CPU `stack` task remains unchanged. `rgba-backproject output-mode=volume` now provides
   the device-resident path and has been validated with the existing 3-D FFT.
9. All three SFSC directional curves are retained. A mean curve is an additional output, not a
   replacement for them.
10. Threshold criteria and conversion of a curve into a scalar resolution are outside the initial
    computational workflow.

### 2.10 Questions intentionally left for later SFSC planning and validation

The document does not yet select answers to the following questions:

- full-spectrum versus Hermitian half-spectrum accumulation;
- sequential versus concurrent SFSC axis execution and the effect of UFO graph expansion;
- noise-estimation inputs and policies appropriate to the DAQ data;
- the FFT scaling that reproduces the corrected-SFSC $\gamma$ derivation;
- whether an axis-wise Fourier-upsampled workflow is mathematically and numerically equivalent to the
  paper's full multidimensional padding for this application;
- the common physical-frequency range used when averaging directional SFSC curves;
- benchmark data and performance targets for comparing alternative SFSC workflows with the authors'
  NumPy implementation.

These are planning and validation inputs, not ambiguities in the high-level method.

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
                                                [C, P1, P2, n, k] on device
                                                                          │
                                                        compact transfer  ▼
                                                               Python normalization
                                                                          │
                                                                          ▼
                                                                     FSCResult
```

The GPU does not calculate the final division. It produces the sufficient statistics
$(C_b,P_{1,b},P_{2,b},n_b,k_b)$ for every shell. Python then calculates
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

Consecutive buffers form pairs. This fits the classic pipeline because `rgba-backproject` emits the
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
3. resolves $\Delta k$, $k_{\max}$, and $B$ using Section 2.2.2;
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

# All voxels have now been visited, so these arrays contain final shell sums.
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

# Stage 1: every group calculates a complete subtotal for its assigned voxels.
for group, indices in enumerate(indices_per_group):
    for index in indices:
        b, cross, power_1, power_2 = voxel_contribution(index)

        if b < B:
            partial_cross[group, b] += cross
            partial_power_1[group, b] += power_1
            partial_power_2[group, b] += power_2
            partial_count[group, b] += 1

# Stage 2: combine all group subtotals into the complete shell statistics.
cross_sum = partial_cross.sum(axis=0)
power_1_sum = partial_power_1.sum(axis=0)
power_2_sum = partial_power_2.sum(axis=0)
n_shell = partial_count.sum(axis=0)
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

The corresponding partial power sums and count are

$$
P_{1,g,b}=\sum_{\mathbf{k}\in S_b\cap W_g}|F_1(\mathbf{k})|^2,
\qquad
P_{2,g,b}=\sum_{\mathbf{k}\in S_b\cap W_g}|F_2(\mathbf{k})|^2,
$$

$$
n_{g,b}=|S_b\cap W_g|.
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
| `local` | Work-items in one work-group only | One temporary four-vector shell histogram per group |
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

Every group needs four local arrays of $B$ unsigned 32-bit words:

```
shells[0            : B]     cross sums C
shells[B            : 2 * B] first-spectrum powers P1
shells[2 * B        : 3 * B] second-spectrum powers P2
shells[3 * B        : 4 * B] integer counts n
```

The allocation therefore requires $4B\times4=16B$ bytes of local memory. It is written in C as
`B * sizeof(cl_uint4)`, although the kernel addresses it as one flat `uint` array. The task rejects
the configuration if this allocation plus the kernel's static local memory exceeds the device
limit.

Each group ultimately writes $B$ `float4` partial records to global memory. With $G$ groups, the
partial buffer is therefore $G\times B\times16$ bytes. This buffer is compact compared with either
full spectrum.

### 3.4 First kernel: one partial histogram per work-group

The first kernel is `fsc_accumulate_partials`. The following annotated blocks contain all of its
executable code, including its helper function.

#### 3.4.1 Portable float addition in local memory

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

#### 3.4.2 Kernel arguments and worker identities

```
kernel void
fsc_accumulate_partials (global const float2 *first,
                         global const float2 *second,
                         global float4 *partials,
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
    for (size_t item = local_id; item < 4 * num_bins; item += local_size)
        shells[item] = 0;

    barrier (CLK_LOCAL_MEM_FENCE);
```

The local shell table is scratch memory and may initially contain arbitrary bits. Its initialization
is shared: local worker $0$ clears entries $0,L,2L,\ldots$, worker $1$ clears
$1,L+1,2L+1,\ldots$, and so on for local size $L$.

The barrier means: *every work-item in this work-group must finish its local-memory writes before any
work-item in the group continues*. Without it, one worker could add a contribution while another
worker was still clearing the same shell, erasing the contribution. This barrier says nothing about
other work-groups; each has its own independent `shells` allocation. Every work-item in the group
must encounter the barrier, which is why it is outside the initialization loop.

Integer zero has the same all-zero bit pattern as floating-point `0.0f`. It therefore initializes
both the three float-bit regions and the integer-count region correctly.

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

            atomic_add_float_local (&shells[bin], cross);
            atomic_add_float_local (&shells[num_bins + bin], power_a);
            atomic_add_float_local (&shells[2 * num_bins + bin], power_b);
            atomic_inc ((volatile __local uint *) &shells[3 * num_bins + bin]);
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

where $W_g$ is the set of voxel indices processed by work-group $g$. These are not yet the final
$C_b$, $P_{1,b}$, $P_{2,b}$, and $n_b$ because every other group owns another part of the volume.

#### 3.4.6 Publishing one partial record per shell

```
    barrier (CLK_LOCAL_MEM_FENCE);

    for (size_t bin = local_id; bin < num_bins; bin += local_size) {
        partials[group_id * num_bins + bin] =
            (float4) (as_float (shells[bin]),
                      as_float (shells[num_bins + bin]),
                      as_float (shells[2 * num_bins + bin]),
                      convert_float (shells[3 * num_bins + bin]));
    }
}
```

The second barrier ensures that every work-item in the group has finished all atomic additions
before any member reads the completed local histogram.

The workers then cooperate again, this time to copy the $B$ shell records to global memory.
`as_float` recovers the float values stored as bit patterns. The count uses `convert_float` instead
because it is a numerical integer-to-float conversion. The resulting `float4` is ordered as
$(C_{g,b},P_{1,g,b},P_{2,g,b},n_{g,b})$.

The flat destination `group_id * num_bins + bin` is equivalent to
`partials[group_id, bin]` in a two-dimensional Python array. In terms of the GPU-shaped Python
example from Section 3.2, one `float4` record combines
`partial_cross[group_id, bin]`, `partial_power_1[group_id, bin]`,
`partial_power_2[group_id, bin]`, and `partial_count[group_id, bin]`. The buffer is named `partials`
because each record is one work-group's subtotal rather than the complete value for that shell.

### 3.5 Second kernel: combine work-group partials

No barrier inside the first kernel can synchronize different work-groups. The global merge is
therefore a second kernel:

```
kernel void
fsc_reduce_partials (global const float4 *partials,
                     global float *output,
                     uint num_groups,
                     uint num_bins,
                     float shell_width)
{
    const size_t bin = get_global_id (0);

    if (bin >= num_bins)
        return;

    float4 total = (float4) (0.0f);

    for (uint group = 0; group < num_groups; group++)
        total += partials[group * num_bins + bin];

    output[bin] = total.x;
    output[num_bins + bin] = total.y;
    output[2 * num_bins + bin] = total.z;
    output[3 * num_bins + bin] = total.w;
    output[4 * num_bins + bin] = (float) bin * shell_width;
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

The first and second kernels are submitted to the same in-order UFO command queue. The second kernel
therefore cannot begin until the first kernel has finished writing `partials`. This kernel-launch
boundary supplies the device-wide ordering that a work-group barrier cannot provide.

Every output element is overwritten. In the flat device buffer, five blocks of $B$ floats are laid
out consecutively:

| Flat output range | NumPy row | Meaning |
|---|---:|---|
| `output[0:B]` | 0 | $C_b$ |
| `output[B:2B]` | 1 | $P_{1,b}$ |
| `output[2B:3B]` | 2 | $P_{2,b}$ |
| `output[3B:4B]` | 3 | $n_b$, transported as float32 |
| `output[4B:5B]` | 4 | $k_b=b\Delta k$ |

UFO describes this buffer as `dims=(B,5)` because `dims[0]` is the fastest-varying dimension.
NumPy consequently exposes the same memory as shape `(5,B)`.

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
`num_groups * num_bins` `cl_float4` records.

For a complete pair, `process` obtains device pointers to $F_1$, $F_2$, and the compact output. It
passes the spectrum geometry, physical frequency increments, shell width, bin count, and total voxel
count as kernel arguments. The launches are:

```
local_bytes = (gsize) priv->num_bins * sizeof (cl_uint4);
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

Only the compact $(5,B)$ array crosses to the host. Python separates its rows, validates that the
transported count values are non-negative integers, and exposes the public result as:

```
FSCResult
    fsc      : float32[B]
    k_bin    : float32[B]
    n_shell  : int64[B]
```

Normalization uses float64 intermediates:

```
cross = raw[0].astype(np.float64)
power_1 = raw[1].astype(np.float64)
power_2 = raw[2].astype(np.float64)
n_shell = np.rint(raw[3]).astype(np.int64)
k_bin = raw[4].astype(np.float32)

denominator = np.sqrt(power_1 * power_2)
valid = (n_shell > 0) & (power_1 > 0.0) & (power_2 > 0.0)
fsc = np.full(cross.shape, np.nan, dtype=np.float64)
np.divide(cross, denominator, out=fsc, where=valid)
fsc = fsc.astype(np.float32)
```

An empty shell or non-positive denominator remains `NaN`. Valid values are not clamped. The raw GPU
statistics remain useful for testing shell membership, counts, and reduction errors independently of
the final division.

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

| Group | Shell | Partial $C_{g,b}$ | Partial $P_{1,g,b}$ | Partial $P_{2,g,b}$ | Partial $n_{g,b}$ |
|---:|---:|---:|---:|---:|---:|
| 0 | 0 | 2 | 4 | 1 | 1 |
| 0 | 1 | 1 | 2 | 2 | 2 |
| 0 | 2 | -1 | 1 | 1 | 1 |
| 1 | 0 | 1 | 1 | 1 | 1 |
| 1 | 1 | 3 | 5 | 2 | 2 |
| 1 | 2 | 0 | 2 | 2 | 1 |

No row in this table is yet a final shell statistic. For example, group 0's shell-1 cross subtotal
is $1$, but it does not include the shell-1 contributions assigned to group 1. The second kernel adds
the two partial rows for each shell and produces the complete statistics:

| Shell $b$ | $C_b$ | $P_{1,b}$ | $P_{2,b}$ | $n_b$ | $FSC_b$ |
|---:|---:|---:|---:|---:|---:|
| 0 | 3 | 5 | 2 | 2 | $3/\sqrt{10}\approx0.949$ |
| 1 | 4 | 7 | 4 | 4 | $4/\sqrt{28}\approx0.756$ |
| 2 | -1 | 3 | 3 | 2 | $-1/3\approx-0.333$ |

Before normalization, the device output is:

```
row 0, cross sum: [ 3, 4, -1]
row 1, power F1: [ 5, 7,  3]
row 2, power F2: [ 2, 4,  3]
row 3, count:    [ 2, 4,  2]
row 4, k_bin:    [ 0, Δk, 2Δk]
```

This example is deliberately small, but the $512^3$ case uses exactly the same structure: more
strided loop iterations, the same four local arrays per work-group, one partial `float4` per group
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
| Final $(C_b,P_{1,b},P_{2,b},n_b)$ | Sum of `partials[group, bin]` in the second kernel |
| Bin centre $k_b$ | `(float) bin * shell_width` |
| $FSC_b$ | Python `cross / sqrt(power_1 * power_2)` |

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
              └─ writes device output [C, P1, P2, n, k]

UFO emits one compact buffer
    └─ Python converts counts, normalizes the shell sums, and creates FSCResult
```

The implementation retains a full complex copy of $F_1$, calculates shell membership on demand, and
does not allocate an `fftshift` volume or shell-map volume. It deliberately uses full spectra in
this first version. A Hermitian half-spectrum optimization would have to preserve conjugate weights
and the meaning of `n_shell` and is therefore deferred.

Floating-point addition is not associative. Local atomic ordering and the later group summation can
produce small rounding differences relative to a sequential Python or NumPy calculation. Tests
therefore require exact counts but compare floating-point shell sums and FSC values with tolerances.

---

## 4. References

1. Eric Verbeke *et al.*, “Self Fourier shell correlation: properties and application to cryo-ET,”
   *Communications Biology* 7, 101 (2024),
   [doi:10.1038/s42003-023-05724-y](https://doi.org/10.1038/s42003-023-05724-y).
2. Authors' reference implementation,
   [`self_fourier_shell_correlation`](https://github.com/EricVerbeke/self_fourier_shell_correlation).
