# Offline mocks only: never opens a raw disk or calls the transaction body.
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$source=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'invoke-ar-pre-p1-transaction.ps1') -Raw
$start=$source.IndexOf('Set-StrictMode -Version Latest')
$stop=$source.IndexOf('$evidence = [ordered]@{')
if($start -lt 0 -or $stop -le $start){throw 'Writer layout mismatch'}
. ([scriptblock]::Create($source.Substring($start,$stop-$start)))
$DiskNumber=1
$fixture=[pscustomobject]@{SerialNumber='GKDROUND64'; BusType='USB'; PartitionStyle='MBR';
    FriendlyName='Linux File-Stor Gadget'; Size=[long]31457280000; Signature=[uint32]0x0007130c;
    IsBoot=$false; IsSystem=$false; IsOffline=$false; OperationalStatus=@('Online')}
$parts=@($ExpectedPartitions | ForEach-Object {
    [pscustomobject]@{PartitionNumber=$_.Number; MbrType=$_.Type; Offset=$_.Offset;
        Size=$_.Size; DriveLetter=[char]0}
})
function Get-Disk { param($Number) $fixture }
function Get-Partition { param($DiskNumber) $parts }
function Must-Reject {
    $rejected=$false
    try { [void](Assert-TargetDisk) } catch { $rejected=$true }
    if(-not $rejected){throw 'Identity gate accepted invalid target'}
}
$Profile='application-only'; $ExpectedSerial='GKDROUND64'
[void](Assert-TargetDisk)
$Profile='recovery-selector'
[void](Assert-TargetDisk)
$fixture.SerialNumber='GKDROUND64R5'; $ExpectedSerial='GKDROUND64R5'
Must-Reject
$Profile='application-only'
[void](Assert-TargetDisk)
$ExpectedSerial='GKDROUND64'
Must-Reject
$ExpectedSerial='GKDROUND64R5'; $fixture.Size=0
Must-Reject
$fixture.Size=[long]31457280000; $fixture.IsSystem=$true
Must-Reject
$fixture.IsSystem=$false; $parts[0].DriveLetter=[char]'D'
Must-Reject
$parts[0].DriveLetter=[char]0; $parts[0].Offset++
Must-Reject
'GKD_TRANSACTION_IDENTITY=PASS accepted=3 rejected=6 raw_access=none'
