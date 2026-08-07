# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

`vcell-ode` is the ODE/DAE numerical solver used by the Virtual Cell framework (https://github.com/virtualcell/vcell). It produces two artifacts from one source tree:

- **A standalone CLI** (`SundialsSolverStandalone_x64`) that VCell launches as a subprocess against a `.cvodeInput` file.
- **A Python wheel** (`pyvcell_odesolver`) exposing the same `solve()` entry through a pybind11 module (`pyvcell_odesolver._core`).

The numerical core is CVODE/IDA from a vendored copy of `sundials/`, wrapped by `IDAWin/`, with user rate laws evaluated via the `vcell-expressionparser` submodule. Optional progress messaging (libcurl → ActiveMQ over the JMS REST bridge) is provided by the `vcell-messaging` submodule.

## Build

CMake-driven. Conan 2.x manages dependencies (`argparse`, `spdlog`, optionally `libcurl`); pybind11 is vendored at `extern/pybind11/`.

`vcell-expressionparser` and `vcell-messaging` are git submodules — a plain `git clone` leaves both directories empty and CMake fails at `add_subdirectory`. Clone with `--recursive`, or in an existing checkout:

```bash
git submodule update --init --recursive
```

Canonical native build (matches `.github/workflows/cd.yml`):

```bash
conan install . --output-folder build --build=missing
cd build && source conanbuild.sh
cmake -B . -S .. -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
ctest -VV
```

Wheel build (separate scikit-build-core invocation; forces messaging OFF):

```bash
python3 -m build --wheel -o ./wheelhouse \
        -C cmake.args="-DCMAKE_TOOLCHAIN_FILE=$PWD/build/conan_toolchain.cmake" \
        -C cmake.define.OPTION_TARGET_PYTHON_BINDING=ON
```

The complete option set in the root `CMakeLists.txt`:

- `OPTION_TARGET_PYTHON_BINDING` (default OFF) — switches the project to scikit-build-core mode and builds the `_core` pybind11 extension. **Forces `OPTION_TARGET_MESSAGING=OFF`** and skips the `bin/` output-dir setup, so the standalone exe is not produced in this mode.
- `OPTION_TARGET_MESSAGING` (default OFF) — adds `-DUSE_MESSAGING`, links libcurl, and changes the install layout (`OPTION_EXE_DIRECTORY` → `../bin`). Without it, `vcell-messaging` still builds, but the curl path is replaced by `NullCurlProxy` (the `AbstractCurlProxy` polymorphism — not `#ifdef` forests).
- `OPTION_TEST_WITH_LOCALHOST` — adds `-DTEST_WITH_LOCALHOST`; flips `tests/unit/smoke_test.cpp` to the localhost-keyed expected-output baseline.
- `OPTION_STATICALLY_LINK`, `OPTION_TARGET_DOCS`, `OPTION_EXTRA_CONFIG_INFO` — niche flags; rarely flipped.

Conan profiles for CI live under `conan-profiles/CI-CD/`, one per `(platform, arch)`. They pin `compiler.cppstd=20`, `compiler.libcxx=libc++`, and add `mold` for Linux exe linking. For local dev, `conan profile detect --force` works on macOS/Linux as long as you bump `cppstd` to `20` to match the project.

Platform notes (full recipes in `cd.yml`):
- **Linux**: clang + `mold` (linker), `libc++-dev`, `libc++abi-dev`, conan installs everything else.
- **macOS** (arm64 + x86_64): Homebrew `conan` + `spdlog`. arm64 and x86_64 wheels build separately; a `MacOS-Universal` job `lipo`s the resulting `_x64`/`.dylib` files.
- **Windows**: LLVM toolchain via `llvm/actions/setup-windows`, conan + chocolatey. `OPTION_TARGET_MESSAGING` is OFF on Windows (libcurl path is not built).
- **Docker** (`Dockerfile`): Debian trixie-slim + clang-19 + libc++-19 + mold + ninja + cmake + spdlog from apt; builds with `OPTION_TARGET_MESSAGING=ON` and `OPTION_TEST_WITH_LOCALHOST=ON`. This is the image published to ghcr.io.

## Tests

`ctest -VV` from `build/` runs four C++ tests built into `build/bin/unit_tests`, plus optionally a Python test:

- `SmokeTest.UserProvidesFilesWithoutJMS` / `SmokeTest.UserProvidesFilesWithJMS` (`tests/unit/smoke_test.cpp`) — runs CVODE against `tests/unit/resources/SimID_*_.cvodeInput` and diffs the output against `*.ida.expected` with relative tolerance `1e-7`. The "WithJMS" variant uses `taskID=2025` and selects the `886118677` (localhost) or `256118677` (vcell.cam.uchc.edu) baseline based on `TEST_WITH_LOCALHOST`. Both variants pass with messaging off — the solver no-ops the curl path under `NullCurlProxy`.
- `HelloTest.BasicAssertions` (`tests/unit/hello_test.cpp`) — gtest sanity.
- `MessageProcessingTest.MessagesAreProcessed` (`tests/unit/message_processing_test.cpp`) — exercises `MessageEventManager` directly (worker thread + lock-ordered queue), independent of `OPTION_TARGET_MESSAGING`.
- `python_smoke_test` (`tests/unit/smoke_test.py`) — pytest, exercises `pyvcell_odesolver.solve()` against the same `1489333437` resource the C++ smoke uses. Registration is **skipped at configure time** unless `${PYTHON_TEST_INTERPRETER}` can `import pyvcell_odesolver`. To enable it locally: build a wheel, install it into a venv, then re-run `cmake -DPYTHON_TEST_INTERPRETER=<venv>/bin/python .`. CI's native ctest job uses the system Python so the entry skips silently; the wheel is exercised separately by `cibuildwheel` (which runs `pytest {project}/tests` per `pyproject.toml`, picking up both `tests/test_basic.py` and `tests/unit/smoke_test.py` by default discovery).

Run a single test: `ctest -VV -R <regex>`. Smoke-test inputs live next to the script and are baselined — if you intentionally change numerical output, regenerate the `.expected` files.

`ExpressionParserTest/ExpressionParserTest.cpp` exists but is not currently wired into ctest.

## Produced binaries

In native builds, `build/bin/` contains:
- `SundialsSolverStandalone_x64` — the CLI VCell invokes; `argparse`-driven, takes `<input> <output> [-tid <jobId>]`.
- `unit_tests` — gtest binary with the C++ tests above.

In Python-binding builds, the wheel includes `pyvcell_odesolver/_core.<soabi>.so` (or `.pyd` on Windows). The Python API exposes:
- `pyvcell_odesolver.version() -> str`
- `pyvcell_odesolver.solve(cvode_input_file_path, output_file_path, tid=-1) -> int` — `tid=-1` skips messaging; positive values are JMS job IDs.

## Architecture

Top-level subdirectories and how they fit:

- **`vcell-expressionparser/`** (submodule, target `vcellexpressionparser`) — math expression parser (AST `Node`s, `Expression`, `SymbolTable`, `StackMachine`). Used by `IDAWin` to evaluate user-supplied rate laws, initial conditions, and event/discontinuity expressions. Headers live under `include/`, sources under `src/`. Requires C++20 — the AST nodes build error messages with `std::format`.
- **`sundials/`** (vendored, modified) — CVODE + IDA + nvec_ser. A thin top-level `add_subdirectory` aggregates `sundials_cvode`/`sundials_ida`/`sundials_nvecserial`/`sundials_lib` into the `sundials` interface target.
- **`IDAWin/`** — the solver layer. Decomposed (since the stabilization branch) into:
  - `VCellSolver.h` — abstract base (`configureFromInput`, `solve`).
  - `VCellSolverInput.{h,cpp}` — `VCellSolverInputBreakdown` struct-of-structs holding `ModelSettings`, `TimeCourseSettings`, `SteadyStateSettings`, `DiscontinuitiesSettings`, `EventSettings`.
  - `VCellSolverFactory.{h,cpp}` — parses the `.cvodeInput` file into the breakdown and produces the right `VCellSolver` subclass.
  - `VCellSundialsSolver.{h,cpp}` — common sundials machinery (events, discontinuities, output, stop-checking).
  - `VCellCVodeSolver` / `VCellIDASolver` — concrete subclasses for ODE and DAE.
  - `SundialsSolverInterface.{h,cpp}` — the `solve(input, output, tid)` entry shared by the CLI and the Python module.
  - `SundialsSolverStandalone.cpp` — `argparse`-based `main` that calls `solve()`.
- **`vcell-messaging/`** (submodule, target `vcellmessaging`) — progress messaging. Decomposed into:
  - `SimulationMessaging` — the singleton facade.
  - `MessageEventManager` — owns the worker `std::thread` and the event queue; lock ordering documented in the header (**queue mutex *before* the `stopRequested` mutex** — this is the reverse of the pre-submodule in-tree copy, which deadlocked). Shutdown joins the worker rather than waiting for the queue to drain: an empty queue is not the same as "all work finished", since the worker pops under the lock but sends after releasing it.
  - `CurlProxyClasses` — `AbstractCurlProxy` / `NullCurlProxy` (always built) / `CurlProxy` (only under `USE_MESSAGING`); polymorphism replaces the old `#ifdef` forests.
  - `JobEventStatus` — `JobEvent::Status` namespaced enum + `toString`.
  - `WorkerEvent` — the queue payload.
- **`src/main.cpp`** — pybind11 module (`PYBIND11_MODULE(_core, m)`) exposing `version` and `solve` to Python.
- **`tests/`** — gtest C++ tests, pytest Python tests, and resource files. `RESOURCE_DIR` is passed to the C++ tests via `target_compile_definitions`; the Python test resolves resources via `Path(__file__).parent / "resources"`.
- **`extern/pybind11/`** — vendored pybind11. Used as the fallback when `find_package(pybind11)` doesn't find a system install.
- **`conan-profiles/CI-CD/`** — per-platform Conan profiles; CI copies the matching one to `~/.conan2/profiles/default`.
- **`cmake/modules/`** — `GetGitRevisionDescription.cmake` (used to stamp `g_GIT_DESCRIBE` into `vcell-messaging/src/GitDescribe.cpp`, which the version string surfaces).

Public headers for shared libraries live in their own dir and are pulled in via `target_include_directories(... PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})` (or `include/` for the two submodules and `sundials`) — there are no installed include dirs at configure time.

## Repo conventions worth knowing

- Output dir is `build/bin/` for native builds (set unconditionally when `OPTION_TARGET_PYTHON_BINDING=OFF`); the wheel build's output dir is whatever scikit-build-core picks (a tmpdir under `/var/folders/...` on macOS).
- `cmake-build-debug/` is the CLion out-of-source build dir; `build/` is the canonical CI/Docker dir. Both are gitignored.
- `extern/pybind11/` and `sundials/` are vendored; prefer surgical edits over upstream re-syncs.
- Shared-library path fixup for releases is done post-build by `.github/scripts/install_name_tool_macos.sh` (macOS) and the `ldd | cp` loops in `cd.yml` (Linux/Windows). Don't introduce absolute `rpath`s into CMake.
- `OPTION_TARGET_PYTHON_BINDING=ON` and `OPTION_TARGET_MESSAGING=ON` are mutually exclusive at the CMake level (the former forces the latter off). The Python wheel never carries a curl dependency.
- `pyproject.toml` is the source of truth for the Python package: `requires-python = ">=3.10"`, `testpaths = ["tests"]`, `filterwarnings = ["error"]` (any warning under pytest is a test failure), and `cibuildwheel` runs `pytest {project}/tests` against the installed wheel.
- Setting `-DOPTION_EXTRA_CONFIG_INFO=ON` dumps every CMake variable at the end of configure — handy for debugging Conan-generated targets.
