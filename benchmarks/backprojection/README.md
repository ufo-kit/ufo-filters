# Backprojection benchmarks

This directory benchmarks `general-backproject` against singular and dual-volume
`rgba-backproject`, and compares the two temporary RGBA even/odd implementations. It constructs
`read → backproject → null` graphs through Python, saves every UFO trace, and reports medians with
unscaled median absolute deviations.

## Benchmark summary

All campaigns used cubic reconstruction sizes from `32³` through `512³`. The following values
are the logical float32 output payloads; they are useful lower bounds, not measured peak GPU memory.
A dual even/odd reconstruction contains two such volumes.

| Volume shape | One float32 volume | Two float32 volumes |
|---|---:|---:|
| `32³` | 0.125 MiB | 0.25 MiB |
| `64³` | 1 MiB | 2 MiB |
| `128³` | 8 MiB | 16 MiB |
| `256³` | 64 MiB | 128 MiB |
| `512³` | 512 MiB | 1,024 MiB |

| Benchmark suite | Compared workloads | Sizes and burst settings | Output payload over the size range | Key question |
|---|---|---|---|---|
| General vs RGBA singular | General Backproject vs RGBA singular; one volume each | `32³`–`512³`; bursts 8, 16, 24, 32, and 64 | 0.125–512 MiB per workload | Is the specialized RGBA path faster and more memory-efficient than General for the same single-volume reconstruction? |
| RGBA even/odd strategies | `even_odd_single` vs `even_odd_dual`; two volumes each | `32³`–`512³`; bursts 8, 16, 24, 32, and 64 per parity | 0.25–1,024 MiB per workload | Is one parity-aware kernel launch or two simpler parity-specific launches more efficient? |
| General vs RGBA dual | General producing one volume vs RGBA dual producing even and odd volumes | `32³`–`512³`; bursts 8, 16, 24, 32, and 64 | General: 0.125–512 MiB; RGBA: 0.25–1,024 MiB | Can RGBA reconstruct both FSC input volumes in comparable time and memory to General reconstructing one volume? |
| UFO vs ASTRA whole-dataset | General singular, RGBA singular, RGBA dual, and ASTRA `BP3D_CUDA` | `32³`–`512³`; UFO burst fixed at 16; ASTRA has no burst | Single-volume methods: 0.125–512 MiB; RGBA dual: 0.25–1,024 MiB | How do the online UFO methods compare with ASTRA's optimized offline, whole-dataset reconstruction? |
| UFO vs ASTRA incremental | General singular, RGBA singular, RGBA dual, and ASTRA `experimental.accumulate_BP` | `32³`–`512³`; burst fixed at 16 | Single-volume methods: 0.125–512 MiB; RGBA dual: 0.25–1,024 MiB | How do the UFO methods compare with ASTRA when ASTRA also consumes projections incrementally in acquisition-order bursts? |

Actual peak device-memory footprint is larger than the output payload because it includes projection
storage, coalesced accumulators, framework and driver allocations, and temporary working memory.
Where DCGM monitoring was enabled, the measured absolute and baseline-subtracted peaks are recorded
in each campaign's `results/memory-summaries.csv`; the interpretation caveats are described below.

### Campaign methodology

Each exact algorithm, output-size, and burst configuration was executed once as a discarded warm-up,
followed by 10 measured runs. Within each output-size and burst block, the algorithm order was
randomized using the campaign seed to reduce systematic bias from GPU temperature, clock changes,
and execution order. Warm-ups, failed runs, and incomplete configurations were retained in the run
records but excluded from aggregate results; no best-run selection was used.

Reported timing and memory values are the median of the successful measured runs rather than their
arithmetic mean. Variability is reported as the unscaled median absolute deviation (MAD). Every run
has its own directory and manifest containing the resolved workload, execution order, environment,
status, and raw profiler or DCGM data where applicable. UFO-only campaigns retained both scheduler
and profiled-kernel measurements. Cross-framework campaigns used a common host-visible completion
boundary and omitted kernel-level comparisons. Dataset loading and explicitly documented layout or
burst preparation were performed before timing.

Install UFO and the filters, copy `config.example.json` if workload paths need changing, and run
either suite:

```sh
ninja -C build
ninja -C build install
python3 benchmarks/backprojection/run_general_vs_rgba.py --config benchmarks/backprojection/config.example.json
python3 benchmarks/backprojection/run_even_odd.py --config benchmarks/backprojection/config.example.json
python3 benchmarks/backprojection/run_general_vs_rgba_dual.py --config benchmarks/backprojection/config.example.json
```

The runners derive their plugin and kernel locations from the active UFO installation; benchmark
configuration has no source- or build-tree path settings. `PKG_CONFIG_PATH` selects the `ufo.pc` file,
while `LD_LIBRARY_PATH` and `GI_TYPELIB_PATH` must select the matching library and introspection data.
Check the active installation with:

