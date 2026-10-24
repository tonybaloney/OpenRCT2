param(
    [string]$Park = (Join-Path $PSScriptRoot '..\test\tests\testdata\parks\bpb.sv6'),
    [ValidateRange(2, 3600)][int]$Seconds = 300,
    [ValidateSet('d3-lite', 'd3-edge')][string]$Model = 'd3-lite',
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$parkPath = (Resolve-Path -LiteralPath $Park).Path
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $root ('bin\guest-decision-comparison-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw 'Choose a new output directory; existing experiments are not overwritten.'
}
$output = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
$metadata = @{
    park = $parkPath
    park_sha256 = (Get-FileHash -LiteralPath $parkPath -Algorithm SHA256).Hash
    model = $Model
    seconds_per_variant = $Seconds
    started_utc = [DateTime]::UtcNow.ToString('o')
    narration = 'off (headless experiment)'
    tick_rate = 40
}
$metadata | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'metadata.json') -Encoding UTF8
$names = @('OPENRCT2_DECISION_EXPERIMENT_PARK', 'OPENRCT2_DECISION_EXPERIMENT_OUTPUT',
           'OPENRCT2_DECISION_EXPERIMENT_SECONDS', 'OPENRCT2_DECISION_EXPERIMENT_MODEL')
$previous = @{}
foreach ($name in $names) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    $env:OPENRCT2_DECISION_EXPERIMENT_PARK = $parkPath
    $env:OPENRCT2_DECISION_EXPERIMENT_OUTPUT = $output
    $env:OPENRCT2_DECISION_EXPERIMENT_SECONDS = [string]$Seconds
    $env:OPENRCT2_DECISION_EXPERIMENT_MODEL = $Model
    $arguments = @('--gtest_filter=GuestDecisionExperiment.DISABLED_ComparePark',
                   '--gtest_also_run_disabled_tests',
                   ('--gtest_output=xml:"' + (Join-Path $output 'test-results.xml') + '"'))
    $process = Start-Process -FilePath (Join-Path $root 'bin\tests.exe') -WorkingDirectory (Join-Path $root 'bin') `
        -ArgumentList $arguments -RedirectStandardOutput (Join-Path $output 'run.log') `
        -RedirectStandardError (Join-Path $output 'run.stderr') -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        Get-Content -LiteralPath (Join-Path $output 'run.log') -Tail 20
        throw "Comparison failed with exit code $($process.ExitCode). Logs: $output"
    }
    $report = Get-Content -LiteralPath (Join-Path $output 'comparison.json') -Raw | ConvertFrom-Json
    if ($report.model_on.submitted -eq 0) { throw 'No decisions were submitted; this map/run did not exercise the controller.' }
    & node (Join-Path $PSScriptRoot 'analyse-guest-decisions.mjs') $output |
        Set-Content -LiteralPath (Join-Path $output 'analysis.log') -Encoding UTF8
    if ($LASTEXITCODE -ne 0) { throw "Trace analysis failed. Logs: $output" }
    Write-Output "Comparison saved to $output"
    foreach ($metric in @('guests', 'mean_happiness', 'hungry', 'thirsty', 'sick', 'toilet_need', 'queuing')) {
        [PSCustomObject]@{
            Metric = $metric
            NativeTimeMean = [Math]::Round($report.native_off.time_mean.$metric, 3)
            ModelTimeMean = [Math]::Round($report.model_on.time_mean.$metric, 3)
        }
    }
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
