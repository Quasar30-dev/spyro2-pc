# Spyro 2 (PS1) BIN/CUE copy of the house xdelta patcher backend (Theia tools/uya_patcher/Patch-ISO.ps1): the source
# may be the retail .cue or .bin (MODE2/2352); the output is a .bin and a matching .cue is written beside it.
# Generic retail-ISO xdelta patcher backend (the ImGui front end's worker; also the console fallback).
# r5's Patch-UYA.ps1 (dist/uya-hoverboots-imgui-20261029-r5) with every game-specific string read from patch.json:
# the patch file is "patch"."file"; the wording comes from the optional "gui" object (console_title, source_label,
# source_rule, output_name, temp_prefix, ascii_example), with the UYA hoverboots values as defaults. The front end
# reads the [n/4] stage markers. Packagers copy this file under a game's own name (e.g. Patch-RC1.ps1).
[CmdletBinding()]
param(
    [string]$Source,
    [string]$Output,
    [switch]$NonInteractive
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$sourceLock = $null
$tempDirectory = $null
$temporaryOutput = $null
$exitStatus = 1
$manifest = $null

function Get-Sha256([string]$Path) {
    $stream = [System.IO.File]::Open($Path, 'Open', 'Read', 'Read')
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Full-Path([string]$Path) {
    return [System.IO.Path]::GetFullPath([Environment]::ExpandEnvironmentVariables($Path.Trim().Trim('"')))
}

function Get-Text([string]$Key, [string]$Default) {
    if ($null -ne $manifest -and $null -ne $manifest.PSObject.Properties['gui']) {
        $property = $manifest.gui.PSObject.Properties[$Key]
        if ($null -ne $property -and -not [string]::IsNullOrWhiteSpace([string]$property.Value)) { return [string]$property.Value }
    }
    return $Default
}

function Test-PlainName([string]$Name) {
    return -not [string]::IsNullOrWhiteSpace($Name) -and $Name -ne '.' -and $Name -ne '..' -and $Name.IndexOfAny([char[]]'/\:') -lt 0
}

try {
    $manifestPath = Join-Path $PSScriptRoot 'patch.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    Write-Host ''
    Write-Host (Get-Text 'console_title' 'UYA Hoverboots - retail USA SCUS-97353 patcher') -ForegroundColor Cyan
    Write-Host 'Creates a separate patched BIN and its CUE. The original stays unchanged.'
    Write-Host ''
    if (-not [Environment]::Is64BitOperatingSystem) { throw 'This package requires 64-bit Windows.' }
    if ($manifest.schema -ne 1) { throw 'Unsupported patch package metadata.' }
    if (-not (Test-PlainName ([string]$manifest.patch.file))) { throw 'patch.json names an invalid patch file. Extract a fresh copy of the complete release ZIP.' }
    $patchPath = Join-Path $PSScriptRoot ([string]$manifest.patch.file)
    $decoderPath = Join-Path $PSScriptRoot 'tools\xdelta3.exe'
    $sourceLabel = Get-Text 'source_label' 'USA UYA'
    if ([string]::IsNullOrWhiteSpace($Source)) {
        if ($NonInteractive) { throw 'Supply -Source and -Output for noninteractive use.' }
        $Source = Read-Host "Paste the full path to your original $sourceLabel .cue or .bin (quotes are OK)"
    }
    if ([string]::IsNullOrWhiteSpace($Source)) { throw 'No source selected. Nothing was changed.' }
    $sourcePath = Full-Path $Source
    if (-not [System.IO.File]::Exists($sourcePath)) { throw "Source file not found: $sourcePath" }
    if ([System.IO.Path]::GetExtension($sourcePath) -ieq '.cue') {
        # the data track's FILE "..." BINARY, beside the cue
        $cueText = [System.IO.File]::ReadAllText($sourcePath)
        $m = [regex]::Match($cueText, '(?im)^\s*FILE\s+"([^"]+)"\s+BINARY\s*$')
        if (-not $m.Success) { throw 'The source .cue names no BINARY file.' }
        $sourcePath = Join-Path ([System.IO.Path]::GetDirectoryName($sourcePath)) $m.Groups[1].Value
        if (-not [System.IO.File]::Exists($sourcePath)) { throw "The .bin named by the .cue was not found: $sourcePath" }
    }
    if ([string]::IsNullOrWhiteSpace($Output)) {
        if ($NonInteractive) { throw 'Supply -Output for noninteractive use.' }
        $outputName = Get-Text 'output_name' 'UYA-Hoverboots-Only.iso'
        if (-not (Test-PlainName $outputName)) { $outputName = 'patched.iso' }
        $defaultOutput = Join-Path ([System.IO.Path]::GetDirectoryName($sourcePath)) $outputName
        $Output = Read-Host "Output .bin path [Enter for $defaultOutput]"
        if ([string]::IsNullOrWhiteSpace($Output)) { $Output = $defaultOutput }
    }
    $outputPath = Full-Path $Output
    # The official Windows xdelta build uses narrow filename APIs. Reject
    # non-ASCII paths before any write instead of allowing silent conversion.
    foreach ($checkedPath in @($PSScriptRoot, $sourcePath, $outputPath)) {
        if ($checkedPath -match '[^\x00-\x7F]') {
            throw ('The bundled xdelta3 requires ASCII paths. Put this package, source ISO and output in folders whose full paths use plain English letters/numbers (spaces are OK), for example {0}.' -f (Get-Text 'ascii_example' 'C:\UYA-Patch'))
        }
    }
    if ($sourcePath.Equals($outputPath, [StringComparison]::OrdinalIgnoreCase)) { throw 'Source and output must be different files.' }
    if ([System.IO.Path]::GetExtension($outputPath) -ine '.bin') { throw 'Choose an output filename ending in .bin.' }
    if (Test-Path -LiteralPath $outputPath) { throw "Output already exists. Choose a new filename: $outputPath" }
    $cuePath = [System.IO.Path]::ChangeExtension($outputPath, '.cue')
    if (Test-Path -LiteralPath $cuePath) { throw "A .cue with the output's name already exists. Choose a new filename: $cuePath" }
    if ([System.IO.Path]::GetFileName($outputPath).Contains('"')) { throw 'The output filename must not contain quotes.' }
    $outputParent = [System.IO.Path]::GetDirectoryName($outputPath)
    if (-not [System.IO.Directory]::Exists($outputParent)) { throw "Output folder does not exist: $outputParent" }
    if ($outputPath.StartsWith('\\')) { throw 'Choose a local output drive so free space and atomic promotion can be checked.' }
    $drive = New-Object System.IO.DriveInfo ([System.IO.Path]::GetPathRoot($outputPath))
    if ([long]$manifest.target.bytes -ge 4294967296 -and $drive.DriveFormat -in @('FAT', 'FAT32')) {
        throw 'The patched ISO exceeds 4 GiB. Choose NTFS, exFAT, or ReFS, not FAT32.'
    }
    [long]$requiredSpace = [long]$manifest.target.bytes + 134217728
    if ($drive.AvailableFreeSpace -lt $requiredSpace) {
        throw ('Insufficient free space: need {0:N0} bytes, available {1:N0}.' -f $requiredSpace, $drive.AvailableFreeSpace)
    }

    # Keep a read-only handle for the entire operation. It prevents another
    # process from changing/deleting the source between validation and decode.
    $sourceLock = [System.IO.File]::Open($sourcePath, 'Open', 'Read', 'Read')
    if ($sourceLock.Length -ne [long]$manifest.source.bytes) {
        throw ('Unsupported source size. Expected exactly {0:N0} bytes; got {1:N0}. {2}' -f $manifest.source.bytes, $sourceLock.Length, (Get-Text 'source_rule' 'Use an unmodified USA SCUS-97353 ISO, not CHD/CSO, PAL, or an already patched image.'))
    }
    Write-Host '[1/4] Checking the complete retail SHA-256...'
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try { $sourceHash = [BitConverter]::ToString($algorithm.ComputeHash($sourceLock)).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
    if ($sourceHash -cne $manifest.source.sha256) {
        throw "Unsupported or damaged source ISO. Expected SHA-256 $($manifest.source.sha256); got $sourceHash. No output was created."
    }
    Write-Host '[2/4] Checking patch and decoder integrity...'
    if (-not [System.IO.File]::Exists($patchPath) -or (Get-Item -LiteralPath $patchPath).Length -ne [long]$manifest.patch.bytes -or (Get-Sha256 $patchPath) -cne $manifest.patch.sha256) {
        throw 'Patch integrity check failed. Extract a fresh copy of the complete release ZIP.'
    }
    if ((Get-Sha256 $decoderPath) -cne $manifest.decoder.sha256) {
        throw 'Bundled xdelta3 integrity check failed. Extract a fresh copy of the complete release ZIP.'
    }
    # A unique same-volume temporary file: success moves it, so no deletion is
    # needed. On failure retain the exact partial path for explicit safe cleanup.
    $tempPrefix = Get-Text 'temp_prefix' '.uya-hoverboots-'
    if (-not (Test-PlainName $tempPrefix)) { $tempPrefix = '.iso-patch-' }
    $temporaryOutput = Join-Path $outputParent ($tempPrefix + [Guid]::NewGuid().ToString('N') + '.partial')
    if (Test-Path -LiteralPath $temporaryOutput) { throw 'Temporary path collision. Please retry.' }
    Write-Host '[3/4] Applying xdelta to a temporary output. This can take several minutes...'
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $decoderPath
    $startInfo.Arguments = '-d -D -R -s "{0}" "{1}" "{2}"' -f $sourcePath, $patchPath, $temporaryOutput
    $startInfo.WorkingDirectory = $PSScriptRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    [void]$startInfo.EnvironmentVariables.Remove('XDELTA')
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw 'Could not start bundled xdelta3.' }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $decoderExit = $process.ExitCode
        $decoderText = $stdoutTask.Result + $stderrTask.Result
    } finally { $process.Dispose() }
    if ($decoderExit -ne 0) { throw "xdelta3 failed (exit $decoderExit): $decoderText" }
    Write-Host '[4/4] Checking the complete patched BIN SHA-256...'
    if ((Get-Item -LiteralPath $temporaryOutput).Length -ne [long]$manifest.target.bytes) { throw 'Decoded output length does not match the expected target. Partial output retained.' }
    $targetHash = Get-Sha256 $temporaryOutput
    if ($targetHash -cne $manifest.target.sha256) { throw "Decoded output hash mismatch: $targetHash. Partial output retained." }
    # .NET Move fails if the destination exists; no force/replacement flag.
    [System.IO.File]::Move($temporaryOutput, $outputPath)
    # the matching CUE (one MODE2/2352 data track, as the retail disc), never overwriting
    $cueBody = 'FILE "' + [System.IO.Path]::GetFileName($outputPath) + """ BINARY`r`n  TRACK 01 MODE2/2352`r`n    INDEX 01 00:00:00`r`n"
    $cueBytes = [Text.Encoding]::ASCII.GetBytes($cueBody)
    $cueFile = [System.IO.File]::Open($cuePath, 'CreateNew', 'Write', 'None')
    try { $cueFile.Write($cueBytes, 0, $cueBytes.Length) } finally { $cueFile.Dispose() }
    $receipt = [ordered]@{
        schema = 1; package = [string]$manifest.name; verified_utc = [DateTime]::UtcNow.ToString('o')
        source_path = $sourcePath; source_bytes = [long]$manifest.source.bytes; source_sha256 = $sourceHash
        output_path = $outputPath; cue_path = $cuePath; output_bytes = [long]$manifest.target.bytes; output_sha256 = $targetHash
        patch_sha256 = $manifest.patch.sha256; decoder_sha256 = $manifest.decoder.sha256
        decoder_exit_code = $decoderExit; decoder_output = $decoderText
        source_opened_read_only = $true; temporary_output_verified_before_promotion = $true
        gameplay_tested = $false; passed = $true
    }
    $receiptPath = $outputPath + '.patch-receipt.json'
    try {
        $receiptBytes = [Text.Encoding]::UTF8.GetBytes(($receipt | ConvertTo-Json -Depth 6))
        $receiptFile = [System.IO.File]::Open($receiptPath, 'CreateNew', 'Write', 'None')
        try { $receiptFile.Write($receiptBytes, 0, $receiptBytes.Length) } finally { $receiptFile.Dispose() }
    } catch { Write-Warning "BIN verified successfully, but could not create receipt: $($_.Exception.Message)" }
    Write-Host ''
    Write-Host "Success: $outputPath" -ForegroundColor Green
    Write-Host "CUE: $cuePath"
    Write-Host "Bytes: $($manifest.target.bytes)"
    Write-Host "SHA-256: $targetHash"
    Write-Host (Get-Text 'play_instructions' 'Open the new .cue in your PS1 setup. Read README for native PC compatibility.')
    $exitStatus = 0
} catch {
    Write-Host ''
    Write-Host ("ERROR: " + $_.Exception.Message) -ForegroundColor Red
    Write-Host 'The original image was not modified.'
} finally {
    if ($null -ne $sourceLock) { $sourceLock.Dispose() }
    if ($null -ne $temporaryOutput -and [System.IO.File]::Exists($temporaryOutput)) {
        Write-Warning "Partial output retained for explicit cleanup: $temporaryOutput"
    }
}
exit $exitStatus
