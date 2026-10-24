function Get-VerifiedDownload {
    param([string]$Url, [string]$Destination, [string]$Hash)
    if ((Test-Path -LiteralPath $Destination) -and
        (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -eq $Hash) { return }
    $temporary = "$Destination.download"
    if ((Test-Path -LiteralPath $temporary) -and
        (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -eq $Hash) {
        Move-Item -LiteralPath $temporary -Destination $Destination -Force
        return
    }
    Write-Output "Downloading $(Split-Path $Destination -Leaf)"
    & curl.exe --fail --location --continue-at - --retry 3 --silent --show-error --output $temporary $Url
    if ($LASTEXITCODE -ne 0) { throw "Download failed (partial file retained for resumption): $Url" }
    if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $Hash) {
        Remove-Item -LiteralPath $temporary
        throw "SHA256 mismatch for $Destination"
    }
    Move-Item -LiteralPath $temporary -Destination $Destination -Force
}
