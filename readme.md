

<p align="center">
  <a href="https://openrct2.io">
    <img src="https://raw.githubusercontent.com/OpenRCT2/OpenRCT2/develop/resources/logo/icon_x128.png" style="width: 128px;" alt="OpenRCT2 logo"/>
  </a>
</p>

<h1 align="center">OpenRCT2</h1>

<h3 align="center">An open-source re-implementation of RollerCoaster Tycoon 2, a construction and management simulation video game that simulates amusement park management.</h3>

---

![Still from the v0.5.0 title sequence](https://github.com/user-attachments/assets/fa893cc8-1484-4751-94be-4ead00a6c8f9)


---

### Download
| Latest release                                                                                                       | Latest development build |
|----------------------------------------------------------------------------------------------------------------------|--------------------------|
| [![OpenRCT2.io](https://img.shields.io/github/v/release/OpenRCT2/OpenRCT2.svg?color=green)](https://openrct2.io/download/release/latest) | [![OpenRCT2.io](https://img.shields.io/github/last-commit/OpenRCT2/OpenRCT2/develop?color=green)](https://openrct2.io/download/develop/latest) |

---

### Chat
Chat takes place on Discord. You will need to create a Discord account if you don't yet have one.

If you want to help *make* the game, join the developer channel.

If you need help, want to talk to the developers, or just want to stay up to date then join the non-developer channel for your language.

If you want to help translate the game to your language, please stop by the Localisation channel.

| Language | Non Developer | Developer | Localisation | Asset Replacement |
| -------- | ------------- | --------- | ------------ | ----------------- |
| English | [![Discord](https://img.shields.io/badge/discord-%23openrct2--talk-blue.svg)](https://discord.gg/ZXZd8D8) </br> [![Discord](https://img.shields.io/badge/discord-%23help-blue.svg)](https://discord.gg/vJABqGGTEt) | [![Discord](https://img.shields.io/badge/discord-%23development-yellowgreen.svg)](https://discord.gg/fsEwSWs) | [![Discord](https://img.shields.io/badge/discord-%23localisation-green.svg)](https://discord.gg/sxnrvX9) | [![Discord](https://img.shields.io/badge/discord-%23open--graphics-b00b69.svg)](https://discord.gg/aM2Pchscnp) </br> [![Discord](https://img.shields.io/badge/discord-%23open--sound--and--music-b00b69.svg)](https://discord.gg/tuz3QBBWJf)
| Nederlands | [![Discord](https://img.shields.io/badge/discord-%23nederlands-orange.svg)](https://discord.gg/cQYSXzW) | | |

---

# Contents
- 1 - [Introduction](#1-introduction)
- 2 - [Downloading the game (pre-built)](#2-downloading-the-game-pre-built)
- 3 - [Building the game](#3-building-the-game)
- 4 - [Contributing](#4-contributing)
  - 4.1 - [Bug fixes](#41-bug-fixes)
  - 4.2 - [New features](#42-new-features)
  - 4.3 - [Translation](#43-translation)
  - 4.4 - [Graphics](#44-graphics)
  - 4.5 - [Audio](#45-audio)
  - 4.6 - [Scenarios](#46-scenarios)
- 5 - [Policies](#5-policies)
  - 5.1 - [Code of conduct](#51-code-of-conduct)
  - 5.2 - [Code signing policy](#52-code-signing-policy)
  - 5.3 - [Privacy policy](#53-privacy-policy)
- 6 - [Licence](#6-licence)
- 7 - [More information](#7-more-information)
- 8 - [Sponsors](#8-sponsors)

---

# 1. Introduction

**OpenRCT2** is an open-source re-implementation of RollerCoaster Tycoon 2 (RCT2). The gameplay revolves around building and maintaining an amusement park containing attractions, shops and facilities. The player must try to make a profit and maintain a good park reputation whilst keeping the guests happy. OpenRCT2 allows for both scenario and sandbox play. Scenarios require the player to complete a certain objective in a set time limit whilst sandbox allows the player to build a more flexible park with optionally no restrictions or finance.

RollerCoaster Tycoon 2 was originally written by Chris Sawyer in x86 assembly and is the sequel to RollerCoaster Tycoon. The engine was based on Transport Tycoon, an older game which also has an equivalent open-source project, [OpenTTD](https://openttd.org). OpenRCT2 attempts to provide everything from RCT2 as well as many improvements and additional features, some of these include support for modern platforms, an improved interface, improved guest and staff AI, more editing tools, increased limits, and cooperative multiplayer. It also re-introduces mechanics from RollerCoaster Tycoon that were not present in RollerCoaster Tycoon 2. Some of those include; mountain tool in-game, the *"have fun"* objective, launched coasters (not passing-through the station) and several buttons on the toolbar.

---

# 2. Downloading the game (pre-built)

OpenRCT2 requires original files of RollerCoaster Tycoon 2 to play. It can be bought at either [Steam](https://store.steampowered.com/app/285330/RollerCoaster_Tycoon_2_Triple_Thrill_Pack/) or [GOG.com](https://www.gog.com/game/rollercoaster_tycoon_2). If you have the original RollerCoaster Tycoon and its expansion packs, you can [point OpenRCT2 to these](https://github.com/OpenRCT2/OpenRCT2/wiki/Loading-RCT1-scenarios-and-data) in order to play the original scenarios.

[Our website](https://openrct2.io/download) offers portable builds and installers with the latest versions of the `master` and `develop` branches. There is also a [launcher](https://openrct2.io/download/launcher) available for Windows, macOS and Linux that will automatically update your build of the game so that you always have the latest version.

Alternatively to using the launcher, for most Linux distributions, we recommend the [latest Flatpak release](https://flathub.org/apps/details/io.openrct2.OpenRCT2). When downloading from Flathub, you will always receive the latest updates regardless of which Linux distribution you use.

Some Linux distributions offer native packages:
* Arch Linux: [openrct2](https://archlinux.org/packages/extra/x86_64/openrct2/) latest release (`extra` repository) and, alternatively, [openrct2-git](https://aur.archlinux.org/packages/openrct2-git) (AUR)
* Gentoo (main portage tree): [games-simulation/openrct2](https://packages.gentoo.org/packages/games-simulation/openrct2)
* NixOS: [openrct2](https://github.com/NixOS/nixpkgs/blob/master/pkgs/by-name/op/openrct2/package.nix)
* openSUSE OBS: [games/openrct2](https://software.opensuse.org/download.html?project=games&package=openrct2)
* Ubuntu PPA (nightly builds): [`develop` branch](https://launchpad.net/~openrct2/+archive/ubuntu/nightly)

Some \*BSD operating systems offer native packages:
* FreeBSD: [games/openrct2](https://www.freshports.org/games/openrct2)

---

# 3. Building the game
- [Building OpenRCT2 on Linux](https://github.com/OpenRCT2/OpenRCT2/wiki/Building-OpenRCT2-on-Linux)
- [Building OpenRCT2 on macOS using CMake](https://github.com/OpenRCT2/OpenRCT2/wiki/Building-OpenRCT2-on-macOS-using-CMake)
- [Building OpenRCT2 on Windows](https://github.com/OpenRCT2/OpenRCT2/wiki/Building-OpenRCT2-on-Windows)
- [Building OpenRCT2 on Windows Subsystem for Linux](https://github.com/OpenRCT2/OpenRCT2/wiki/Building-OpenRCT2-on-Windows-Subsystem-for-Linux)
- [Building OpenRCT2 on MSYS2 MinGW](https://github.com/OpenRCT2/OpenRCT2/wiki/Building-OpenRCT2-on-MSYS2-MinGW)

## Windows ARM64

Install Visual Studio's ARM64 C++ build tools, a Windows SDK, and the **C# and Visual Basic Roslyn compilers** component (`Microsoft.VisualStudio.Component.Roslyn.Compiler`). The Roslyn compiler is required for the inline MSBuild dependency tasks; without it, the build can fail with `MSB4036` when loading `ReadAssetsJson`.

From an ARM64 Native Tools Command Prompt, run the following in the repository root:

```cmd
msbuild openrct2.proj /t:Build /p:Platform=arm64 /p:Configuration=Release /m:4
```

The first build downloads dependencies and generates graphics. Run the game with `bin\openrct2.exe`.

### Experimental generated guest thoughts

This branch enables embedded guest thoughts by default for Windows ARM64. It uses the existing
`..\llama.cpp-builds\d775b896-windows-arm64-cuda-13.4\bin` runtime and the installed ARM64 CUDA 13.4
cuBLAS libraries; it does not compile a replacement llama.cpp. Override the runtime location with
`/p:GuestThoughtRuntime="C:\path\to\the\pinned\runtime\bin"`.

Build setup verifies the pinned runtime DLLs, downloads matching headers and the official
Qwen2.5-1.5B-Instruct Q8_0 model, and verifies their SHA256 checksums from `guest-thoughts.json`.
The model occupies approximately 1.9 GB in `bin\models`; DLLs and license notices are prepared
in `bin\guest-thoughts`. These generated dependencies are not committed. Subsequent unchanged
MSBuild builds do not redownload them.

Choose **Options > Advanced > Experimental guest thoughts > Language model** to switch models
without restarting. The selection is saved; switching cancels obsolete work and frees the old model
on the worker before loading the next one. Existing guest windows keep their subscriptions.
The checkbox disables generation and unloads the model in the background.

The model-free portable ZIP includes the game/runtime but no model weights or original RCT2 assets.
Extract the whole archive, then download the default narration model from its root:

```powershell
.\scripts\download-guest-models.ps1
```

Run `START.cmd` afterward. The launcher keeps a fresh local `profile` beside the packaged `bin` directory.
Use `-Model qwen2.5-3b-instruct-q4_k_m.gguf` for a specific narration model, `-DecisionModels`
to also prepare both D3 classifiers, or `-AllModels` for the complete narration catalogue and both
D3 classifiers. The complete collection uses more than 75 GB, plus conversion inputs and temporary files.
Downloads are pinned, SHA256-verified, resumable for narration, and reused when already correct.
Split GGUF models download all required shards. D3 conversion uses the bundled native ARM64 Node runtime;
no developer toolchain or Python installation is needed in the portable package.
The script does not download game assets; supply your own RCT2 installation/data as usual.

The checksum-pinned `guest-thought-models.json` catalogue retains 0.5B and 1.5B Instruct Q8_0,
and adds Qwen2.5 3B, 7B, 14B, and 32B **base and Instruct** variants at **Q4_K_M**.
Install the complete collection in advance with:

```powershell
.\scripts\prepare-guest-thoughts.ps1 -AllModels
```

This downloads roughly 74 GB including the small models. Downloads resume after network failures,
and every file is SHA256-verified before publication. All shards of split models are required;
incomplete entries are disabled in the picker. Normal builds provision only the default 1.5B model,
runtime, catalogue, and licenses, not the entire collection. The official Instruct GGUFs come from
Qwen; base GGUFs come from mradermacher, with original Qwen model licenses included.

Instruct models use their chat templates. Base models use a plain-text few-shot completion prompt,
not a fabricated chat template; their behavior is experimental and the same grounding checks apply.
The new 3B-32B entries require CUDA unless CPU is explicitly forced for diagnostics, and check
free GPU memory against weight sizes plus 1 GiB of context/headroom before loading.
The 72B Q4_K_M weights alone exceed this machine's approximately 31 GiB GPU memory allocation,
so that size is deliberately excluded. Only one selected narration model is loaded, not the whole collection.

Open a guest's **Thoughts** tab to generate a state-conditioned English reflection automatically.
Only open Thoughts tabs request inference. The runtime uses CUDA first, including integrated
CUDA devices, with a logged CPU fallback for recoverable failures on the original small models. The prompt requests
one short first-person paragraph of two to four sentences, aiming for 30-50 words. Text is limited to 60 words,
360 Unicode characters, and twelve rendered lines, further shortened if needed to fit the current window height.
The generation budget is 192 tokens within a 2,048-token context. Generated text replaces the displayed native list,
with `Thinking...` while pending; the engine's internal thoughts and simulation behavior are unchanged.
Inference never downloads files or calls a network service; missing dependencies or
rejected output restores the native thoughts.

The `[interface]` section of `config.ini` supports `guest_thoughts = false` to disable generation,
`guest_thought_model` to select an executable-relative catalogue filename or override the GGUF path,
and `guest_thought_force_cpu = true` for diagnostics. Custom models outside the catalogue retain
the original instruction-tuned chat-template behavior.
To build without the feature or its provisioning, append `/p:EnableGuestThoughts=false`.
Other Windows architectures default to disabled. CMake exposes `ENABLE_GUEST_THOUGHTS` and
`GUEST_THOUGHT_RUNTIME` for the equivalent ARM64 UI build.

To try this branch on another machine, use the Windows ARM64 build instructions above and supply
the exact pinned `d775b896` ARM64 CUDA runtime's `bin` directory with `/p:GuestThoughtRuntime=...`
if it is not in the default sibling directory. The feature requires the ARM64 CUDA 13.4 cuBLAS
libraries installed by the CUDA toolkit, even when forcing CPU inference. The runtime checksums
are recorded in `guest-thoughts.json`; arbitrary llama.cpp builds are not interchangeable.
Model weights, runtime DLLs, game assets, and build outputs are not included in git.
The first build needs network access to provision dependencies; the running game does not.
The native MSBuild build is verified on Windows ARM64; the CMake integration is experimental
and has not been validated end-to-end.

Model-free regression tests run with `tests.exe` from the `bin` working directory. The optional embedded model
checks require the prepared dependencies:

```cmd
cd bin
tests.exe --gtest_filter=GuestThoughtModel.*-GuestThoughtModel.DISABLED_InstalledCatalogCuda --gtest_also_run_disabled_tests
```

The complete collection also has an opt-in sequential CUDA-only load-and-generation check:

```cmd
cd bin
tests.exe --gtest_filter=GuestThoughtModel.DISABLED_InstalledCatalogCuda --gtest_also_run_disabled_tests
```

Set `OPENRCT2_GUEST_THOUGHT_TEST_MODEL` to one catalogue GGUF filename to restrict that check.
CPU fallback is disallowed in this check, and the runtime logs its layer-offload summary.

The llama.cpp runtime carries its MIT notice, most listed Qwen models their Apache 2.0 notice,
the 3B variants their separate Qwen license, and OpenMP its LLVM
notice. CUDA libraries retain NVIDIA's separate license terms. This is a local experimental
setup, not a redistribution bundle; review all component and game license requirements before
distributing binaries. A tiny model can still produce inaccurate text; deterministic validation
rejects common contradictions but is not a general factuality guarantee.

### Experimental guest decision controllers

**Options > Advanced > Experimental guest decisions** has an independent enable checkbox and
**Decision model** picker for **D3-Lite (0.8B)** and **D3-Edge (0.6B)**. Decisions are **off by
default**, with D3-Lite selected. They affect walking guests throughout the park, not only guests
whose windows are open. Narration remains a separate, presentation-only feature.

Prepare both models once, after the ordinary ARM64 inference-runtime setup:

```powershell
node .\scripts\prepare-guest-decisions.mjs
```

This requires Node.js and Windows curl, not Python or PyTorch. The catalogue pins both official
`vllm-sr` revisions. Setup verifies every downloaded input against the pinned upstream manifest,
converts the text backbones to F16 GGUF, and preserves the FP32 255-code classifier readouts,
Apache 2.0 licenses and notices under `bin\models\guest-decisions`. Original verified
safetensors are retained for reproducibility. Models are not downloaded by normal builds or by
the running game. The two prepared GGUFs occupy about 1.0 GB and 1.5 GB, plus runtime memory;
these are classifiers, not extra Qwen thought-generation models.

The existing pinned CUDA DLLs perform a non-causal prompt forward pass, with the original causal
DeltaNet scans and per-layer attention layout. The final hidden state feeds the model's dedicated
FP32 readout. As in upstream `permutation_average`, both option orders are scored and their aligned
probabilities averaged. No prose is parsed into commands, and no downloaded Python code executes.
The vision tower is omitted because guest decisions are text-only. F16 conversion and llama.cpp
numerics are not claimed to be bit-identical to upstream BF16 PyTorch inference.

Immutable snapshots contain actual current motives, cash, intensity preferences, nausea tolerance
and bounded, known, natively eligible destinations. Urgent physiological needs exclude recreational
rides from the candidates; cash machines can remain an alternative when money is low. At most eight
choices are offered. If fewer than two exist, native selection continues. A tiny classifier can
misrank choices: both models sometimes preferred a ride over food in an unrestricted calibration probe,
so neither model is allowed to bypass the native eligibility and urgent-need filtering.

A bounded worker queue admits at most four decisions per second **across the entire park**, with
round-robin guest selection and a 30-second per-guest cooldown. Each decision uses two forward
passes; this is not one inference per guest per simulation tick. Narration and decisions share a
GPU-work mutex, but keep separate model contexts. Switching decision models cancels old requests
and unloads the previous decision model on the worker, without joining on the UI thread. CUDA is
required; insufficient free GPU memory produces an explicit unavailable status rather than a
CPU fallback. The settings panel reports loading, active/applied count, suspended, or unavailable.

The **D3 tab in the park-wide Guests window** shows a live, newest-first feed of the last 128
completed decisions, model status, accepted/native/stale counters, and queued requests. Each row
shows the guest, outcome, top-scored destination, model score and elapsed time including queueing.
Click a row to open that guest's **D3 tab**, which shows their latest request and all offered
destinations with score bars. Green bars mark accepted choices, not guaranteed correctness.
An accepted choice can keep the existing target; the views distinguish that from changing it.
Low-confidence and stale results explicitly show native fallback. Scores are model probabilities
over the offered choices, not calibrated certainty. Only eligible walking guests are sampled;
guests without two destinations may have no record. These diagnostic records are bounded,
client-local, cleared on park/model changes and never saved.

Results older than five wall-clock seconds or 512 simulation ticks, reused guest IDs, changed
motives/intents, and newly ineligible targets are rejected on the simulation thread. Low confidence,
timeouts and failures leave native behavior running. Approved choices update the existing target,
lost countdown and navigation reset; native pathfinding, queue entry, payments, purchases, rides,
benches, bins and departure logic remain authoritative. The controller does not use scenario RNG
or add entity/save fields. Existing chosen targets can persist in saves, just like native targets;
worker requests and model state do not.

This experimental controller is **single-player only** and is suspended in title/editor modes,
network games, replay recording, playback and normalisation. Asynchronous model decisions are not
recorded/synchronised for replay or multiplayer. Park/entity resets cancel outstanding work.
`[interface]` configuration uses `guest_decisions` and `guest_decision_model` (`d3-lite` or `d3-edge`).
The controller shares the experimental build's `EnableGuestThoughts` / `ENABLE_GUEST_THOUGHTS`
capability gate; disabling that capability also removes its inference backend and settings.

Model-free controller tests run with the ordinary suite. The opt-in real CUDA classifier and
native-goal application checks run from `bin`:

```cmd
tests.exe --gtest_filter=GuestDecisionModel.*:GuestDecisionIntegration.DISABLED_InstalledD3Controller --gtest_also_run_disabled_tests
```

#### Tracing and comparing a saved park

The `ab/d3-incumbent-neutral` experiment branch adds an explicit
`-PromptVariant incumbent-neutral` comparison. It removes `current_destination`
from model-visible state and `already_heading_here` from each option, without
changing candidate eligibility, option order, confidence thresholds (0.45 top
probability and 0.08 margin), stale-result checks or native fallback. Incumbent
identity remains in diagnostics and main-thread freshness validation. Ordinary
gameplay and comparisons without this parameter retain the original prompt.
This tests an incumbent-confidence shortcut; it is not a claim that lowering
confidence or increasing redirections improves the park.

```powershell
.\scripts\run-d3-eval-suite.ps1 -Seconds 300
```

The suite runs D3-Lite and D3-Edge with original and incumbent-neutral prompts,
each paired with a freshly reloaded native baseline. Allow at least 40 minutes
for the four five-minute native/model pairs, plus loading and frozen-input
ablation checks. All four runs must match the same initial state and RNG.

After building the test executable and installing the chosen decision model, run:

```powershell
.\scripts\compare-guest-decisions.ps1 -Park "C:\parks\example.park" -Seconds 300 -Model d3-lite
```

Omit `-Park` to use the repository's populated Blackpool test park. Use `-OutputDirectory` to
choose a new results directory; otherwise a timestamped directory is created under ignored `bin`.
The opt-in headless experiment reloads the same save for native-only and model-enabled variants,
checks equal initial entity checksums, scenario RNG and metrics, and advances each for the same
number of ticks at 40 ticks/second. It does not overwrite the save, use the personal UI profile,
enable narration, change confidence thresholds or change the production prompt. Each variant
takes the specified real-time duration when the CPU keeps up; actual wall time and slow-tick
counts are recorded. Avoid other GPU inference workloads while comparing.

`model-decisions.jsonl` contains submitted and completed events with frozen guest state, the
exact prompt, offered destination attributes, scores, incumbent/selected IDs, confidence margins,
and acceptance or explicit fallback/revalidation reasons. Tracing is opt-in through the
simulation-thread `SetGuestDecisionTraceSink` API; normal gameplay does not write trace files.
A trace-write failure is logged, disables that sink and is reported to the experiment.

`comparison.json` reports start/end and time-mean park metrics, customer totals, controller
outcomes and timing. `analysis.json` separates confidence for existing versus alternative
destinations, and the per-second metric files are also exported as CSV. For up to 64 systematically
sampled decisions with an incumbent among the options, `incumbent-ablation.jsonl` additionally
rescores the identical state/options with incumbent metadata hidden or assigned to a different
option. These interventions change both `current_destination` and `already_heading_here`, not
guest needs, prices, distances or ratings. They test classifier sensitivity, not counterfactual
park performance. One short paired run cannot establish long-term benefits; model timing is
asynchronous and repeated runs/seeds and longer horizons are needed for stronger conclusions.

---

# 4. Contributing
OpenRCT2 uses the [gitflow workflow](https://www.atlassian.com/git/tutorials/comparing-workflows#gitflow-workflow). If you are implementing a new feature or fixing a bug, please branch off and perform pull requests to ```develop```. ```master``` only contains tagged releases, you should never branch off this.

Please read our [contributing guidelines](https://github.com/OpenRCT2/OpenRCT2/blob/develop/CONTRIBUTING.md) for information.

## 4.1 Bug fixes
A list of bugs can be found on the [issue tracker](https://github.com/OpenRCT2/OpenRCT2/issues). Feel free to work on any bug and submit a pull request to the develop branch with the fix. Mentioning that you intend to fix a bug on the issue will prevent other people from trying as well.

## 4.2 New features
Please talk to the OpenRCT2 team first before starting to develop a new feature. We may already have plans for or reasons against something that you'd like to work on. Therefore contacting us will allow us to help you or prevent you from wasting any time. You can talk to us via Discord, see links at the top of this page.

## 4.3 Translation
You can translate the game into other languages by editing the language files in ```data/language``` directory. Please join discussions in the [#localisation channel on Discord](https://discordapp.com/invite/sxnrvX9) and submit pull requests to [OpenRCT2/Localisation](https://github.com/OpenRCT2/Localisation).

## 4.4 Graphics
You can help create new graphics for the game by visiting the [OpenGraphics project](https://github.com/OpenRCT2/OpenGraphics). 3D modellers needed!

## 4.5 Audio
You can help create the music and sound effects for the game. Check out the [OpenMusic](https://github.com/OpenRCT2/OpenMusic) repository and drop by our [#open-sound-and-music channel on Discord](https://discord.gg/9y8WbcX) to find out more.

## 4.6 Scenarios
We would also like to distribute additional scenarios with the game, when the time comes. For that, we need talented scenario makers! Check out the [OpenScenarios repository](https://github.com/PFCKrutonium/OpenRCT2-OpenScenarios).

---

# 5. Policies

## 5.1 Code of Conduct

We have a [Code of Conduct](CODE_OF_CONDUCT.md) that applies to all OpenRCT2 projects. Please read it.

## 5.2 Code signing policy

We sign our releases with a digital certificate provided by SignPath Foundation.

Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).

Signed releases can only be done by members of the [development team](https://github.com/OpenRCT2/OpenRCT2/blob/develop/contributors.md#development-team).

## 5.3 Privacy policy

See [PRIVACY.md](PRIVACY.md) for more information.

---

# 6. Licence
**OpenRCT2** is licensed under the GNU General Public License version 3 or (at your option) any later version. See the [`licence.txt`](licence.txt) file for more details.

---

# 7. More information
- [GitHub](https://github.com/OpenRCT2/OpenRCT2)
- [OpenRCT2.io](https://openrct2.io)
- [Facebook](https://www.facebook.com/OpenRCT2)
- [RCT subreddit](https://www.reddit.com/r/rct/)
- [OpenRCT2 subreddit](https://www.reddit.com/r/openrct2/)
- OpenRCT2 plug-ins
    - [Plug-in directory (unofficial)](https://openrct2plugins.org)
    - [Plug-in development documentation](https://github.com/OpenRCT2/OpenRCT2/blob/develop/distribution/scripting/scripting.md)

## Similar Projects

| [OpenLoco](https://github.com/OpenLoco/OpenLoco) | [OpenTTD](https://github.com/OpenTTD/OpenTTD) | [openage](https://github.com/SFTtech/openage) | [OpenRA](https://github.com/OpenRA/OpenRA) |
|:------------------------------------------------:|:----------------------------------------------------------------------------------------------------------:|:-------------------------------------------------------------------------------------------------------:|:-------------------------------------------------------------------------------------------------------------:|
| [![icon_x128](https://user-images.githubusercontent.com/604665/53047651-2c533c00-3493-11e9-911a-1a3540fc1156.png)](https://github.com/OpenLoco/OpenLoco) | [![](https://github.com/OpenTTD/OpenTTD/raw/850d05d24d4768c81d97765204ef2a487dd4972c/media/openttd.128.png)](https://github.com/OpenTTD/OpenTTD) | [![](https://user-images.githubusercontent.com/550290/36507534-4693f354-175a-11e8-93a7-faa0481474fb.png)](https://github.com/SFTtech/openage) | [![](https://raw.githubusercontent.com/OpenRA/OpenRA/bleed/packaging/artwork/ra_128x128.png)](https://github.com/OpenRA/OpenRA) |
| Chris Sawyer's Locomotion | Transport Tycoon Deluxe | Age of Empires 2 | Red Alert |

# 8. Sponsors

Companies that kindly allow us to use their stuff:

| [JetBrains](https://www.jetbrains.com/)                                                                                                        | [Backtrace](https://backtrace.io/)                                                                                                        | [SignPath](https://signpath.org/)                                                                                  |
|------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------|--------------------------------------------------------------------------------------------------------------------|
| [![jetbrains](https://github.com/user-attachments/assets/0d1cf25e-706d-4e3a-96ee-157fbf2cf0c0)](https://www.jetbrains.com/) | [![backtrace](https://user-images.githubusercontent.com/550290/47113259-d0647680-d258-11e8-97c3-1a2c6bde6d11.png)](https://backtrace.io/) | [![Image](https://github.com/user-attachments/assets/2b5679e0-76a4-4ae7-bb37-a6a507a53466)](https://signpath.org/) |
| CLion and other products                                                                                                                       | Minidump uploads and inspection                                                                                                           | Free code signing provided by [SignPath.io](https://about.signpath.io/), certificate by [SignPath Foundation](https://signpath.org/).                                                                                                       |
