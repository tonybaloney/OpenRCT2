param(
    [string]$Park = (Join-Path $PSScriptRoot '..\test\tests\testdata\parks\bpb.sv6'),
    [ValidateRange(2, 3600)][int]$Seconds = 300,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $root ('bin\d3-eval-suite-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output directory.' }
$output = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
$parkPath = (Resolve-Path -LiteralPath $Park).Path
$revision = & git -C $root rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source revision.' }
$runs = @()
foreach ($model in @('d3-lite', 'd3-edge')) {
    foreach ($variant in @('original', 'incumbent-neutral')) {
        $name = "$model-$variant"
        Write-Output "Starting $name ($Seconds seconds native + $Seconds seconds model)."
        & (Join-Path $PSScriptRoot 'compare-guest-decisions.ps1') -Park $parkPath -Seconds $Seconds `
            -Model $model -PromptVariant $variant -OutputDirectory (Join-Path $output $name)
        $runs += $name
    }
}
$identity = $null
foreach ($name in $runs) {
    $report = Get-Content -LiteralPath (Join-Path $output "$name\comparison.json") -Raw | ConvertFrom-Json
    $current = $report.initial_state | ConvertTo-Json -Depth 30 -Compress
    if ($null -eq $identity) { $identity = $current }
    elseif ($current -ne $identity) { throw "Initial state mismatch in $name" }
}
@{
    schema = 1
    source_revision = $revision
    test_executable_sha256 = (Get-FileHash -LiteralPath (Join-Path $root 'bin\tests.exe') -Algorithm SHA256).Hash
    park_sha256 = (Get-FileHash -LiteralPath $parkPath -Algorithm SHA256).Hash
    seconds_per_variant = $Seconds
    minimum_probability = 0.45
    minimum_margin = 0.08
    runs = $runs
    completed_utc = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $output 'suite.json') -Encoding UTF8
Write-Output "Verified eval suite ready: $output"
