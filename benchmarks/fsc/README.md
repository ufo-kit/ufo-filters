# Classic FSC functional benchmark

This directory runs the first complete device-resident classic-FSC graph:

```text
read -> rgba-backproject -> fft -> fsc-core -> Ufo.OutputTask
```

`rgba-backproject` emits the even reconstruction followed by the odd reconstruction. Both volumes
remain on the selected device through the three-dimensional FFT and shell reduction. Only the five
compact shell-statistics vectors are transferred to Python.

Build the plugins, copy `config.example.json` if paths or acquisition geometry need changing, and
replace all three `voxel_size_um` values with the physical reconstructed voxel spacing. The example
values of `1.0` are placeholders and must not be interpreted as calibration for the example TIFF.

```sh
ninja -C build
python3 benchmarks/fsc/run_classic_fsc.py \
  --config benchmarks/fsc/config.example.json
```

One run creates:

- `fsc-result.npz`: `fsc`, `k_bin`, `n_shell`, and the three raw shell sums;
- `fsc.png`: a quick-look curve without a resolution threshold;
- `run.json`: resolved configuration, shell geometry, timings, kernel hashes, and optional memory
  measurements.

The runner accepts `even_odd_single` and `even_odd_dual`, defaulting to `even_odd_dual` when the
configuration omits `operation_mode`; `singular` is not a classic-FSC input. It forces one UFO device
and disables graph expansion because `fsc-core` pairs consecutive spectra.

By default, the shell width is the coarsest axial FFT frequency increment and the radial cutoff is
the smallest axial Nyquist frequency. Set positive `shell_width` or `max_frequency` values to
override either choice. With voxel sizes expressed in µm, `k_bin` is expressed in 1/µm.

Set `dcgm_memory_enabled` to `true` to reuse the optional NVIDIA DCGM monitor from the
backprojection benchmarks. DCGM monitoring requires a running host engine and the Python bindings;
it is not needed for ordinary functional runs.

This is a single-run functional and profiling harness. It is not yet a repeated statistical
benchmark campaign and intentionally does not apply an FSC resolution threshold.
