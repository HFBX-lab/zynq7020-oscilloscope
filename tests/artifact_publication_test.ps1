$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '..\hardware\artifact-publication.ps1')
$testRoot=Join-Path (Split-Path $PSScriptRoot -Parent) ('build\publication-test-'+[guid]::NewGuid().ToString('N'))
$evidence=Join-Path $testRoot 'evidence'
$run=Join-Path $testRoot 'run'
New-Item -ItemType Directory -Path $evidence,$run -Force | Out-Null
Set-Content -LiteralPath (Join-Path $evidence 'lcd_grid.bit') -Value 'previous-success'
Set-Content -LiteralPath (Join-Path $evidence 'source-hashes.json') -Value '[{"Hash":"OLD"}]'
Set-Content -LiteralPath (Join-Path $run 'lcd_grid.bit') -Value 'stale-run-output'
$a=Start-FpgaBuildAttempt -EvidenceDir $evidence -Pattern grid -SourceHashes @(@{Hash='NEW'}) -RunDir $run
Complete-FpgaBuildAttempt -Attempt $a -RunDir $run -Succeeded $false -Failure 'negative timing slack'
if(Test-Path (Join-Path $evidence 'lcd_grid.bit')){throw 'Failed build published stale bitstream'}
if(Test-Path (Join-Path $evidence 'source-hashes.json')){throw 'Failed build published new source as successful'}
$status=Get-Content (Join-Path $evidence 'build-status.json') -Raw | ConvertFrom-Json
if($status.state -ne 'failed' -or $status.artifact){throw 'Failure state not explicit'}
if(-not (Get-ChildItem (Join-Path $evidence 'history') -Filter lcd_grid.bit -Recurse)){throw 'Previous successful bitstream was not preserved'}
$b=Start-FpgaBuildAttempt -EvidenceDir $evidence -Pattern grid -SourceHashes @(@{Hash='CURRENT'}) -RunDir $run
Set-Content -LiteralPath (Join-Path $run 'lcd_grid.bit') -Value 'fresh-success'
Complete-FpgaBuildAttempt -Attempt $b -RunDir $run -Succeeded $true
$status=Get-Content (Join-Path $evidence 'build-status.json') -Raw | ConvertFrom-Json
if($status.state -ne 'succeeded' -or $status.artifact.sha256 -ne (Get-FileHash (Join-Path $evidence 'lcd_grid.bit')).Hash){throw 'Successful publication hash mismatch'}
if((Get-Content (Join-Path $evidence 'source-hashes.json') -Raw | ConvertFrom-Json).Hash -ne 'CURRENT'){throw 'Successful source provenance mismatch'}
$c=Start-FpgaBuildAttempt -EvidenceDir $evidence -Pattern grid -SourceHashes @(@{Hash='LATEST'}) -RunDir $run
Set-Content -LiteralPath (Join-Path $run 'lcd_grid.bit') -Value 'unexpected-old-output'
[IO.File]::SetLastWriteTimeUtc((Join-Path $run 'lcd_grid.bit'),[DateTime]::UtcNow.AddDays(-1))
$rejected=$false
try {Complete-FpgaBuildAttempt -Attempt $c -RunDir $run -Succeeded $true} catch {
  if($_.Exception.Message -ne 'PASS had no freshly written bitstream'){throw}
  $rejected=$true
}
if(-not $rejected -or (Test-Path (Join-Path $evidence 'lcd_grid.bit'))){throw 'Stale output accepted as a new success'}
Write-Output 'PASS: failed build cannot publish old bitstream; previous success preserved; fresh success paired with source and artifact hashes'