```sh
pkg-config --variable=prefix ufo
pkg-config --variable=plugindir ufo
pkg-config --variable=kerneldir ufo
```

Before a campaign starts, the required installed plugins and kernel files are validated. The kernels
are then linked from the installed `kerneldir` into each run directory so UFO uses the inspected
sources and the manifests can retain their SHA-256 hashes. The resolved installation directories,
plugin binaries, kernel sources, and relevant environment variables are recorded in campaign
metadata.

The third suite compares General's completed `Z`-slice volume against RGBA dual's completed `2Z`
even/odd output using identical configured burst values. RGBA therefore collects `2 × burst`
incoming projections per combined parity batch.

The default matrix performs 550 graph executions per suite. A quick framework check can use:

```sh
python3 benchmarks/backprojection/run_even_odd.py --dry-run --shapes 32 --bursts 24 --runs 1
```

Use `--resume CAMPAIGN_DIR` to retry unfinished configurations. Rebuild tables and figures with
`python3 benchmarks/backprojection/analyze_results.py CAMPAIGN_DIR`. PyGObject and UFO must come from
the system installation; plotting additionally needs the packages in `requirements.txt`.

Edit `colors.example.json` to configure algorithm and stage colors. Both plotting scripts always
load this file automatically. Plots are emitted as PNG only.

Generate the optional 3D burst × output-size view for any summary metric with:

```sh
python3 benchmarks/backprojection/plot_results_3d.py CAMPAIGN_DIR \
  --metric total_profiled_kernel_ms --elev 24 --azim -55

python3 benchmarks/backprojection/plot_results_3d.py CAMPAIGN_DIR \
  --metric output_completion_span_ms --elev 24 --azim -55
```

The 3D script uses configured colors exactly by default. It also accepts `--shade` to apply lighting,
`--log-time`, and `--output`.

## Peak device memory

NVIDIA DCGM monitoring is optional. Start the standalone `nv-hostengine`, ensure `dcgmi discovery`
lists the selected GPU, and add `--dcgm-memory` to a new campaign command. The equivalent JSON
setting is `"dcgm_memory_enabled": true`; the default sampling interval is 50 ms. If the DCGM
bindings are outside a standard installation path, set `dcgm_bindings_path`. A null `dcgm_gpu_id`
uses the configured UFO device index; set it explicitly if DCGM numbers the device differently.
Monitoring is strict: a bindings, host-engine, GPU, or field error stops the campaign before
benchmark runs begin.

The pre-run baseline is the device memory in use immediately before `scheduler.run`. The absolute
peak includes the persistent OpenCL context, program cache, driver allocations, and any other users
of the device. The peak delta subtracts the baseline and is normally the more useful value for
comparing algorithms. Both values are sampled, device-wide framebuffer usage rather than exact
per-allocation accounting, so the selected GPU must remain exclusive to the campaign. A 50 ms
interval can miss a very short-lived allocation, although reconstruction buffers normally persist
long enough to be observed. Do not combine memory statistics from DCGM-monitored and unmonitored
campaigns.

Memory samples are retained in each run's `dcgm-memory.json`; aggregate values are written to
`memory-summaries.csv`. The normal plotting command creates absolute and baseline-delta charts.
The 3D utility accepts `peak_device_memory_mib` and `peak_device_memory_delta_mib` as metrics.

The kernel charts contain only commands submitted through UFO's profiler. In particular, General's
buffer-to-image copies and RGBA's ring-buffer and final slice copies are not included in profiled
kernel totals. Scheduler time is retained as pipeline context, not as an isolated task measurement.

## Full-detector UFO versus ASTRA

The separate cross-framework campaign compares General, singular RGBA, dual-volume RGBA, and
ASTRA `BP3D_CUDA` while keeping all 3001 complete `1024×1024` projections in the workload. UFO uses
burst 16; ASTRA has no burst setting. Install `astra-toolbox` and `tifffile` in the Python environment
that provides PyGObject/UFO, then run:

```sh
python3 benchmarks/backprojection/run_ufo_vs_astra.py \
  --config benchmarks/backprojection/cross-framework-config.example.json
```

The runner loads the TIFF once before timing and retains contiguous `[P,H,W]` and `[H,P,W]` host
layouts for UFO and ASTRA. This needs roughly 23.5 GiB for the default input layouts, plus outputs
and a configurable safety reserve. It fails before allocation if Linux reports insufficient available
host memory. The full ASTRA projection set is about 11.74 GiB, so `BP3D_CUDA` is expected to use its
automatic GPU splitting on a 12 GiB device.

