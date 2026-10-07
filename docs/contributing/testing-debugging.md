# Testing and Debugging

CrossPoint runs on real hardware, so debugging usually combines local build checks, host-side tests and on-device logs.

## Local checks

Make sure `clang-format` 21+ is installed and available in `PATH` before running the formatting step.
If needed, see [Getting Started](./getting-started.md).

```sh
./bin/clang-format-fix
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
pio run
```

## Host tests

Parsing, layout, cache and most non-UI code is covered by GoogleTest suites in `test/`, built with CMake and run on the development machine. CI runs them on every push and pull request (`.github/workflows/ci.yml`, the host test job):

```sh
cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/test
ctest --test-dir build/test --output-on-failure -j
```

Any build directory works; `test/build` is a common local choice, and every `build*/` directory is git-ignored. To iterate from inside one:

```sh
cd test/build
cmake --build . -j 8
ctest -j 8 --output-on-failure
ctest -R EpubPipeline          # one suite by name
```

GoogleTest is fetched on the first configure. `test/README` has the per-suite commands.

**Golden files.** `EpubPipelineTest` compares a layout dump of every book in `test/epubs` against `test/epub_pipeline/goldens`. When a change is meant to move layout, regenerate them and say why in the commit:

```sh
UPDATE_GOLDENS=1 ctest -R EpubPipeline
```

When a change should move layout but not content, compare word streams rather than whole goldens: take the `t=` field of every `W x=… s=… z=… t=…` record in the dump, in order, before and after the change. Redirect stderr to a separate file first; an interleaved `BENCHMARK` line can split a record. The dump covers table cells too, in row order, so the word stream stays the same when a table switches between grid and paragraph layout. It does not cover anchors; `AnchorMapTest` and `AnchorPageAccuracyTest` do.

`HeapPeakRegression` guards the heap peak of a section build; see the next section.

## Measuring memory

[Memory Allocation Strategy §7](../memory-allocation-strategy.md#7-measuring-memory) explains what each tool and log line measures. To reproduce a measurement:

**On the host**, from the repository root, with `B` set to your host build directory:

```sh
B=build/test
WH_HOST_STDIO_UNBUFFERED=1 $B/epub_pipeline/epub_pipeline_dump book.epub /tmp/cache-a --bench > dump.txt 2> bench.txt
WH_HOST_STDIO_UNBUFFERED=1 $B/epub_pipeline/epub_pipeline_dump book.epub /tmp/cache-b --bench --arena=48000 > dump.txt 2> bench.txt
```

The first run models a build with nothing lent, the second a build in the borrowed framebuffer (48000 for the X4). Use an empty cache directory for a cold build. `bench.txt` has the per-spine times, `heap_peak`, the allocation size histogram and the largest allocation sites. `test/epub_pipeline/run_baseline.sh $B/epub_pipeline/epub_pipeline_dump` tabulates time and peak heap over the whole corpus. `epub_pipeline_dump_noheap` is the same tool without the heap tracker, for when only the layout dump is needed.

`ctest -R HeapPeakRegression` checks six fixtures in both modes against `test/epub_pipeline/heap_peak_baseline.txt`. When a change lowers or raises a peak on purpose, re-baseline and explain it in the commit:

```sh
UPDATE_HEAP_BASELINE=1 ctest --test-dir $B -R HeapPeakRegression
```

For a site-by-site inventory of one build (Linux only):

```sh
$B/epub_pipeline/epub_build_inventory book.epub /tmp/cache-c /tmp/inventory --arena=48000 --spines=3
python3 test/epub_pipeline/inventory_report.py $B/epub_pipeline/epub_build_inventory /tmp/inventory/spine_3.txt > report.md
```

## Install firmware

Locked X4 hardware does not support flashing over USB (`pio run --target upload` is not supported).

To update firmware:
- Copy the compiled binary (`.pio/build/default/firmware.bin`) to the SD card root as `update.bin`
- Or use **Settings -> System -> SD Firmware Update** (or flash from the file browser context menu)
- Or update wirelessly via Wi-Fi OTA / local web interface

## Test release workflows locally with `act`

Use [`act`](https://github.com/nektos/act) to dry-run the release workflows
after modifying them. Running locally with `act` is faster and more iterative
than pushing commits to GitHub and waiting for Actions to run. It will test the
entire workflow, including job conditions and release-notes generation, without
actually publishing releases or uploading assets.

For this repository, local `act` runs simulate different GitHub event types for
the two release workflows:

- `release.yml` is exercised with a simulated `release` event whose action is
  `published`
- `release_candidate.yml` is exercised with a simulated `push` event on
  a `release/**` branch

### What local `act` runs validate

Local runs are useful for validating:

- workflow wiring and job conditions
- PlatformIO release builds
- release-notes generation via `scripts/generate_release_notes.py`

Local `act` runs do **not** publish GitHub releases or upload release assets.
Those steps are skipped when `ACT=true`, so final release publication still
requires a real GitHub Actions run.

### Prerequisites

- `act` installed locally
- Docker available and running
  - Podman will most likely also work. Ensure the rootless user socket is
    configured and set `DOCKER_HOST` to its path (e.g.
    `export DOCKER_HOST=unix:///run/user/1000/podman/podman.sock`).
- `gh` CLI authenticated (`gh auth status`)
- event payload files in `.github/act/`

Included payload files:

- `.github/act/release-published.json` to simulate the `release.published` event
  used by `release.yml`
- `.github/act/release-candidate-push.json` to simulate the branch-push event
  used by `release_candidate.yml`

### Provide `GITHUB_TOKEN` safely

Release-notes generation expects `GITHUB_TOKEN`. Prefer exporting it from `gh`
instead of pasting a token directly into shell history:

```sh
export GITHUB_TOKEN="$(gh auth token)"
```

Then pass it to `act` by name:

```sh
act ... -s GITHUB_TOKEN
```

Unset it when finished:

```sh
unset GITHUB_TOKEN
```

### Run the stable release workflow locally

```sh
act release \
  -W .github/workflows/release.yml \
  -e .github/act/release-published.json \
  -s GITHUB_TOKEN
```

### Run the release-candidate workflow locally

```sh
act push \
  -W .github/workflows/release_candidate.yml \
  -e .github/act/release-candidate-push.json \
  -s GITHUB_TOKEN
```

### What still needs a real GitHub run

After a local `act` pass, a real GitHub Actions run is still required to verify:

- GitHub release creation
- asset upload
- workflow permissions and repository-token behavior
- the exact GitHub-hosted runner environment

## Useful bug report contents

- Firmware version and build environment
- Exact steps to reproduce
- Expected vs actual behavior
- Serial logs from boot through failure
- Whether issue reproduces after clearing `.crosspoint/` cache on SD card

## Common troubleshooting references

- [User Guide troubleshooting section](../../USER_GUIDE.md#8-troubleshooting-issues--escaping-bootloop)
- [Webserver troubleshooting](../troubleshooting.md)
