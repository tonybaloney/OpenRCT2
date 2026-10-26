# Build and model hints

## Windows ARM64

- The validated route is native ARM64 MSBuild, Release configuration. CMake wiring
  exists, but has not been validated end-to-end for these experiments.
- Install Visual Studio's ARM64 C++ tools, a Windows SDK, and the **C# and Visual
  Basic Roslyn compilers** component (`Microsoft.VisualStudio.Component.Roslyn.Compiler`).
  Missing Roslyn can cause `MSB4036` for the inline `ReadAssetsJson` dependency task.
- Run commands below from the repository root in an ARM64 Native Tools Command Prompt:

```cmd
msbuild openrct2.proj /t:Build /p:Platform=arm64 /p:Configuration=Release /m:4 /nologo
msbuild test\tests\tests.vcxproj /t:Build /p:Platform=arm64 /p:Configuration=Release /m:4 /nologo
```

When invoking Visual Studio from PowerShell, call `VsDevCmd.bat -arch=arm64
-host_arch=arm64` and MSBuild in the same `cmd.exe` process; environment changes
from a separate batch-file process do not persist. Visual Studio 18 Insiders is
the toolchain used on the development machine.

Launch `.\bin\openrct2.exe` after building. Original RCT2 game assets must be
supplied separately; do not commit them.

## Pinned inference runtime

Windows ARM64 builds enable the experimental inference capability by default.
They use the existing llama.cpp CUDA runtime pinned by `guest-thoughts.json`,
revision `d775b8967a46d8beb110d444aa3b8938179e0dd8`, not a freshly compiled replacement.
The default runtime directory is:

```text
..\llama.cpp-builds\d775b896-windows-arm64-cuda-13.4\bin
```

Override it with `/p:GuestThoughtRuntime="C:\path\to\pinned\runtime\bin"`.
CUDA 13.4 ARM64 cuBLAS libraries must also be installed, including for the
diagnostic CPU-fallback path. Provisioning verifies runtime checksums and stages
DLLs/notices under `bin\guest-thoughts`, headers under `lib\guest-thoughts`, and
the default narration model under `bin\models`.

To build without either experimental inference feature or its provisioning, add
`/p:EnableGuestThoughts=false`. Other Windows architectures default to disabled.
Do not unload/reload the native backend DLLs: upstream process-wide handlers
require the backend to remain pinned for the process lifetime.

## Fetching models

The first ordinary enabled build provisions the default Qwen2.5-1.5B-Instruct
Q8_0 narration model (approximately 1.9 GB), model catalogue and licenses.
Unchanged builds reuse prepared files. To prepare explicitly from PowerShell:

```powershell
.\scripts\prepare-guest-thoughts.ps1
node .\scripts\prepare-guest-decisions.mjs
```

The second command prepares both D3-Edge and D3-Lite from pinned upstream
weights, using Node.js for conversion; Python is not required.

After initial provisioning, the downloader also supports:

```powershell
.\scripts\download-guest-models.ps1
.\scripts\download-guest-models.ps1 -Model qwen2.5-3b-instruct-q4_k_m.gguf
.\scripts\download-guest-models.ps1 -DecisionModels
.\scripts\download-guest-models.ps1 -AllModels
```

The downloader expects the deployed catalogue at
`bin\models\guest-thought-models.json`, not just the root source catalogue.
`-DecisionModels` fetches the selected/default narration model and both D3
classifiers. `-AllModels` fetches the entire narration catalogue and both D3
classifiers: allow more than 75 GB plus conversion inputs and temporary space.
Do not fetch everything unless explicitly requested. Narration transfers resume
and verify SHA256; split models require every shard. D3 preparation also verifies
pinned inputs. In a source checkout D3 conversion uses installed Node.js; the
portable package supplies `tools\node.exe`.

Select installed models in **Options > Advanced**. Narration and destination
decisions have independent toggles and pickers; D3 decisions default to off.
The running game never downloads models or contacts an inference service.

## Demo walkthrough

