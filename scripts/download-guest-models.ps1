param(
    [string]$GameDirectory = (Join-Path (Split-Path $PSScriptRoot -Parent) 'bin'),
    [string]$Model = 'qwen2.5-1.5b-instruct-q8_0.gguf',
    [switch]$AllModels,
    [switch]$DecisionModels
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'verified-model-download.ps1')
$GameDirectory = [IO.Path]::GetFullPath($GameDirectory)
$models = Join-Path $GameDirectory 'models'
$catalog = Get-Content -LiteralPath (Join-Path $models 'guest-thought-models.json') -Raw | ConvertFrom-Json
if ($AllModels -and $PSBoundParameters.ContainsKey('Model')) {
    throw 'Choose either -Model or -AllModels.'
}
$selected = @($catalog.models | Where-Object { $AllModels -or $_.files[0].name -eq $Model })
if ($selected.Count -eq 0) { throw "Unknown model filename: $Model. See models\guest-thought-models.json." }
Write-Output ('Narration download: {0:N2} GiB (already verified files are reused).' -f
    (($selected | ForEach-Object { $_.files } | Measure-Object -Property size -Sum).Sum / 1GB))
foreach ($entry in $selected) {
    if ($entry.repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $entry.revision -notmatch '^[a-f0-9]{40}$') {
        throw 'Invalid pinned model repository or revision.'
    }
    foreach ($file in $entry.files) {
        if ([string]::IsNullOrWhiteSpace($file.name) -or $file.name -in @('.', '..') -or
            $file.name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
            $file.sha256 -notmatch '^[a-f0-9]{64}$') { throw 'Invalid pinned model filename or checksum.' }
        Get-VerifiedDownload "https://huggingface.co/$($entry.repository)/resolve/$($entry.revision)/$($file.name)" `
            (Join-Path $models $file.name) $file.sha256
        if ((Get-Item -LiteralPath (Join-Path $models $file.name)).Length -ne $file.size) {
            throw "Model size mismatch: $($file.name)"
        }
    }
}
if ($DecisionModels -or $AllModels) {
    $node = Join-Path (Split-Path $PSScriptRoot -Parent) 'tools\node.exe'
    if (!(Test-Path -LiteralPath $node)) {
        $node = (Get-Command node.exe -ErrorAction Stop).Source
    }
    Write-Output 'Preparing D3-Edge and D3-Lite from pinned upstream weights (several additional GiB).'
    & $node (Join-Path $PSScriptRoot 'prepare-guest-decisions.mjs') (Join-Path $models 'guest-decisions')
    if ($LASTEXITCODE -ne 0) { throw 'Decision model download/conversion failed.' }
}
Write-Output "Verified models are ready in $models. Select installed models in Options > Advanced."
