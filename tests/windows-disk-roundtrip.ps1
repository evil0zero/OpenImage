param(
    [string]$Executable = "$PSScriptRoot\..\build\bundled-qt-release\application\openimage.exe"
)

$ErrorActionPreference = 'Stop'
$principal = [Security.Principal.WindowsPrincipal]::new(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this test in an elevated PowerShell. It creates and overwrites two disposable VHDs.'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$root = Join-Path ([IO.Path]::GetTempPath()) ("openimage-vhd-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $root | Out-Null
$images = @()

function Invoke-OpenImage {
    & $Executable @args
    if ($LASTEXITCODE -ne 0) { throw "OpenImage failed: $($args[0])" }
}

function Assert-Rejected {
    $ErrorActionPreference = 'Continue'
    & $Executable @args 2>$null
    if ($LASTEXITCODE -eq 0) { throw "OpenImage accepted unsafe arguments: $args" }
}

function New-TestDisk([string]$Image) {
    if ($Image -match '[^\x00-\x7f]') { throw 'DiskPart requires an ASCII path for this test.' }
    $scriptPath = Join-Path $root 'create.txt'
    "create vdisk file=""$Image"" maximum=32 type=fixed" |
        Set-Content -LiteralPath $scriptPath -Encoding Ascii
    $result = & diskpart.exe /s $scriptPath
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $Image -PathType Leaf)) {
        throw "Could not create a disposable VHD. $result"
    }
    $disk = @(Mount-DiskImage -ImagePath $Image -NoDriveLetter -PassThru | Get-Disk)
    if ($disk.Count -ne 1 -or $disk[0].IsBoot -or $disk[0].IsSystem -or $disk[0].Size -ne 32MB) {
        throw "Could not identify the newly mounted disposable VHD."
    }
    if (-not $disk[0].IsOffline) { $disk[0] | Set-Disk -IsOffline $true }
    return '\\.\PhysicalDrive' + $disk[0].Number
}

try {
    $first = Join-Path $root 'first.vhd'
    $second = Join-Path $root 'second.vhd'
    $images = @($first, $second)
    $source = New-TestDisk $first
    $target = New-TestDisk $second
    $inventory = Invoke-OpenImage disks | Out-String
    foreach ($device in @($source, $target)) {
        if ($inventory -notmatch ([regex]::Escape($device) + '\s+[^\r\n]+\boffline\b')) {
            throw "Offline VHD missing from startup inventory: $device"
        }
    }
    foreach ($vhd in $images) {
        $testDisk = Get-DiskImage -ImagePath $vhd | Get-Disk
        $testDisk | Set-Disk -IsOffline $false
        $testDisk | Initialize-Disk -PartitionStyle MBR
        $testDisk | New-Partition -UseMaximumSize |
            Format-Volume -FileSystem NTFS -Confirm:$false | Out-Null
    }
    $raw = Join-Path $root 'source.raw'
    $bytes = [byte[]]::new(32MB)
    [Random]::new(42).NextBytes($bytes)
    [Array]::Clear($bytes, 0, 4096)
    [IO.File]::WriteAllBytes($raw, $bytes)
    $image = Join-Path $root 'source.omi'
    $backup = Join-Path $root 'clone.omi'
    Invoke-OpenImage backup $raw $image zstd
    Assert-Rejected disk-restore $image $source
    Invoke-OpenImage disk-restore $image $source --overwrite
    Assert-Rejected disk-clone $source $source --overwrite
    Invoke-OpenImage disk-clone $source $target --overwrite
    $small = Join-Path $root 'small.raw'
    [IO.File]::WriteAllBytes($small, [byte[]]::new(512))
    $smallImage = Join-Path $root 'small.omi'
    Invoke-OpenImage backup $small $smallImage raw
    Assert-Rejected disk-restore $smallImage $target --overwrite
    $invalid = Join-Path $root 'invalid.omi'
    Copy-Item -LiteralPath $image -Destination $invalid
    $file = [IO.File]::OpenWrite($invalid)
    try { $file.WriteByte(0) } finally { $file.Dispose() }
    Assert-Rejected disk-restore $invalid $target --overwrite
    Invoke-OpenImage disk-backup $target $backup zstd
    Invoke-OpenImage verify $backup
    Invoke-OpenImage compare $backup $raw
    Write-Host 'Windows raw-disk round trip passed on two disposable VHDs.'
} finally {
    foreach ($image in $images) {
        if (Test-Path -LiteralPath $image -PathType Leaf) {
            $attached = Get-DiskImage -ImagePath $image
            if ($attached.Attached) { Dismount-DiskImage -ImagePath $image | Out-Null }
        }
    }
    Get-ChildItem -LiteralPath $root -File | Remove-Item
    Remove-Item -LiteralPath $root
}
