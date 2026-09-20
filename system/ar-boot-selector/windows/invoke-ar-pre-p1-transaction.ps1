[CmdletBinding()]
param(
    [int]$DiskNumber = 1,
    [ValidateSet('recovery-selector', 'application-only')] [string]$Profile = 'recovery-selector',
    [ValidateSet('GKDROUND64', 'GKDROUND64R5')] [string]$ExpectedSerial = 'GKDROUND64',
    [Parameter(Mandatory = $true)] [string]$CandidatePath,
    [Parameter(Mandatory = $true)] [string]$ExpectedCandidateSha256,
    [Parameter(Mandatory = $true)] [string]$ExpectedLiveSha256,
    [Parameter(Mandatory = $true)] [string]$BackupPath,
    [Parameter(Mandatory = $true)] [string]$ResultPath,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$DiskBytes = [long]31457280000
$DiskSignature = [uint32]0x0007130c
$PrefixBytes = [int]20971520
$EnvironmentOffset = [int]0x200000
$EnvironmentBytes = [int]0x1000
$EnvironmentActivationBytes = [int]512
$SelectorOffset = [int]0x201000
$SelectorBytes = [int]0x1000
$RecoveryOffset = [int]0x300000
$RecoveryBytes = [int]0x600000
$RecoveryActivationBytes = [int]512
$ApplicationOffset = [int]0x900000
$ApplicationBytes = [int]0x600000
$ExpectedMbrSha256 = 'a1dfed84445c9a6bd1eecced9e8af0af97a932fac29766cbe78aea0a62f7b95e'
$ExpectedPartitions = @(
    [pscustomobject]@{ Number = 1; Type = 0x83; Offset = [long]20971520; Size = [long]805306880 },
    [pscustomobject]@{ Number = 2; Type = 0x83; Offset = [long]827326464; Size = [long]29556211712 },
    [pscustomobject]@{ Number = 3; Type = 0x82; Offset = [long]30383538176; Size = [long]1073741824 }
)

function Get-BytesSha256 {
    param([byte[]]$Bytes, [int]$Offset = 0, [int]$Count = -1)
    if ($Count -lt 0) { $Count = $Bytes.Length - $Offset }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes, $Offset, $Count)) -replace '-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Assert-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator privileges are required for raw disk access.'
    }
}

