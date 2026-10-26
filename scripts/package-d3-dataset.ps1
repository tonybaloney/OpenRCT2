param(
    [Parameter(Mandatory = $true)][string]$Dataset,
    [Parameter(Mandatory = $true)][string]$Destination
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $Dataset).Path
$root = Split-Path -Parent $PSScriptRoot
$target = [IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $target) { throw "Destination already exists: $target" }
if (Test-Path -LiteralPath ($target + '.zip')) { throw 'Archive already exists.' }
if (-not (Test-Path -LiteralPath (Join-Path $source 'independent-audit.json'))) {
    throw 'Run audit-d3-dataset.mjs before packaging.'
}
$audit = Get-Content -LiteralPath (Join-Path $source 'independent-audit.json') -Raw | ConvertFrom-Json
if (-not $audit.passed) { throw 'Dataset audit did not pass.' }
$sourceHashes = Get-Content -LiteralPath (Join-Path $source 'source-hashes.json') -Raw | ConvertFrom-Json
$pinnedSources = @{
    engine_source = 'src\openrct2\peep\GuestDecision.cpp'
    replay_harness = 'test\tests\GuestDecisionDataset.cpp'
    utility_rng_source = 'src\openrct2\util\Util.cpp'
    utility_rng_header = 'src\openrct2\util\Util.h'
    map_animation_source = 'src\openrct2\world\MapAnimation.cpp'
    map_animation_header = 'src\openrct2\world\MapAnimation.h'
    exporter = 'scripts\produce-d3-dataset.mjs'
    labeler = 'scripts\d3-dataset.mjs'
    tokenizer_helper = 'scripts\tokenize-d3-dataset.py'
}
foreach ($key in $pinnedSources.Keys) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $root $pinnedSources[$key]) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $sourceHashes.$key) { throw "Source changed since capture: $key" }
}
New-Item -ItemType Directory -Path $target | Out-Null

$files = Get-ChildItem -LiteralPath $source -File |
    Where-Object { $_.Extension -in @('.json', '.jsonl', '.md') }
foreach ($directory in @('audit', 'review')) {
    $files += Get-ChildItem -LiteralPath (Join-Path $source $directory) -File -Recurse |
        Where-Object {
            $_.FullName.Substring($source.Length) -notmatch '\\profile\\' -and
            $_.Extension -in @('.json', '.jsonl', '.log', '.txt')
        }
}
$files += Get-ChildItem -LiteralPath (Join-Path $source 'capture') -File -Recurse |
    Where-Object { $_.Name -eq 'object-dependencies.json' }
foreach ($file in $files) {
    $relative = $file.FullName.Substring($source.Length).TrimStart('\')
    $to = Join-Path $target $relative
    New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $to
    if ((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash) {
        throw "Copy hash mismatch: $relative"
    }
}

$sourceFiles = @(
    'AGENTS.md', 'readme.md', 'licence.txt', 'guest-thoughts.json',
    'scripts\d3-dataset.mjs', 'scripts\produce-d3-dataset.mjs',
    'scripts\audit-d3-dataset.mjs', 'scripts\tokenize-d3-dataset.py',
    'scripts\d3-dataset-config.json', 'scripts\d3-dataset-row.schema.json',
    'scripts\package-d3-dataset.ps1', 'test\tests\d3-dataset.test.mjs',
    'test\tests\GuestDecisionDataset.cpp', 'test\tests\GuestDecisionTests.cpp',
    'test\tests\tests.vcxproj', 'test\tests\CMakeLists.txt',
    'src\openrct2\peep\GuestDecision.cpp', 'src\openrct2\peep\GuestDecision.h',
    'src\openrct2\util\Util.cpp', 'src\openrct2\util\Util.h',
    'src\openrct2\world\MapAnimation.cpp', 'src\openrct2\world\MapAnimation.h'
)
foreach ($relative in $sourceFiles) {
    $to = Join-Path $target (Join-Path 'source' $relative)
    New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $relative) -Destination $to
}
$commit = & git -C $root rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source revision.' }
$checkpointIndex = Get-Content -LiteralPath (Join-Path $source 'audit\case-index.json') -Raw | ConvertFrom-Json
$checkpoints = $checkpointIndex | Group-Object checkpoint_sha256 | ForEach-Object {
    $entry = $_.Group[0]
    [ordered]@{
        sha256 = $entry.checkpoint_sha256
        local_path = $entry.checkpoint
        world = $entry.world
        utility_rng_state = $entry.utility_rng_state
        map_update_queues = $entry.map_update_queues
    }
}
$replayManifest = [ordered]@{
    source_commit = $commit
    branch = 'dataset/d3-lite-neutral-anonymous'
    checkpoints_delivered = $false
    checkpoints = @($checkpoints)
    instructions = 'Clone the source branch, build ARM64 tests, provide original RCT2 objects, and run produce-d3-dataset.mjs with configuration.json. Check object dependency hashes. Saved world authoring timestamps may differ; all nonauthoring chunk hashes must agree.'
    requirements = 'Original RCT2 objects installed separately; Node.js; Python standard library; pinned runtime and D3-Lite vocabulary only for exact token checks. No trainer required.'
} | ConvertTo-Json -Depth 30
[IO.File]::WriteAllText((Join-Path $target 'replay-manifest.json'), ($replayManifest + "`n"))

$hashes = Get-ChildItem -LiteralPath $target -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{
        file = $_.FullName.Substring($target.Length).TrimStart('\')
        bytes = $_.Length
        sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$checksumJson = @($hashes) | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText((Join-Path $target 'checksums.json'), ($checksumJson + "`n"))
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archivePath = $target + '.zip'
$archive = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem -LiteralPath $target -File -Recurse) {
        $relative = $file.FullName.Substring($target.Length).TrimStart('\').Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $file.FullName, $relative, [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
$verification = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    foreach ($entry in $verification.Entries) {
        if ($entry.FullName -match '\.(park|sv6|gguf|dll|exe|dat)$') { throw 'Forbidden asset in dataset archive.' }
        $stream = $entry.Open()
        $algorithm = [Security.Cryptography.SHA256]::Create()
        try {
            $hash = [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '')
        } finally { $stream.Dispose(); $algorithm.Dispose() }
        $local = Join-Path $target $entry.FullName.Replace('/', '\')
        if ($hash -ne (Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash) {
            throw "Archive verification failed: $($entry.FullName)"
        }
    }
} finally { $verification.Dispose() }
$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($archivePath + '.sha256'), "$archiveHash  $([IO.Path]::GetFileName($archivePath))`n")
Write-Output "Verified dataset folder: $target"
Write-Output "Verified archive: $archivePath"
Write-Output "SHA256: $archiveHash"
