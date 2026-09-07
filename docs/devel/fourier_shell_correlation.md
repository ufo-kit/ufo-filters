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
\operatorname{Re}\!\left(F_1(\mathbf{k})\overline{F_2(\mathbf{k})}\right),
$$

$$
P_{1,b}=\sum_{\mathbf{k}\in S_b}|F_1(\mathbf{k})|^2,
\qquad
P_{2,b}=\sum_{\mathbf{k}\in S_b}|F_2(\mathbf{k})|^2.
$$

The FSC curve is

$$
\operatorname{FSC}_b =
\frac{C_b}{\sqrt{P_{1,b}P_{2,b}}}.
$$

**Main idea.** FSC is the normalized dot product of the two complex spectra within a shell. The
normalization removes their absolute amplitude, while the numerator retains only the aligned common
component.

Bins with $n_b=0$, zero power, or a non-finite denominator are invalid and must not silently
produce a plausible number. The benchmark returns `NaN` for these bins and does not clamp valid FSC
values.

The initial result contract is

```text
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
\operatorname{Re}\!\left(A\overline{B}\right)=ac+bd.
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
\operatorname{Re}\!\left(
F_{N_1}(\mathbf{k})\overline{F_{N_2}(\mathbf{k})}
\right)
\right]\approx 0.
$$

The corresponding signal-noise conditions are

$$
\mathbb{E}\!\left[
\sum_{\mathbf{k}\in S_b}
\operatorname{Re}\!\left(
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
\operatorname{SSNR}_b=\frac{\lambda_b^2}{\sigma_b^2},
\qquad
\operatorname{EFSC}_b=
\frac{\operatorname{SSNR}_b}{1+\operatorname{SSNR}_b}.
$$

Equivalently,

$$
\operatorname{SSNR}_b=
\frac{\operatorname{EFSC}_b}{1-\operatorname{EFSC}_b}.
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
6. Normalize the accumulated statistics to obtain $\operatorname{FSC}_b$.
7. Transfer only the compact result vectors to the host and attach physical-frequency and count
   types.

In pseudocode:

```text
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
\widehat{\operatorname{FSC}}_b=
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
\widehat{\operatorname{FSC}}_{\mathrm{global},b}=
\frac{1}{3}\sum_{d\in\{z,x,y\}}
\widehat{\operatorname{FSC}}_{d,b},
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
\operatorname{Cov}(N_w)\approx I.
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
\operatorname{pad}_{0}\left(\mathcal{F}_3(V_w),2N_z,2N_x,2N_y\right)
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
\widehat{\operatorname{FSC}}_b=
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

```text
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

```text
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

```text
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

```text
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

```text
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

### 2.7 Why the generic reduction tasks are not the shell reducer

The task named `reduce` does not run an arbitrary OpenCL kernel. It is a fixed CPU processor that
replaces each 2-by-2 input block with its sum and halves both 2-D output dimensions. It is unrelated
to radial shell accumulation.

The task relevant to arbitrary OpenCL code is `opencl-reduce`.
`opencl-reduce` folds a *stream* of equal-shaped buffers element by element. Its processing kernel has
exactly two global-float arguments, input and output; its output requisition is copied unchanged from
the input. An optional finish kernel operates once on that same output shape.

This is useful for operations such as summing multiple images or averaging a stream. It cannot
directly implement FSC shell reduction because FSC needs all of the following:

- two complex spectra rather than one arbitrary float input and an equal-shaped accumulator;
- a mapping from 3-D coordinates to radial bins;
- a changed output shape from a 3-D spectrum to $B$ shell values;
- several coupled accumulators: cross-power, two auto-powers, and counts;
- task state that pairs consecutive spectra and resets between volume pairs.

A kernel supplied to the shape-preserving `opencl` task can prototype whitening or a phase ramp, but
it has the same requisition-change limitation. Extending either generic task until it understands FSC
would make it less generic and would still leave pairing and metadata awkward. A dedicated
shell-statistics task is the clearer boundary.

`opencl-reduce` may still be useful downstream if several already compact, equal-shaped curves need a
device-side elementwise average. For three vectors of a few hundred values, however, doing that final
average in tofu is unlikely to affect runtime materially.

### 2.8 Shell-reduction design

`fsc-core` uses a staged reduction selected for OpenCL 1.2 portability and bounded auxiliary memory:

- summation order affects floating-point reproducibility;
- each work-group accumulates a complete shell histogram in local memory using compare-and-swap
  float additions and integer counts;
- work-groups write compact partial histograms, which a second kernel reduces into the output;
- device and kernel limits select work-group count and size automatically;
- the task rejects a bin count whose local histogram does not fit device local memory;
- Hermitian symmetry permits a half-spectrum optimization only if conjugate weights and
  `n_shell` semantics are handled consistently;
- `fftshift` should be avoided as a full-volume data movement;
- the task must initialize recycled UFO output buffers explicitly and must not retain an input
  `UfoBuffer` after `process` returns;
- the first spectrum is copied into a task-owned device buffer rather than retaining a borrowed
  `UfoBuffer`;
- graph expansion is rejected because it could separate pair members or duplicate pairing state.

The first implementation retains the full complex first spectrum, computes shell membership on the
fly, and avoids a full shell-index allocation. Alternative reduction strategies remain candidates
for later performance comparisons rather than public modes of the initial task.

### 2.9 Python/tofu responsibility and typed result

Tofu is the natural orchestration layer because it already builds UFO graphs from Python and consumes
`Ufo.OutputTask` buffers as NumPy arrays. Its responsibilities should be:

- choose classic, basic-SFSC, or corrected-SFSC configuration;
- wire the appropriate reconstruction and preprocessing branches;
- provide voxel spacing, binning policy, and any noise-estimation input or mask;
- collect only compact device results through an output task;
- construct the public typed result and preserve method/axis metadata;
- compare curves, record timing and memory measurements, and later apply resolution criteria.

A conceptual host result is

```text
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

### 2.10 Runtime and memory implications at $512^3$

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

### 2.11 Decisions established by this methodology

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

### 2.12 Questions intentionally left for later SFSC planning and validation

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

## 3. References

1. Eric Verbeke *et al.*, “Self Fourier shell correlation: properties and application to cryo-ET,”
   *Communications Biology* 7, 101 (2024),
   [doi:10.1038/s42003-023-05724-y](https://doi.org/10.1038/s42003-023-05724-y).
2. Authors' reference implementation,
   [`self_fourier_shell_correlation`](https://github.com/EricVerbeke/self_fourier_shell_correlation).
