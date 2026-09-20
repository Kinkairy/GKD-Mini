$ErrorActionPreference = 'Stop'
$path = Join-Path $PSScriptRoot '..\windows\invoke-ar-pre-p1-transaction.ps1'
$tokens = $null
$errors = $null
[void][Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
if ($errors.Count -ne 0) { throw ($errors | Out-String) }
$text = Get-Content -LiteralPath $path -Raw
foreach ($required in @(
    "'Linux File-Stor Gadget'", "'GKDROUND64'", '31457280000', '0x0007130c',
    '$RecoveryOffset + $RecoveryActivationBytes', "'R activation sector'", "'selector'",
    "'environment activation sector'", "'Complete 20 MiB readback mismatch.'",
    "'environment non-activation tail'", "'A/capsule/guard/trace/request'",
    "'Fresh backup SHA256 mismatch.'"
)) {
    if (-not $text.Contains($required)) { throw "Missing writer contract token: $required" }
}
if ($text.IndexOf("'R body'") -gt $text.IndexOf("'R activation sector'")) { throw 'R write order drift.' }
if ($text.IndexOf("'R activation sector'") -gt $text.LastIndexOf("'selector'")) { throw 'Selector write order drift.' }
if ($text.LastIndexOf("'selector'") -gt $text.IndexOf("'environment activation sector'")) { throw 'Environment must activate last.' }
Write-Output 'GKD_AR_WRITER_CONTRACT=PASS'