Prepare the default narration model and both D3 classifiers before presenting;
do not download the entire catalogue just for a demo. Use a Windows ARM64 CUDA
machine with sufficient free GPU memory and the pinned runtime above. Close
other inference workloads. Have original RCT2 assets configured and a populated,
single-player park ready; `test\tests\testdata\parks\bpb.sv6` is the comparison
script's default example. Work from a copy of any personal save.

1. Launch `.\bin\openrct2.exe` and load the populated park. Start at normal
   simulation speed. In **Options > Advanced**, leave **Experimental guest
   decisions** off to establish the native baseline.
2. Enable **Experimental guest thoughts**, select **Qwen2.5-1.5B-Instruct**, and
   open a guest's **Thoughts** tab. Allow the initial model load; `Thinking...`
   should become first-person text grounded in that guest's current state.
   Enlarge the guest window if needed to see more of the paragraph. Show their
   needs/activity alongside the generated thought, then choose another guest
   with a different need. Only open Thoughts tabs request narration.
3. Toggle narration off to show the native thought list, then back on. If
   another narration model was prepared in advance, switch the **Language
   model** picker to demonstrate cancellation/reloading without restarting.
   Missing model shards disable that entry; do not promise instantaneous loads.
4. Enable **Experimental guest decisions**, selecting **D3-Lite** initially.
   Open the park-wide **Guests** window's **D3** tab and let the simulation run.
   Check the model status, pending requests and newest-first completed feed.
   Sampling is park-wide and bounded, not inference for every guest each tick.
5. Click a feed row to open that guest's **D3** tab. Explain the offered
   destinations and their score bars. Contrast a changed destination with
   **Accepted - already heading there**, **Native fallback - low confidence**,
   or **Native fallback - stale result** when those outcomes occur. An accepted
   result is not necessarily a redirect; a green bar is not proof of a good
   choice, and probabilities are over the offered options, not calibrated
   certainty. Guests with fewer than two eligible choices may have no record.
6. Switch to **D3-Edge** if desired, allowing the new model to load. Toggle
   decisions off to return to native selection; this does not rewind targets
   already applied. Reload the original save for a clean baseline comparison.

