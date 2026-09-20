$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot;$writer=Join-Path $root '写入-GKD卡.ps1'
$t=$null;$e=$null;[Management.Automation.Language.Parser]::ParseFile($writer,[ref]$t,[ref]$e)|Out-Null;if($e){throw ($e|Out-String)}
$text=[IO.File]::ReadAllText($writer,[Text.Encoding]::UTF8)
foreach($required in @('FileShare]::None','FileOptions]::WriteThrough','function ReadExact','untrusted package signature','launcher must be last','rollback','journal')){if($text -notmatch [Regex]::Escape($required)){throw "missing $required"}}
if($text -match '\\\\.\\PhysicalDrive[0-9]'){throw 'writer hardcodes a physical disk'}
$m=[IO.MemoryStream]::new([byte[]](1,2,3,4));$b=New-Object byte[] 4;if($m.Read($b,0,4)-ne4 -or ($b-join ',')-ne'1,2,3,4'){throw 'MemoryStream exact read'}
$m=[IO.MemoryStream]::new([byte[]](1,2));$b=New-Object byte[] 4;if($m.Read($b,0,4)-ne2){throw 'MemoryStream partial read'}
'PS51_AST_MEMORYSTREAM_PASS'
