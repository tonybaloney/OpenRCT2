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
