# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

ufo-filters provides the standard plugin library for [UFO](https://github.com/ufo-kit/ufo-core), a
multi-threaded, GPU-enabled, distributed data-processing framework. Each plugin ("task") is a GObject
implementing `UfoTask`/`UfoTaskNode` from ufo-core, typically backed by an OpenCL kernel, and compiled
into its own shared module (`libufofilter<name>.so`) that ufo-core loads by name at pipeline-construction
time. This repo requires a working ufo-core install (`>= 0.16`) discoverable via pkg-config as `ufo`.

## Build system

Both Meson (primary/actively used — a configured `build/` tree already exists) and CMake are supported
in parallel. Keeping them in sync is the main source of accidental breakage; see the checklist under
"Adding things" below for the exact dual-edit rules (they differ per file type — `.cl` kernels in
particular are auto-globbed by CMake but must be listed explicitly for Meson).

### Meson

```sh
meson setup build            # first time only
ninja -C build                # build
ninja -C build test           # run test suite (meson test)
meson test -C build <name>    # run a single test, e.g. test-149, test_fft, test-nlm
ninja -C build install
```

Options (`meson_options.txt`) — these four are the complete set: `-Ddocs=false` (skip Sphinx docs),
`-Doclfft=false`, `-Dcontrib_filters=true` (build `contrib/`),
`-Dlamino_backproject_burst_mode=<1|2|4|8|16>`.

Note the burst-mode option only sets the `BURST` `#define` in `config.h`. `make_burst_kernels.py` is
invoked with a hard-coded `1 2 4 8 16` in `src/kernels/meson.build`, so every burst variant is always
generated into the `.cl` file and the C code selects one *by kernel name* at runtime.

### CMake

```sh
cmake -DCMAKE_INSTALL_PREFIX=/usr .
make
make test
sudo make install
```

`-DWITH_CONTRIB=ON` enables contrib filters, `-DBP_BURST` sets the lamino backprojection burst mode.

### Tests

Tests live in `tests/` as standalone shell scripts (`test-*.sh`) plus a few Python scripts
(`test_*.py`). There is no unit-test framework per se — tests build small UFO pipelines with the
compiled plugins and check output.

Gotchas that will otherwise cost you time:

- **The test names differ between build systems**: Meson keeps dashes (`test-149`), CTest uses
  underscores (`test_149`).
- **`UFO_PLUGIN_PATH` is set only by Meson's `test_env`** (to `<build>/src`, in `tests/meson.build`) —
  the scripts never set it themselves. To run one by hand you must export it, and run from the build
  root, since tests resolve helpers by relative path (`tests/check-gradient`) and write temp files into
  the CWD. Some don't clean up after themselves.
- `test-142` is only registered if `tiffinfo` is found, so the live test count varies by machine.
- External deps are not declared anywhere: Python tests need PyGObject + numpy (`test_memin` also
  needs `pyopencl`), and several shell tests shell out to `python` — not `python3` — with
  numpy/tifffile. `make-input-multipage-readers` needs `h5py`.

## Relationship to ufo-core

The framework contract lives outside this repo, and most of what a filter must not get wrong is
documented only there. ufo-filters compiles against the *installed* headers at
`$(pkg-config --variable=includedir ufo)/ufo-0/ufo/`; a sibling ufo-core source checkout, if present, is
the readable copy of the same thing (verify they match before trusting the checkout).

Worth reading there: `ufo/ufo-task-iface.h` (the vtable), `ufo/ufo-buffer.h` (the API used constantly),
`ufo/ufo-plugin-manager.c` (module→symbol resolution), `ufo/ufo-scheduler.c` + `ufo-task-graph.c`
(expansion/copying), and `docs/manual/devel.rst` (the prose "Developing new task filters" guide — note
it is CMake-era and still references the retired `UfoFilter` API, so don't copy patterns from it
wholesale).

**`pkg-config --modversion ufo` returns the ABI version (e.g. `1.0`), not the API version (`0.17.0`).**
So the `dependency('ufo', version: '>= 0.16')` check in `meson.build` passes trivially and is
effectively vacuous — it can no longer detect an outdated ufo-core. `UFO_VERSION` exists only as a
`config.h` define inside ufo-core. Relatedly, a stale `libufo.so.0` may linger beside the current
`libufo.so.1.0` in an install prefix; built filters should resolve `NEEDED libufo.so.1.0`.

## Architecture

### Plugin anatomy

Every filter is a `.c`/`.h` pair named `ufo-<name>-task.{c,h}` directly under `src/` implementing the
GObject type `UfoNameTask` (boilerplate `G_DEFINE_TYPE_WITH_CODE` implementing the `UfoTask` interface).
The interface methods wired up in `ufo_task_interface_init` are the core contract:

- `setup` — one-time OpenCL kernel/resource acquisition (`ufo_resources_get_kernel`). Called exactly
  once per instance, and the only place with a `UfoResources *`.
- `get_num_inputs` / `get_num_dimensions` — pipeline shape declaration.
- `get_requisition` — computes output buffer size from input(s). Runs on **every** iteration, right
  before `process`, and takes a `GError **` — so it's also where per-iteration validation belongs.
- `get_mode` — see below.
- `process` — consume input buffer(s), produce/accumulate into `output`.
- `generate` — emit buffered/accumulated results (reductor/generator tasks only).
- `set_json_object_property` — JSON-only escape hatch for properties that aren't plain GObject
  properties; no plugin in this repo implements it.

All are technically optional (ufo-core installs warning stubs), but the first five plus `process`
and/or `generate` are mandatory in practice.

#### How ufo-core actually loads a plugin

There is **no registration symbol** — no `G_MODULE_EXPORT`, no module init function. The entire
contract is the name of the exported constructor. `ufo_plugin_manager_get_task()` turns a pipeline name
into a filename `libufofilter<name>.so` plus a symbol `ufo_<name>_task_new` (dashes → underscores),
`g_module_open`s the file and `g_module_symbol`s that exact name, typed `UfoNode *(*)(void)`. So
`general-backproject` → `libufofiltergeneral-backproject.so` + `ufo_general_backproject_task_new`.

Consequences: the exported `_new()` must match the module name exactly (the GType name and `_get_type`
are irrelevant to loading); Meson enforces the filename via `name_prefix: 'libufofilter'` and CMake via
`set(target "ufofilter${task}")`; the repo sets no visibility flags, so the constructor is exported by
default. Plugin *discovery* is a filename glob over the built-in plugindir plus `UFO_PLUGIN_PATH`.

#### Task modes

`get_mode` returns a type bit OR'd with exactly one of `UFO_TASK_MODE_CPU` / `_GPU`:

- **PROCESSOR** — `output` is pushed downstream after each `process`.
- **REDUCTOR** — `output` is **not** pushed after `process`; the *same* buffer persists across calls and
  serves as the accumulator (see `ufo_average_task_process` doing `out[i] += in[i]`). `process` returns
  TRUE to keep consuming; then `generate` is drained, returning TRUE per emitted item.
- **GENERATOR** — no `process` at all; `generate` returns FALSE to end the stream.
- **SINK** — still implements `process`, but `get_requisition` sets `n_dims = 0`.

#### Buffer contract

`UfoBuffer` storage is **always 32-bit float** internally. `UFO_BUFFER_MAX_NDIMS` is 3 and
`UfoRequisition` is `{n_dims, dims[3]}` with `dims[0]` the fastest-varying (width).
`ufo_buffer_get_host_array` / `get_device_array` / `get_device_image` select the location and trigger
transfers; `ufo_buffer_get_requisition` reads a buffer's current size; `UfoBufferDepth` +
`ufo_buffer_convert` expand packed integer input to float; `UfoBufferLayout` (`REAL` /
`COMPLEX_INTERLEAVED`) is how the FFT filters flag complex data.

Three rules that are easy to violate and produce silently wrong results:

- **Output buffers are recycled and contain stale data from a previous iteration.** Never assume zeroed
  memory — a REDUCTOR accumulator must be explicitly initialized.
- The scheduler calls `ufo_buffer_copy_metadata (inputs[i], output)` before `process`, so input
  metadata propagates for free — and metadata set on `output` *before* `process` gets clobbered.
- **Do not retain an input `UfoBuffer` past `process`**; inputs are released back to the producer as
  soon as it returns.

#### Task copying and threading

One OS thread per graph node, so `process`/`generate` for a given *instance* is never concurrent and
instance state needs no locking — file-scope/static state is the hazard.

`ufo_task_graph_expand` duplicates the longest GPU-task path once per GPU and round-robins the copies
onto `UfoGpuNode`s. **Copies are made by GObject property copy**, so anything not exposed as a property
must be rebuilt in an overridden `UFO_NODE_CLASS->copy` (always called before `setup`). Practical rule:
expose everything as a property and derive internals from it. Only tasks whose mode includes `_GPU` are
ever duplicated; a `_CPU` task stays a single instance and becomes the pipeline's serialization point.
`expand` is a property of the *scheduler*, not of a task — there is no per-task opt-out.

This is precisely why the command queue must be fetched inside `process` rather than cached in `setup`:
the `UfoGpuNode` is assigned after the graph is built, so a cached queue can belong to another device.

#### Properties

GObject properties (plugin parameters exposed to UFO pipelines, e.g. via `ufo-launch`/Python) are
declared in a `PROP_*` enum + `properties[]` array of `GParamSpec`s, installed in `_class_init`, and
handled in `_set_property`/`_get_property`. Follow this pattern exactly when adding a new parameter —
it's what makes the property visible to pipeline tooling, and it's what makes it survive task copying.

All tasks here use the legacy private-data idiom (`G_TYPE_INSTANCE_GET_PRIVATE` macro +
`g_type_class_add_private` in `_class_init`) rather than `G_ADD_PRIVATE`. Match it; don't modernize one
file in isolation. Headers are hand-written boilerplate including only `<ufo/ufo.h>`.

Never `exit()`/`abort()` from a filter — use
`g_set_error (error, UFO_TASK_ERROR, UFO_TASK_ERROR_SETUP, ...)`; these surface as Python exceptions.

### OpenCL kernels

`.cl` kernel source lives in `src/kernels/` and is installed to `kerneldir` (queried from ufo-core's
pkg-config) rather than compiled at build time; plugins load kernel source at runtime via
`ufo_resources_get_kernel`/`ufo_resources_get_kernel_source`.

Conventions:

- Canonical `process` preamble, fetched **per call**: `ufo_task_node_get_proc_node` →
  `ufo_gpu_node_get_cmd_queue` → `ufo_buffer_get_device_array` → `ufo_task_node_get_profiler`.
- Enqueue via `ufo_profiler_call (profiler, queue, kernel, work_dim, global, local)`, never
  `clEnqueueNDRangeKernel` directly — the profiler wrapper is what makes kernels show up in traces.
- **`ufo_resources_get_kernel` returns a resources-owned kernel.** The house pattern is
  `if (kernel != NULL) UFO_RESOURCES_CHECK_SET_AND_RETURN (clRetainKernel (kernel), error);` in `setup`,
  with a matching `clReleaseKernel` in `finalize`. Getting this wrong is a leak or a use-after-free.
- Error macros: `UFO_RESOURCES_CHECK_CLERR` (logs) vs `UFO_RESOURCES_CHECK_SET_AND_RETURN` (sets
  `GError`; `setup`-only, as it returns void).

Some kernels are generated from templates in `src/kernels/templates/*.in`. Two different mechanisms:
`general-backproject` assembles source **in C at setup time** (concatenating
`general_bp_definitions.in` + a scalar-or-vector header + body, then splitting on `%tmpl%` markers to
interleave generated fragments), while `lamino-backproject` consumes `.cl` files **generated at build
time** by `src/kernels/tools/make_burst_kernels.py` and just selects a kernel by name.

### Shared/common code

`src/common/` holds code shared across multiple plugins (not itself a plugin): `ufo-math.c` (vector/geometry
math), `ufo-fft.c` (FFT dependency abstraction over clFFT or Apple's oclFFT in `deps/oclfft`),
`ufo-conebeam.c`/`ufo-ctgeometry.c` (cone-beam/CT geometry for `general-backproject`), `ufo-scarray.c`
("sparse"/scalar-or-vector array abstraction used for time-varying geometry parameters), `ufo-common.c`
(misc helpers e.g. noise estimation), `hdf5.c` (optional HDF5 helpers gated by `WITH_HDF5`). Link a
plugin against the specific common files it needs rather than pulling in all of `common/`.
`ufo-addressing.h` and `ufo-interpolation.h` are header-only `GEnumValue` tables — including them
requires no linking.

### Readers / writers

`src/readers/` (`ufo-reader.c` + EDF/RAW/TIFF/HDF5 backends) and `src/writers/` (`ufo-writer.c` +
RAW/TIFF/JPEG/HDF5 backends) implement the I/O backends used by the generic `ufo-read-task` and
`ufo-write-task` plugins. Both are GObject *interfaces* with a `can_open`/`open`/`close`/... vtable; the
generic task instantiates every available backend and picks per-file by trying `can_open` in a fixed
priority order, overridable via the `type` property.

Build-time gating uses inconsistent macro names — **`HAVE_TIFF` and `HAVE_JPEG` but `WITH_HDF5`** —
emitted through `src/config.h.meson.in` (`#mesondefine`) and `src/config.h.in` (`#cmakedefine`).
TIFF/JPEG are auto-detected only; HDF5 is a user-facing toggle. When adding a format, note that the
`type` GEnum and its `type_values[]` array in `ufo-read-task.c` are `#ifdef`-gated in parallel and carry
an in-source "keep enum and values array in sync!" warning.

### Backprojection family

There are several related but distinct backprojection plugins — don't conflate them:
`ufo-backproject-task.c` (basic 2D), `ufo-general-backproject-task.c` (general cone-beam/laminography,
template-generated kernels, scalar or per-projection vector geometry via `ufo-scarray`),
`ufo-lamino-backproject-task.c` (laminography-specific, Python-assisted kernel generation, burst mode),
`ufo-stacked-backproject-task.c`, and `ufo-rgba-backproject-task.c`. `lamino-roi.c`/`.h` supports ROI
handling shared by laminography reconstruction.

### contrib/

Additional filters not part of the core distribution live in `contrib/`, built only when
`-Dcontrib_filters=true` (Meson) / `-DWITH_CONTRIB=ON` (CMake).

## Adding things

New tasks are scaffolded by **`ufo-mkfilter`** (ships with ufo-core):
`ufo-mkfilter AwesomeFoo --type=<processor|generator|reductor|sink> [--use-gpu]` writes
`ufo-awesome-foo-task.{c,h}` into the CWD. There is no template or generator script in this repo.

Registration checklist — each row has a *different* dual-build rule:

- **Task** → `src/meson.build` (the generic `plugins` list, or an explicit `shared_module` block if it
  needs extra sources or an optional dep) **and** `src/CMakeLists.txt` (`ufofilter_SRCS`, plus a
  `<name>_aux_SRCS` entry if Meson pulls in any `common/*.c`) → a `.. gobj:class::` entry in the right
  `docs/*.rst`.
- **Kernel** → CMake `file(GLOB "*.cl")`s automatically, but **Meson needs an explicit entry in
  `kernel_files`** in `src/kernels/meson.build`. This asymmetry is the easiest drift to introduce,
  because the CMake build keeps working.
- **New `config.h` define** → both `src/config.h.meson.in` (`#mesondefine`) and `src/config.h.in`
  (`#cmakedefine`).
- **Test** → both `tests/meson.build` and `tests/CMakeLists.txt`.

## Running and debugging pipelines

`ufo-launch` grammar: `[a, b] ! task prop=value ! task2` — `!` chains to input port 0, brackets group
branches and **bracket order determines the input port index**, and comma-separated values fill a
`GParamSpecValueArray` property (e.g. `center=10.0,10.0`). Dashes and underscores in property names are
equivalent; docs use dashes.

Two silent footguns: an unknown property name only emits a `g_warning` and the pipeline runs anyway;
and for booleans only a case-insensitive `true` prefix is TRUE — **`1` and `yes` are FALSE**.

Environment variables ufo-core honors (this is the complete list):

- `UFO_PLUGIN_PATH` — colon-separated, *prepended* before the installed plugin dir. This is what makes
  an uninstalled build testable.
- `UFO_KERNEL_PATH` — colon-separated, but **appended last**. Search order is `.` → the installed
  kernel dir → `UFO_KERNEL_PATH`. **This is a trap**: if a `.cl` has ever been installed, that stale
  copy wins over your edited `src/kernels/` even with `UFO_KERNEL_PATH` set, so you silently run the
  *old kernel* against *new host code* — which looks like a logic bug, not a stale file. Symptoms are
  wrong results plus `CL_INVALID_ARG_INDEX` criticals if you added kernel arguments. Work around it by
  running from `src/kernels/` (`.` is searched first) or reinstalling; verify with
  `diff <installed>/rgba-backproject.cl src/kernels/rgba-backproject.cl`.
- `UFO_DEVICES` — comma-separated OpenCL device indices.
- `UFO_DEVICE_TYPE` — `cpu`, `gpu`, `acc`; defaults to GPU only. This is how CI runs headless.

Debug logging: ufo-core's meson build does not set `G_LOG_DOMAIN`, so use `G_MESSAGES_DEBUG=all` —
`G_MESSAGES_DEBUG=Ufo` will not work.

Profiling: `ufo-launch -t` writes Chrome-trace `opencl.<PID>.json` and `trace.<PID>.json` into the CWD;
view with the shipped `ufo-prof` or `chrome://tracing`. Per-kernel timings only appear for kernels
launched through `ufo_profiler_call`. `ufo-launch -d FILE` dumps the graph to JSON instead of running.

## Conventions

- Commit style on `master` is `component: lowercase imperative`
  (e.g. `retrieve-phase: fix uninitialized lambda`). Some feature branches use conventional-commit
  `feat:`/`chore:` prefixes instead — match whatever the current branch is already doing.
- **No linter or formatter exists** — no `.clang-format`, `.clang-tidy`, `.editorconfig`. Style is
  enforced only by compiler flags (`-Wall -Wextra -pedantic -std=gnu99` under CMake). Match the
  surrounding file.
- CI is a single stale, **CMake-only** `.travis.yml`. Meson — the build actually in use — is never
  exercised in CI, so local `ninja -C build test` is the only real gate.

## Docs

Sphinx docs live in `docs/` (`filters.rst`, `kernels.rst`, `generators.rst`, `sinks.rst`, etc. — one page
per task category) and are built via `docs/meson.build`/`docs/CMakeLists.txt` when the `docs` option is
enabled; hosted at ufo-filters.readthedocs.io. `docs/sphinxgobject/` is a small custom Sphinx extension
for documenting the GObject-introspected plugin properties. Note `docs/install.rst` is somewhat stale —
it documents the old `meson build` invocation and omits all four meson options.