function Assert-OutputPath {
    param([string]$Path, [string]$Label, [switch]$AllowExisting)
    $full = [IO.Path]::GetFullPath($Path)
    $parent = Get-Item -LiteralPath ([IO.Path]::GetDirectoryName($full)) -Force
    if (-not $parent.PSIsContainer -or (($parent.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) {
        throw "$Label parent is unsafe."
    }
    if ((Test-Path -LiteralPath $full) -and -not $AllowExisting) { throw "$Label must be new." }
    return $full
}

function Assert-TargetDisk {
    if ($ExpectedSerial -ceq 'GKDROUND64R5' -and $Profile -cne 'application-only') {
        throw 'R export identity is allowed only for an A-only transaction.'
    }
    $disk = Get-Disk -Number $DiskNumber -ErrorAction Stop
    $serial = ([string]$disk.SerialNumber).Trim().Trim([char]0)
    if ($disk.BusType -ne 'USB' -or $disk.PartitionStyle -ne 'MBR' -or
        $disk.FriendlyName -cne 'Linux File-Stor Gadget' -or $serial -cne $ExpectedSerial -or
        [long]$disk.Size -ne $DiskBytes -or [uint32]$disk.Signature -ne $DiskSignature -or
        $disk.IsBoot -or $disk.IsSystem -or $disk.IsOffline -or
        $disk.OperationalStatus -notcontains 'Online') {
        throw 'Exact selected GKD system-card disk identity mismatch.'
    }
    $partitions = @(Get-Partition -DiskNumber $DiskNumber | Sort-Object PartitionNumber)
    if ($partitions.Count -ne 3) { throw 'Partition count mismatch.' }
    for ($index = 0; $index -lt $ExpectedPartitions.Count; $index++) {
        $actual = $partitions[$index]
        $expected = $ExpectedPartitions[$index]
        if ([int]$actual.PartitionNumber -ne $expected.Number -or
            [int]$actual.MbrType -ne $expected.Type -or
            [long]$actual.Offset -ne $expected.Offset -or
            [long]$actual.Size -ne $expected.Size -or [char]$actual.DriveLetter -ne [char]0) {
            throw "Partition geometry or access-path mismatch at partition $($expected.Number)."
        }
    }
    return $disk
}

function Read-ExactPrefix {
    param([IO.Stream]$Stream)
    $bytes = New-Object byte[] $PrefixBytes
    $Stream.Position = 0
    $done = 0
    while ($done -lt $PrefixBytes) {
        $read = $Stream.Read($bytes, $done, $PrefixBytes - $done)
        if ($read -le 0) { throw "Short raw read at byte $done." }
        $done += $read
    }
    return $bytes
}

function Assert-EqualRange {
    param([byte[]]$Before, [byte[]]$After, [int]$Offset, [int]$Count, [string]$Label)
    if ((Get-BytesSha256 $Before $Offset $Count) -cne (Get-BytesSha256 $After $Offset $Count)) {
        throw "Candidate changes forbidden range: $Label."
    }
}

function Write-And-VerifyRange {
    param([IO.FileStream]$Stream, [byte[]]$Candidate, [int]$Offset, [int]$Count, [string]$Label)
    $expected = Get-BytesSha256 $Candidate $Offset $Count
    $Stream.Position = $Offset
    $Stream.Write($Candidate, $Offset, $Count)
    $Stream.Flush($true)
    $readback = New-Object byte[] $Count
    $Stream.Position = $Offset
    $done = 0
    while ($done -lt $Count) {
        $read = $Stream.Read($readback, $done, $Count - $done)
        if ($read -le 0) { throw "Short $Label readback at byte $done." }
        $done += $read
    }
    if ((Get-BytesSha256 $readback) -cne $expected) { throw "$Label readback mismatch." }
}

$evidence = [ordered]@{
    schema = 'gkd-mini-ar-pre-p1-transaction-v1'
    status = 'STARTED'
    validate_only = [bool]$ValidateOnly
    profile = $Profile
    expected_serial = $ExpectedSerial
    disk_number = $DiskNumber
    expected_live_sha256 = $ExpectedLiveSha256.ToLowerInvariant()
    expected_candidate_sha256 = $ExpectedCandidateSha256.ToLowerInvariant()
    live_sha256 = $null
    candidate_sha256 = $null
    backup_sha256 = $null
    readback_sha256 = $null
    changed_regions = @()
    error = $null
}
$raw = $null
$result = $null
try {
    Assert-Administrator
    $candidate = [IO.Path]::GetFullPath($CandidatePath)
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { throw 'Candidate not found.' }
    $backup = Assert-OutputPath $BackupPath 'BackupPath' -AllowExisting
    $result = Assert-OutputPath $ResultPath 'ResultPath'
    [void](Assert-TargetDisk)

    $candidateBytes = [IO.File]::ReadAllBytes($candidate)
    if ($candidateBytes.Length -ne $PrefixBytes) { throw 'Candidate size mismatch.' }
    $evidence.candidate_sha256 = Get-BytesSha256 $candidateBytes
    if ($evidence.candidate_sha256 -cne $evidence.expected_candidate_sha256) { throw 'Candidate SHA256 mismatch.' }

    $access = if ($ValidateOnly) { [IO.FileAccess]::Read } else { [IO.FileAccess]::ReadWrite }
    $options = if ($ValidateOnly) { [IO.FileOptions]::SequentialScan } else { [IO.FileOptions]::WriteThrough }
    $raw = [IO.FileStream]::new("\\.\PhysicalDrive$DiskNumber", [IO.FileMode]::Open,
        $access, [IO.FileShare]::None, (1MB), $options)
    $liveBytes = Read-ExactPrefix $raw
    $evidence.live_sha256 = Get-BytesSha256 $liveBytes
    if ($evidence.live_sha256 -cne $evidence.expected_live_sha256) { throw 'Live 20 MiB SHA256 mismatch.' }
    if ((Get-BytesSha256 $liveBytes 0 512) -cne $ExpectedMbrSha256) { throw 'Raw MBR identity mismatch.' }

    if ($Profile -ceq 'application-only') {
        Assert-EqualRange $liveBytes $candidateBytes 0 $ApplicationOffset 'MBR/selector/environment/frozen R'
        Assert-EqualRange $liveBytes $candidateBytes ($ApplicationOffset + $ApplicationBytes) ($PrefixBytes - $ApplicationOffset - $ApplicationBytes) 'post-A guard/trace/request'
    } else {
    Assert-EqualRange $liveBytes $candidateBytes 0 $EnvironmentOffset 'MBR through pre-environment'
    Assert-EqualRange $liveBytes $candidateBytes ($EnvironmentOffset + $EnvironmentActivationBytes) ($EnvironmentBytes - $EnvironmentActivationBytes) 'environment non-activation tail'
    Assert-EqualRange $liveBytes $candidateBytes ($SelectorOffset + $SelectorBytes) ($RecoveryOffset - $SelectorOffset - $SelectorBytes) 'selector-to-R gap'
    Assert-EqualRange $liveBytes $candidateBytes ($RecoveryOffset + $RecoveryBytes) ($PrefixBytes - $RecoveryOffset - $RecoveryBytes) 'A/capsule/guard/trace/request'
    }

    if (Test-Path -LiteralPath $backup) {
        $evidence.backup_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $backup).Hash.ToLowerInvariant()
        if ($evidence.backup_sha256 -cne $evidence.live_sha256) { throw 'Existing backup does not match live source.' }
    }
    else {
        $backupStream = [IO.FileStream]::new($backup, [IO.FileMode]::CreateNew,
            [IO.FileAccess]::Write, [IO.FileShare]::None, (1MB), [IO.FileOptions]::WriteThrough)
        try { $backupStream.Write($liveBytes, 0, $liveBytes.Length); $backupStream.Flush($true) }
        finally { $backupStream.Dispose() }
        $evidence.backup_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $backup).Hash.ToLowerInvariant()
        if ($evidence.backup_sha256 -cne $evidence.live_sha256) { throw 'Fresh backup SHA256 mismatch.' }
    }

    $regions = if ($Profile -ceq 'application-only') { @(
        [pscustomobject]@{ Name = 'application-a'; Offset = $ApplicationOffset; Count = $ApplicationBytes }
    ) } else { @(
        [pscustomobject]@{ Name = 'dedicated-r'; Offset = $RecoveryOffset; Count = $RecoveryBytes },
        [pscustomobject]@{ Name = 'selector'; Offset = $SelectorOffset; Count = $SelectorBytes },
        [pscustomobject]@{ Name = 'environment'; Offset = $EnvironmentOffset; Count = $EnvironmentActivationBytes }
    ) }
    $changed = @($regions | Where-Object {
        (Get-BytesSha256 $liveBytes $_.Offset $_.Count) -cne
        (Get-BytesSha256 $candidateBytes $_.Offset $_.Count)
    })
    $evidence.changed_regions = @($changed | ForEach-Object { $_.Name })

    if ($ValidateOnly) {
        $evidence.status = 'VALIDATED_READ_ONLY'
    }
    else {
        if ($Profile -ceq 'application-only') {
            if (@($changed | Where-Object Name -eq 'application-a').Count -eq 1) {
                Write-And-VerifyRange $raw $candidateBytes ($ApplicationOffset + 512) ($ApplicationBytes - 512) 'A body'
                Write-And-VerifyRange $raw $candidateBytes $ApplicationOffset 512 'A activation sector'
            }
        } else {
        $recoveryChanged = @($changed | Where-Object Name -eq 'dedicated-r').Count -eq 1
        if ($recoveryChanged) {
            Write-And-VerifyRange $raw $candidateBytes ($RecoveryOffset + $RecoveryActivationBytes) ($RecoveryBytes - $RecoveryActivationBytes) 'R body'
            Write-And-VerifyRange $raw $candidateBytes $RecoveryOffset $RecoveryActivationBytes 'R activation sector'
        }
        if (@($changed | Where-Object Name -eq 'selector').Count -eq 1) {
            Write-And-VerifyRange $raw $candidateBytes $SelectorOffset $SelectorBytes 'selector'
        }
        if (@($changed | Where-Object Name -eq 'environment').Count -eq 1) {
            Write-And-VerifyRange $raw $candidateBytes $EnvironmentOffset $EnvironmentActivationBytes 'environment activation sector'
        }
        }
        $readback = Read-ExactPrefix $raw
        $evidence.readback_sha256 = Get-BytesSha256 $readback
        if ($evidence.readback_sha256 -cne $evidence.candidate_sha256) { throw 'Complete 20 MiB readback mismatch.' }
        $evidence.status = 'WRITTEN_AND_VERIFIED'
    }
}
catch {
    $evidence.status = 'FAILED'
    $evidence.error = $_.Exception.Message
}
finally {
    if ($null -ne $raw) { $raw.Dispose() }
    if ($null -ne $result) {
        [IO.File]::WriteAllText($result, ($evidence | ConvertTo-Json -Depth 6) + [Environment]::NewLine,
            (New-Object Text.UTF8Encoding($false)))
    }
}

if ($evidence.status -eq 'VALIDATED_READ_ONLY' -or $evidence.status -eq 'WRITTEN_AND_VERIFIED') {
    Write-Output "GKD_AR_TRANSACTION=$($evidence.status)"
    exit 0
}
Write-Error $evidence.error
exit 1
