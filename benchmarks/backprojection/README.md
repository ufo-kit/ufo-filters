# Backprojection benchmarks

This directory contains three GPU backprojection campaigns:

| Campaign | Workloads | Key question |
|---|---|---|
| General vs RGBA singular | One General volume and one RGBA singular volume | How much does RGBA specialization improve one-volume online reconstruction? |
| General vs RGBA even/odd | One General volume and two RGBA parity volumes | Can both FSC input volumes be reconstructed in time comparable to one General volume? |
| UFO vs ASTRA experimental | General, RGBA singular, RGBA even/odd, and ASTRA `experimental.accumulate_BP` | How do the UFO methods compare with ASTRA when every implementation accumulates projections incrementally? |

The maintained server configuration uses 3001 float32 projections of `2016 × 2016`, cubic outputs
`256³`, `512³`, and `1024³`, and bursts 16, 32, and 64. One float32 volume occupies 64 MiB,
512 MiB, or 4 GiB respectively; RGBA even/odd produces two volumes. These payload sizes are lower
bounds rather than measured GPU peaks.

## Campaign method

Each exact shape, burst, and algorithm configuration receives one discarded warm-up and ten
measured runs. Algorithm order is shuffled reproducibly within every block. Results use the median
and unscaled median absolute deviation (MAD); warm-ups, failures, and incomplete blocks remain in
the manifests but are excluded from summaries. No best-run selection is performed.

For UFO-only campaigns, the primary latency is `output_completion_span_ms`: the interval from the
first backproject task `process` call through completion of the final null-sink call. The sink calls
`clFinish`, so queued reconstruction and output work is complete. This avoids graph-wide startup,
reader lifecycle, and teardown included by scheduler time. Runner wall time remains only in each run
manifest as a diagnostic.

UFO-only runs also retain exact profiled-kernel totals and stage summaries. Kernel-stage figures show
projection packing, backprojection, and distribution, but not unprofiled OpenCL copies. The
cross-framework campaign instead reports a common host-visible `completion_time_ms` and does not
compare kernel internals.

## Configuration and execution

Campaign runners accept only `--config`; edit the JSON rather than supplying workload overrides.
The UFO-only campaigns share `config.example.json`:

```sh
python3 benchmarks/backprojection/run_general_vs_rgba.py \
  --config benchmarks/backprojection/config.example.json

python3 benchmarks/backprojection/run_general_vs_rgba_even_odd.py \
  --config benchmarks/backprojection/config.example.json
```

Run the cross-framework campaign with:

```sh
python3 benchmarks/backprojection/run_ufo_vs_astra_experimental.py \
  --config benchmarks/backprojection/astra-experimental-config.example.json
```

Important JSON controls include:

- `campaign_dir`: null creates a timestamped directory below `output_root`; set an explicit path for
  a fixed new campaign or any resume.
- `resume`: resume the campaign named by `campaign_dir`.
- `dry_run`: print the resolved schedule without loading data or running a graph.
- `generate_plots`: generate PNG figures after aggregation.
- `scaling_plot_orientation`: `row` (default) or `column` for the three burst subplots.
- `dcgm_memory_enabled` and the remaining `dcgm_*` fields: optional strict memory monitoring.

The default schedules contain 198 executions for each two-algorithm UFO campaign and 396 executions
for the four-algorithm ASTRA campaign. Rebuild an existing campaign's summaries and figures with:

```sh
python3 benchmarks/backprojection/analyze_results.py CAMPAIGN_DIR
python3 benchmarks/backprojection/analyze_ufo_vs_astra_experimental.py CAMPAIGN_DIR
```

Edit `colors.example.json` to change algorithm and kernel-stage colors. Plotters always load that
file and emit PNG only. The maintained figures are per-shape completion bars, completion scaling,
UFO kernel stages, and optional absolute peak-memory bars.

## Installed UFO resources

Runners use the active installed UFO selected by `pkg-config`; no source or build directory is
required. `PKG_CONFIG_PATH` selects `ufo.pc`, while `LD_LIBRARY_PATH` and `GI_TYPELIB_PATH` must select
the matching libraries and introspection data. Diagnose the selection with:

```sh
pkg-config --variable=prefix ufo
pkg-config --variable=plugindir ufo
pkg-config --variable=kerneldir ufo
```

Required plugins and kernels are validated before execution. Installed kernels are linked into each
run directory so the exact source selected by UFO can be hashed and recorded with plugin binaries,
GPU/runtime identifiers, configuration, ordering, and failures.

## Incremental ASTRA input path

The ASTRA campaign loads the TIFF once as contiguous UFO data `[projection, detector-row,
detector-column]`. For one burst setting at a time, consecutive ranges are transposed and copied into
contiguous ASTRA arrays `[detector-row, burst-angle, detector-column]`. With 3001 projections this
produces:

| Burst | Groups | Tail |
|---:|---:|---:|
| 16 | 188 | 9 projections |
| 32 | 94 | 25 projections |
| 64 | 47 | 57 projections |

TIFF loading and burst packing are recorded but excluded from reconstruction timing. The original
`[P,H,W]` dataset occupies approximately 45.44 GiB of host RAM. For one selected burst size, the
complete dataset is reorganized into a list of contiguous host arrays; the list's combined size is
another approximately 45.44 GiB. That full burst-organized host copy is released before the next
burst size is prepared, so the three settings do not retain three such copies simultaneously. It is
not a 45.44 GiB GPU allocation: for example, one burst-16 array is approximately 248 MiB and is
linked/submitted to ASTRA for an additive update. Exact global angles, `parallel3d` geometry, and
`cuda3d` projectors are created outside timing for the current shape/burst block.

During a measured ASTRA run, the runner allocates one zeroed `N³` float32 CuPy accumulator and links
it to ASTRA. Each host burst is linked and passed to `experimental.accumulate_BP` in acquisition
order. Linked inputs remain alive until CUDA synchronization, after which the completed accumulator
is downloaded once to NumPy. Allocation, linking, host-to-device work, all additive backprojections,
synchronization, and final download are timed.

This is methodologically close to UFO because every method incrementally adds angular subsets into
persistent GPU reconstruction storage. The input interfaces still differ: General and RGBA receive
individual `[H,W]` frames and form internal batches, while ASTRA receives an already contiguous
`[H,B,W]` group per call. General and ASTRA process `B` projections per batch; RGBA even/odd defines
`B` per parity and therefore processes up to `2B` consecutive projections in a combined batch.
Acquisition delays are excluded. `BP3D_CUDA` is used only by a discarded synthetic correctness check
for the experimental API and is never a measured workload.

## Peak device memory

DCGM monitoring is optional and requires a running standalone `nv-hostengine`. When enabled, each
run records the device-wide framebuffer baseline, raw timestamped samples, and absolute peak in MiB.
Only absolute peak memory is aggregated and plotted.

The absolute peak includes the persistent OpenCL/CUDA context, program and projector state, driver
allocations, and any other users of the device. Keep the selected GPU exclusive and its baseline
stable during a campaign. The default 50 ms sampling interval can miss very short-lived allocations,
although reconstruction buffers generally persist long enough to be observed. Do not combine memory
statistics from monitored and unmonitored campaigns.

## RGBA output validation

`validate_rgba_output_modes.py` remains a manual real-data correctness utility rather than a timing
benchmark. It accepts `singular` or `even_odd` and can compare slice and volume output or run the
device-only three-dimensional FFT smoke graph.