The reported completion interval starts with host-resident projections and ends when all output data
is host-accessible. It includes framework setup, required transfers, and reconstruction, but excludes
TIFF loading and the layout transpose. General and singular RGBA retain their existing angular
normalization; dual RGBA and ASTRA are intentionally unnormalized. The matched single-volume plot
therefore compares General, singular RGBA, and ASTRA. The all-workloads plot additionally shows RGBA
dual, clearly labeled as producing two volumes.

The campaign supports the usual `--shapes`, `--runs`, `--seed`, `--resume`, `--dry-run`,
`--dcgm-memory`, and `--no-plots` options. Its 2D figures are generated automatically. Create the
optional 3D shape × algorithm view with:

```sh
python3 benchmarks/backprojection/plot_cross_framework_3d.py CAMPAIGN_DIR \
  --metric completion_time_ms --elev 24 --azim -55

python3 benchmarks/backprojection/plot_cross_framework_3d.py CAMPAIGN_DIR \
  --metric peak_device_memory_delta_mib
```

Rebuild its CSV summaries and 2D figures without rerunning reconstruction with:

```sh
python3 benchmarks/backprojection/analyze_cross_framework.py CAMPAIGN_DIR
```

Both plotting utilities always read `colors.example.json`; edit its algorithm colors to change the
figures. Only PNG files are produced. DCGM memory values have the same device-wide sampled semantics
described above.

## Online UFO versus incremental ASTRA

The final online-style campaign compares General, singular RGBA, dual-volume RGBA, and one-volume
ASTRA `experimental.accumulate_BP`, all with burst 16. It is separate from the whole-dataset
`BP3D_CUDA` campaign above, which remains the offline reference. Install the CuPy package matching
the CUDA runtime (the dependency file selects `cupy-cuda12x`), then run:

```sh
python3 benchmarks/backprojection/run_online_ufo_vs_astra.py \
  --config benchmarks/backprojection/online-cross-framework-config.example.json
```

Before timing, the runner loads the TIFF and packs 188 contiguous ASTRA arrays in acquisition order:
187 arrays contain 16 projections and the final array contains nine. This packing time is recorded
but excluded, as is the existing ASTRA full-layout conversion. The staged arrays replace the offline
campaign's `[H,P,W]` copy, so the default campaign still needs approximately 23.5 GiB for its two
host projection layouts, plus outputs and the configured reserve.

Static burst geometries and `cuda3d` projectors are also prepared outside timing. Each measured ASTRA
interval includes allocation of a zeroed GPU-linked CuPy accumulator, linking and processing every
host burst with its exact global angles, required transfers, one blocking final download, and the
time until the complete NumPy volume is available. The output remains unnormalized. The experimental
API is checked by a discarded synthetic comparison against `BP3D_CUDA` before campaign execution.

The full projection stream is already resident in host memory to make repeated measurements
deterministic, but ASTRA consumes it strictly in burst order. Camera arrival delays, TIFF I/O, and
burst packing are not part of completion time. Automatic splitting is intentionally unnecessary:
the largest GPU-linked accumulator is 512 MiB and each full input burst is 64 MiB before ASTRA's
transient allocations.

The command accepts the same `--shapes`, `--runs`, `--seed`, `--resume`, `--dry-run`, `--dcgm-memory`,
and `--no-plots` options as the offline campaign. Rebuild results and plots with:

```sh
python3 benchmarks/backprojection/analyze_online_cross_framework.py CAMPAIGN_DIR

python3 benchmarks/backprojection/plot_cross_framework_3d.py CAMPAIGN_DIR \
  --metric completion_time_ms --elev 24 --azim -55
```

The matched plot compares the three single-volume methods. The contextual plot additionally includes
RGBA dual and states that it produces two volumes. Edit `colors.example.json` to configure all plot
colors.

## Validate RGBA slice and volume output

`validate_rgba_output_modes.py` is a manual real-data check, not a timing benchmark. It reconstructs
the same data using both output representations, writes comparable TIFF page stacks, and checks
their values and ordering. It uses `build/src` and `src/kernels` by default so the source kernel wins
over a stale installed copy.

```sh
python3 benchmarks/backprojection/validate_rgba_output_modes.py \
  --input /path/to/fltfc.tiff \
  --num-projections 3001 --burst 16 \
  --center-x 540.4 --center-z 512 \
  --region -99.5 156.5 1 \
  --x-region -256.4 255.4 1 \
  --y-region -256.4 255.4 1 \
  --operation-mode even_odd_dual --fft-smoke
```

Select at least five z slices. `--fft-smoke` additionally runs the device-only graph
`rgba-backproject → fft dimensions=3 → null` without inserting `stack`. Supply `--output-dir` to
retain the intermediate TIFF files.
