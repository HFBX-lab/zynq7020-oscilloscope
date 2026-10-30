# Framed TF diagnostics; dot sourcing does not access hardware.
function Assert-StorageVideoStatus([string]$Line){
 if($Line -notmatch '^APP_DIAG '){throw 'Not a video diagnostic'}
 $f=Convert-AcqFields $Line
 if($f.locked -ne '1' -or $f.pixel_locked -ne '1' -or $f.dma_errors -ne '00000000' -or $f.ms -notmatch '^\d+$' -or $f.frames -notmatch '^\d+$'){throw 'Video diagnostic fields invalid'}
 return $f
}
function Convert-TfStatusFrame([string]$Body){
 $h=[regex]::Matches($Body,'(?m)^TF_STATUS [^\r\n]+\r?\n');$m=[regex]::Matches($Body,'(?m)^TF_MEDIA [^\r\n]+\r?\n');$b=[regex]::Matches($Body,'(?m)^TF_BLOB [^\r\n]+\r?\n')
 if($h.Count -ne 1 -or $m.Count -ne 1 -or $b.Count -ne 1 -or $h[0].Index -gt $m[0].Index -or $m[0].Index -gt $b[0].Index){throw 'Incomplete/duplicate/unordered TF frame'}
 $status=Convert-AcqFields $h[0].Value;$media=Convert-AcqFields $m[0].Value;$blob=Convert-AcqFields $b[0].Value
 foreach($key in @('busy','ready','result','seq','id','kind')){if(!$status.ContainsKey($key) -or $status[$key] -notmatch '^\d+$'){throw 'Missing/invalid TF status field'}}
 if($status.busy -notin @('0','1') -or $status.ready -notin @('0','1') -or $status.kind -notin @('0','1') -or [uint64]$status.result -gt 9){throw 'Invalid TF flags'}
 if($media.cid -notmatch '^[0-9a-fA-F]{32}$' -or $media.fat -notin @('0','1','2','3') -or $media.sector_count -notmatch '^\d+$'){throw 'Invalid TF media identity'}
 if($blob.address -notmatch '^[0-9a-fA-F]{8}$' -or $blob.crc -notmatch '^[0-9a-fA-F]{8}$' -or $blob.bytes -notmatch '^\d+$'){throw 'Invalid TF blob range/CRC'}
 if([uint64]$blob.bytes -ne 0){
  if($status.busy -ne '0' -or $status.result -ne '0' -or $blob.bytes -ne $(if($status.kind -eq '0'){'16512'}else{'96'})){throw 'TF error/busy/length inconsistent with blob'}
 }elseif($blob.address -ne '00000000' -or $blob.crc -ne '00000000'){throw 'Empty TF blob has nonzero address/CRC'}
 return @{status=$status;media=$media;blob=$blob}
}
function Read-TfState {
 [void](Invoke-AcqCommand '__query_boundary__' '(?m)^COMMANDS:[^\r\n]+\r?\n');$begin=$captured.Length
 [void](Invoke-AcqCommand 'tfstatus' '(?m)^TF_BLOB [^\r\n]+\r?\n')
 return Convert-TfStatusFrame $captured.ToString().Substring($begin)
}
function Invoke-TfAction([string]$Command,[int]$ExpectedResult=0){
 $ack=Invoke-AcqCommand $Command '(?m)^TF_COMMAND [^\r\n]+\r?\n'
 if($ack -notmatch ' QUEUED seq=(\d+)'){throw "TF action rejected: $Command"};$sequence=$Matches[1]
 $watch=[Diagnostics.Stopwatch]::StartNew()
 do{$s=Read-TfState;if($s.status.seq -eq $sequence -and $s.status.busy -eq '0'){break};if($watch.Elapsed.TotalSeconds -gt 30){throw "TF action timeout: $Command"};Start-Sleep -Milliseconds 50}while($true)
 $state.tf_actions+=@{command=$Command;sequence=$sequence;status=$s}
 if([int]$s.status.result -ne $ExpectedResult){throw "TF unexpected result for $Command : $($s.status.result)"}
 return $s
}