For a repeatable five-minute-per-variant comparison, build `tests.exe`, stop
other model workloads (including the live game's inference), and run:

```powershell
.\scripts\compare-guest-decisions.ps1 -Seconds 300 -Model d3-lite
```

On `ab/d3-incumbent-neutral`, append `-PromptVariant incumbent-neutral` to
remove incumbent-target hints from the model prompt only. Confidence and safety
gates remain unchanged, and normal gameplay retains the original prompt.
`.\scripts\run-d3-eval-suite.ps1 -Seconds 300` runs both D3 models with both
prompts and a fresh native baseline for each (at least 40 minutes total).
The finished suite can be passed to `scripts\build-d3-eval-dashboard.mjs` to
generate the offline pixel dashboard, two HTML slides and hashed raw evidence.
See the README for the command and the optional image-based PowerPoint exporter.

Add `-Park "C:\parks\demo.park"` to use a different saved park. The script reloads
the same starting state for each variant and writes reports/traces beneath a
new `bin\guest-decision-comparison-*` directory. Inspect `comparison.json`,
`analysis.json` and `model-decisions.jsonl`; use
`guest-decision-architecture.html` to explain candidate selection and native
pathfinding. A short run does not establish better park performance. Existing
traces show strong incumbent-target anchoring, so present fallback/unchanged
outcomes honestly rather than selecting only successful redirects.

If narration stays native or D3 reports unavailable, check model files,
catalogues, pinned DLLs and free GPU memory; inspect the game's console/log
diagnostics. D3 needs CUDA and runs only in normal single-player gameplay, not
title/editor, multiplayer or replay modes. Invalid narration restores native
thoughts, and generated prose can still invent details despite lexical checks.
Do not use output quality as a substitute for checking actual guest state.

## Offline neutral-anonymous dataset

On `dataset/d3-lite-neutral-anonymous`, build `tests.exe` before collection:

```powershell
node --test .\test\tests\d3-dataset.test.mjs
node .\scripts\produce-d3-dataset.mjs --output .\bin\d3-dataset-local
node .\scripts\audit-d3-dataset.mjs .\bin\d3-dataset-local
```

Use `--config` for a separate frozen JSON configuration. The default is a capped
500-case pilot, not a promise of 5,000 retained groups. It uses four independent
headless workers, fresh process/profile per action, full native/candidate
continuations at 3,200/9,600/24,000 ticks, two matched initial RNG replicas,
an uninterrupted source-world probe, and exact native replay. Whole-park rollouts
are expensive. The one-hour dispatch deadline lets in-progress groups finish;
branch and disk caps also apply. Do not rebuild/replace `tests.exe` while
collection is running. Resume only with identical config, executable and assets.

Simulation collection needs original RCT2 objects but no inference. Final exact
token validation uses installed D3-Lite's vocabulary and the pinned native DLL,
via Python's standard library; no weights, GPU inference, trainer, downloads or
third-party Python packages are used. Node.js orchestrates capture and export.

`neutral-anonymous-v1` is a new, explicit future inference contract, not the old
B prompt: codes A-H have generic attributes, without persistent IDs, names,
coordinates or incumbent hints. Existing gameplay prompts/gates are unchanged.
The dataset targets D3's 255-code classifier readout, not chat/SFT completions.
Dataset-only preview/validation guards preserve RNG, including rain/umbrella
eligibility peeks. Registry spatial lookup vectors and normal post-load caches
are rebuilt; whole-world/source-continuation checks reject restoration errors.
The separate utility RNG (used by ride music) is seeded and captured/restored
in audit sidecars too; no ride chunks are ignored to hide tune differences.
Sidecars also preserve state-changing map update queues, including temporary
on-ride-photo countdowns and land-edge doors, which park files do not store.
Keep metadata and audit mappings out of input text. Censored initial journeys
do not receive action labels; later unrelated native rides do not censor an
already resolved initial journey. Observed departures use an explicit zero-utility
tail through the fixed horizon; unknown removal/slot reuse remains censored.
Measured near-tie/RNG disagreement stays soft.
No offered improvement over native means no supervised destination target.
Use `evaluation-*.jsonl` (including non-improving/censored cases) for later
selective-policy evaluation, not just the selected supervised JSONL.

Read `DATASET-CARD.md`, `manifest.json`, contracts and `coverage.json` before use.
Labels are provisional/automatic/unreviewed, Blackpool-only, and not evidence
of cross-park generalization or improved model confidence. Calibration/test
partitions are locked; human review exposes train/development only. Review
records must not be represented as completed until actually annotated.
`package-d3-dataset.ps1` packages JSON evidence/source without checkpoints,
original assets, models, profiles or binaries. Local `.park` checkpoints and
object dependency hashes remain under ignored `bin` for replay.

## Focused checks and invariants

Run tests with `bin` as the working directory:

```powershell
Push-Location .\bin
try {
    .\tests.exe --gtest_filter="GuestNarrative.*:GuestThoughtService.*:GuestThoughtModelCatalog.*:GuestDecision.*:GuestDecisionService.*:GuestDecisionIntegration.*"
} finally {
    Pop-Location
}
```

These tests do not require model weights. Real inference checks are opt-in,
require prepared models/runtime, and can consume substantial GPU memory:

```powershell
Push-Location .\bin
try {
    .\tests.exe --gtest_filter="GuestThoughtModel.DISABLED_ParagraphCuda:GuestThoughtModel.DISABLED_PairedStates:GuestDecisionModel.DISABLED_InstalledD3Cuda" --gtest_also_run_disabled_tests
} finally {
    Pop-Location
}
```

- Narration is presentation-only: preserve native thoughts, simulation RNG,
  save formats and networking. Enforce 60 words, 360 Unicode characters and at
  most 12 rendered lines, further constrained by the actual panel height.
- D3 changes destinations only through main-thread revalidation. Preserve
  bounded asynchronous scheduling, cancellation and native fallback. It is
  single-player only and suspended for replay/multiplayer.
- Keep model weights, runtime DLLs, build products, personal settings/saves and
  original game assets out of git. Commit catalogue/checksum changes together
  with their provisioning changes, and retain license notices.
- See `readme.md` for feature details, comparison experiments and limitations.
