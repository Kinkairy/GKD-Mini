[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$TaskDirectory,
    [Parameter(Mandatory=$true)][string]$WriterSha256,
    [Parameter(Mandatory=$true)][string]$SlotSha256,
    [int]$DiskNumber=1,
    [ValidateSet('GKDROUND64', 'GKDROUND64R5')] [string]$ExpectedSerial='GKDROUND64',
    [switch]$Apply
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$Profile='application-only'
$writer=Join-Path $TaskDirectory 'invoke-ar-pre-p1-transaction.ps1'
$metadataPath=Join-Path $TaskDirectory 'preflight.json'
$validatePath=Join-Path $TaskDirectory 'validate.json'
try {
    if ((Get-FileHash -LiteralPath $writer -Algorithm SHA256).Hash.ToLowerInvariant() -cne $WriterSha256) { throw 'Writer identity mismatch' }
    if ($Apply) {
        $meta=Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
        $validation=Get-Content -LiteralPath $validatePath -Raw | ConvertFrom-Json
        if ($validation.status -cne 'VALIDATED_READ_ONLY' -or $validation.profile -cne $Profile -or
            $validation.expected_serial -cne $ExpectedSerial -or $meta.expected_serial -cne $ExpectedSerial -or
            @($validation.changed_regions).Count -ne 1 -or $validation.changed_regions[0] -cne 'application-a' -or
            $meta.slot_sha256 -cne $SlotSha256 -or $meta.disk_number -ne $DiskNumber -or
            $validation.live_sha256 -cne $meta.live_sha256 -or $validation.candidate_sha256 -cne $meta.candidate_sha256) { throw 'A-only validation mismatch' }
        & $writer -Profile $Profile -ExpectedSerial $ExpectedSerial -DiskNumber $DiskNumber -CandidatePath $meta.candidate_path -ExpectedCandidateSha256 $meta.candidate_sha256 -ExpectedLiveSha256 $meta.live_sha256 -BackupPath $meta.backup_path -ResultPath (Join-Path $TaskDirectory 'write.json')
        exit $LASTEXITCODE
    }
    if (Test-Path -LiteralPath $metadataPath) { throw 'Existing preflight must be reused' }
    $source=[IO.File]::ReadAllText($writer)
    $start=$source.IndexOf('Set-StrictMode -Version Latest',[StringComparison]::Ordinal)
    $stop=$source.IndexOf('$evidence = [ordered]@{',[StringComparison]::Ordinal)
    if ($start -lt 0 -or $stop -le $start) { throw 'Writer layout mismatch' }
    . ([scriptblock]::Create($source.Substring($start,$stop-$start)))
    Assert-Administrator
    [void](Assert-TargetDisk)
    $raw=[IO.FileStream]::new("\\.\PhysicalDrive$DiskNumber",[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None,1MB,[IO.FileOptions]::SequentialScan)
    try { [byte[]]$live=Read-ExactPrefix $raw } finally { $raw.Dispose() }
    if ((Get-BytesSha256 $live 0 512) -cne $ExpectedMbrSha256) { throw 'MBR mismatch' }
    $rHash=Get-BytesSha256 $live $RecoveryOffset $RecoveryBytes
    if ($rHash -cne '8b6069db6df73dd56a05117520acc43adcaab74847b8be4799daf68be3abb3ca') { throw 'Frozen R mismatch' }
    [byte[]]$slot=[IO.File]::ReadAllBytes((Join-Path $TaskDirectory 'application-a-slot.bin'))
    if ($slot.Length -ne $ApplicationBytes -or (Get-BytesSha256 $slot) -cne $SlotSha256) { throw 'Slot mismatch' }
    [byte[]]$candidate=$live.Clone()
    [Array]::Copy($slot,0,$candidate,$ApplicationOffset,$ApplicationBytes)
    Assert-EqualRange $live $candidate 0 $ApplicationOffset 'before A'
    Assert-EqualRange $live $candidate ($ApplicationOffset+$ApplicationBytes) ($PrefixBytes-$ApplicationOffset-$ApplicationBytes) 'after A'
    $candidatePath=Join-Path $TaskDirectory 'candidate-prefix.bin'
    $backupPath=Join-Path $TaskDirectory 'outgoing-prefix.bin'
    $output=[IO.FileStream]::new($candidatePath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $output.Write($candidate,0,$candidate.Length); $output.Flush($true) } finally { $output.Dispose() }
    $meta=[ordered]@{ disk_number=$DiskNumber; expected_serial=$ExpectedSerial; slot_sha256=$SlotSha256; frozen_r_sha256=$rHash;
        outgoing_a_sha256=(Get-BytesSha256 $live $ApplicationOffset $ApplicationBytes);
        live_sha256=(Get-BytesSha256 $live); candidate_sha256=(Get-BytesSha256 $candidate);
        candidate_path=$candidatePath; backup_path=$backupPath; outside_a_unchanged=$true }
    [IO.File]::WriteAllText($metadataPath,($meta|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    & $writer -Profile $Profile -ExpectedSerial $ExpectedSerial -DiskNumber $DiskNumber -CandidatePath $candidatePath -ExpectedCandidateSha256 $meta.candidate_sha256 -ExpectedLiveSha256 $meta.live_sha256 -BackupPath $backupPath -ResultPath $validatePath -ValidateOnly
    exit $LASTEXITCODE
} catch {
    [IO.File]::WriteAllText((Join-Path $TaskDirectory 'error.txt'),$_.Exception.Message,[Text.UTF8Encoding]::new($false))
    exit 1
}
