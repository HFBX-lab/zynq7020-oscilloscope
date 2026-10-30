# Keep the last success recoverable; never label an old .bit as a new build.
function Start-FpgaBuildAttempt {
  param([string]$EvidenceDir,[string]$Pattern,[object[]]$SourceHashes,[string]$RunDir)
  New-Item -ItemType Directory -Path $EvidenceDir -Force | Out-Null
  $id=(Get-Date -Format 'yyyyMMdd-HHmmss-ffff')+'-'+[guid]::NewGuid().ToString('N').Substring(0,6)
  $history=Join-Path $EvidenceDir "history\$id"
  $previous=@(Get-ChildItem -LiteralPath $EvidenceDir -File | Where-Object {$_.Extension -eq '.bit' -or $_.Name -in 'source-hashes.json','build-status.json'})
  if($previous.Count){
    New-Item -ItemType Directory -Path $history -Force | Out-Null
    foreach($file in $previous){Move-Item -LiteralPath $file.FullName -Destination $history}
  }
  if($RunDir){
    $oldRunBit=Join-Path $RunDir ('lcd_'+$Pattern+'.bit')
    if(Test-Path -LiteralPath $oldRunBit){
      $runHistory=Join-Path $RunDir "history\$id"
      New-Item -ItemType Directory -Path $runHistory -Force | Out-Null
      Move-Item -LiteralPath $oldRunBit -Destination $runHistory
    }
  }
  $attempt=@{EvidenceDir=$EvidenceDir;Pattern=$Pattern;StartedUtc=[DateTime]::UtcNow;SourceHashes=$SourceHashes}
  @{state='building';pattern=$Pattern;started_utc=$attempt.StartedUtc.ToString('o');artifact=$null;source_hashes=$SourceHashes} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'build-status.json') -Encoding utf8
  return $attempt
}

function Complete-FpgaBuildAttempt {
  param([hashtable]$Attempt,[string]$RunDir,[bool]$Succeeded,[string]$Failure='')
  $dest=$Attempt.EvidenceDir
  Get-ChildItem -LiteralPath $RunDir -File | Where-Object {$_.Extension -in '.log','.rpt'} | Copy-Item -Destination $dest -Force
  $artifact=$null
  $source=Join-Path $RunDir ('lcd_'+$Attempt.Pattern+'.bit')
  # Previous run output was removed before the command. Tolerate timestamp rounding
  # between .NET wall clock and filesystem last-write time; existence is now fresh.
  if($Succeeded -and (-not (Test-Path -LiteralPath $source) -or (Get-Item -LiteralPath $source).LastWriteTimeUtc.AddSeconds(1) -lt $Attempt.StartedUtc)){
    $Succeeded=$false;$Failure='PASS had no freshly written bitstream'
  }
  if($Succeeded){
    Copy-Item -LiteralPath $source -Destination $dest -Force
    $artifact=@{file=(Split-Path $source -Leaf);sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash}
    @($Attempt.SourceHashes) | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $dest 'source-hashes.json') -Encoding utf8
  }
  @{state=$(if($Succeeded){'succeeded'}else{'failed'});pattern=$Attempt.Pattern;
    started_utc=$Attempt.StartedUtc.ToString('o');finished_utc=[DateTime]::UtcNow.ToString('o');
    artifact=$artifact;source_hashes=$Attempt.SourceHashes;failure=$Failure} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $dest 'build-status.json') -Encoding utf8
  if(-not $Succeeded -and $Failure -eq 'PASS had no freshly written bitstream'){throw $Failure}
}
