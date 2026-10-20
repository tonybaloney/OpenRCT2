param(
    [string]$Root = (Split-Path $PSScriptRoot -Parent),
    [string]$RuntimeDirectory,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$Root = [IO.Path]::GetFullPath($Root)
if (!$RuntimeDirectory) {
    $RuntimeDirectory = Join-Path $Root '..\llama.cpp-builds\d775b896-windows-arm64-cuda-13.4\bin'
}
if (!$OutputDirectory) { $OutputDirectory = Join-Path $Root 'bin' }
$manifest = Get-Content -LiteralPath (Join-Path $Root 'guest-thoughts.json') -Raw | ConvertFrom-Json
$dependency = Join-Path $Root 'lib\guest-thoughts'
$runtime = Join-Path $OutputDirectory 'guest-thoughts'
$include = Join-Path $dependency 'include'
$models = Join-Path $OutputDirectory 'models'
foreach ($directory in @($dependency, $runtime, $include, $models)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

function Assert-Arm64([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $reader = New-Object IO.BinaryReader($stream)
    try {
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0xaa64) {
            throw "Guest-thought dependency is not a native ARM64 PE binary: $Path"
        }
    } finally {
        $reader.Dispose()
    }
}

function Get-VerifiedDownload([string]$Url, [string]$Destination, [string]$Hash) {
    if ((Test-Path -LiteralPath $Destination) -and
        (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -eq $Hash) { return }
    $temporary = "$Destination.download"
    try {
        & curl.exe --fail --location --retry 3 --silent --show-error --output $temporary $Url
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $Url" }
        if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $Hash) {
            throw "SHA256 mismatch for $Destination"
        }
        Move-Item -LiteralPath $temporary -Destination $Destination -Force
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}

function Copy-RuntimeFile([string]$Path) {
    Assert-Arm64 $Path
    $destination = Join-Path $runtime (Split-Path $Path -Leaf)
    if ((Test-Path -LiteralPath $destination) -and
        (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -eq
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash) { return }
    Copy-Item -LiteralPath $Path -Destination $destination -Force
}

foreach ($entry in $manifest.runtimeHashes.PSObject.Properties) {
    $file = Join-Path $RuntimeDirectory $entry.Name
    if (!(Test-Path -LiteralPath $file)) {
        throw "Missing supplied CUDA runtime: $file. Set GuestThoughtRuntime to the pinned d775b896 runtime's bin directory."
    }
    if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.Value) {
        throw "Pinned CUDA runtime checksum mismatch: $file"
    }
    Copy-RuntimeFile $file
}
$cuda = $env:CUDA_PATH_V13_4
if (!$cuda) { $cuda = Join-Path $env:ProgramFiles 'NVIDIA GPU Computing Toolkit\CUDA\v13.4' }
foreach ($name in @('cublas64_13.dll', 'cublasLt64_13.dll')) {
    $file = Join-Path $cuda "bin\arm64\$name"
    if (!(Test-Path -LiteralPath $file)) { throw "Missing CUDA 13.4 ARM64 dependency: $file" }
    Copy-RuntimeFile $file
}
Copy-Item -LiteralPath (Join-Path $cuda 'EULA.txt') -Destination (Join-Path $runtime 'CUDA-EULA.txt') -Force
Copy-Item -LiteralPath (Join-Path $cuda 'LICENSE') -Destination (Join-Path $runtime 'CUDA-LICENSE') -Force
Get-VerifiedDownload 'https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-22.1.8/openmp/LICENSE.TXT' `
    (Join-Path $runtime 'OpenMP-LICENSE') $manifest.openmpLicenseSha256

$revision = $manifest.llamaRevision
$archive = Join-Path $dependency "$revision.zip"
Get-VerifiedDownload "https://github.com/ggml-org/llama.cpp/archive/$revision.zip" $archive $manifest.sourceSha256
& tar.exe -xf $archive -C $dependency "llama.cpp-$revision/include" "llama.cpp-$revision/ggml/include" "llama.cpp-$revision/LICENSE"
if ($LASTEXITCODE -ne 0) { throw 'Cannot extract pinned llama.cpp headers.' }
$source = Join-Path $dependency "llama.cpp-$revision"
Copy-Item -Path (Join-Path $source 'include\*.h') -Destination $include -Force
Copy-Item -Path (Join-Path $source 'ggml\include\*.h') -Destination $include -Force
Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination (Join-Path $runtime 'llama-LICENSE') -Force

$model = Join-Path $models $manifest.modelFile
Get-VerifiedDownload "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/$($manifest.modelRevision)/$($manifest.modelFile)" $model $manifest.modelSha256
Get-VerifiedDownload "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct/raw/$($manifest.modelLicenseRevision)/LICENSE" `
    (Join-Path $models 'Qwen-LICENSE') $manifest.modelLicenseSha256
Set-Content -LiteralPath (Join-Path $dependency 'prepared.stamp') -Value "$revision $($manifest.modelSha256)"
Write-Output 'Prepared pinned ARM64 CUDA guest-thought runtime, headers, and verified model.'
