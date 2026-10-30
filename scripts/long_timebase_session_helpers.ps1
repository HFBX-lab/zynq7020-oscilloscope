function Get-AcqTimebaseSeconds([int]$Index){
 $values=@(.00005,.0001,.0002,.0005,.001,.002,.005,.01,.02,.05,.1,.2,.5)
 if($Highspeed10M -or $Highspeed25M -or $Highspeed50M){$values=@(.000001,.000002,.000005,.00001,.00002)+$values}
 if($Highspeed50M){$values=@(.0000001,.0000002,.0000005)+$values}
 if($Index -lt 0 -or $Index -ge $values.Count){throw 'Invalid timebase index'}
 return [double]$values[$Index]
}
# Loaded only by the authorized acquisition runner with its owned serial/server.
function Set-LongTimebase([int]$Index){
 if($Index -lt 0 -or $Index -gt $(if($Highspeed50M){20}elseif($Highspeed10M -or $Highspeed25M){17}else{12})){throw 'Invalid long-timebase index'}
 $s=Read-AcqState
 while([int]$s.ui.time -ne $Index){
  $next=[int]$s.ui.time+$(if([int]$s.ui.time -lt $Index){1}else{-1})
  Send-AcqCommands @($(if([int]$s.ui.time -lt $Index){'tap 950 274'}else{'tap 840 274'}))
  $s=Wait-AcqState {param($x) [int]$x.ui.time -eq $next}
 }
 return $s
}
function Assert-LongProfile($s,[int]$Index,[bool]$Aligned){
 $seconds=Get-AcqTimebaseSeconds $Index
 $legacyIndex=if($Highspeed10M -or $Highspeed25M -or $Highspeed50M){$Index-5}else{$Index}
 $rawMode=($Highspeed50M -and $Index -lt 6) -or ($Highspeed10M -and $Index -lt 5) -or ($Highspeed25M -and $Index -lt 4)
 $expectedFilter=if($rawMode){0}else{3}
 if(($Highspeed10M -or $Highspeed25M -or $Highspeed50M) -and (!$s.adc.ContainsKey('display_filter') -or !$s.adc.ContainsKey('display_conversion_hz') -or $s.adc.display_conversion_hz -ne $(if($Highspeed50M){'50000000'}elseif($Highspeed25M){'25000000'}else{'10000000'}))){throw 'Missing or mismatched highspeed frozen metadata'}
 $actualFilter=if($Highspeed10M -or $Highspeed25M -or $Highspeed50M){$s.adc.display_filter}else{$s.adc.filter_kind}
 $rate=if($Highspeed50M){if($Index -lt 6){50000000}elseif($Index -lt 9){6250000}elseif($Index -lt 12){625000}elseif($Index -lt 15){62500}elseif($Index -lt 18){6250}else{625}}
 elseif($Highspeed25M){if($Index -lt 4){25000000}elseif($Index -lt 6){6250000}elseif($Index -lt 9){625000}elseif($Index -lt 12){62500}elseif($Index -lt 15){6250}else{625}}
 elseif($rawMode){10000000}elseif($legacyIndex -lt 4){625000}elseif($legacyIndex -lt 7){62500}elseif($legacyIndex -lt 10){6250}else{625}
 if([int]$s.ui.time -ne $Index -or [int]$s.adc.display_rate -ne $rate -or [int]$actualFilter -ne $expectedFilter -or $s.adc.display_epoch -ne $s.ui.epoch){throw 'New display record did not match requested timebase/profile/epoch'}
 $start=[double]$s.ui.view_start;$crossing=[double]$s.ui.crossing
 if([double]::IsNaN($start) -or [double]::IsInfinity($start) -or [double]::IsNaN($crossing) -or [double]::IsInfinity($crossing) -or $start -lt 0){throw 'Invalid finite nonnegative record window'}
 $span=10*$seconds*$rate
 if($span -gt 4095 -or [double]$s.ui.view_start+$span -gt 4095.004){throw 'Long window exceeds actual raw record'}
 if($Aligned -and ($s.ui.triggered -ne '1' -or [math]::Abs([double]$s.ui.crossing-[double]$s.ui.view_start-.2*$span) -gt .004)){throw 'Long record20percent alignment mismatch'}
}
function Export-LongRecord([string]$Name,[bool]$Frame=$false){
 $s=Read-AcqState
 if($s.ui.running -ne '0'){throw 'Long record export requires a frozen acquisition'}
 if($HardwareTrigger -and ($s.adc.id -ne $s.ui.display_id -or $s.adc.display_id -ne $s.ui.display_id)){throw 'Frozen hardware metadata does not identify displayed record'}
 $record=$s.adc.display_address;$crc=$s.adc.display_crc;$rate=[int]$s.adc.display_rate
 if($record -notmatch '^1300[04]000$' -or $crc -notmatch '^[0-9a-fA-F]{8}$' -or $rate -notin @(50000000,25000000,10000000,6250000,625000,62500,6250,625)){throw 'Invalid long-record metadata'}
 $frameAddress='';$frameCrc=''
 if($Frame){
  $snapshot=Invoke-AcqCommand 'snapshot' '(?m)^SNAPSHOT_READY[^\r\n]+\r?\n';$state.resume_needed=$true
  $fields=Convert-AcqFields $snapshot;$frameAddress=$fields.addr;$frameCrc=$fields.crc
  if($frameAddress -notmatch '^10[02]00000$' -or $frameCrc -notmatch '^[0-9a-fA-F]{8}$' -or $fields.bytes -ne '1843200' -or $fields.format -ne 'BGR888'){throw 'Invalid long frame metadata'}
 }
 $frameRead=if($Frame){"mrd -bin -file {$run/$Name-frame.bin} 0x$frameAddress 460800"}else{''}
 @'
if {[catch {
connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "APU"}
configparams force-mem-accesses 1
mrd -bin -file {@RUN@/@NAME@.bin} 0x@RECORD@ 4096
@FRAME@
puts LONG_RECORD_EXPORTED
disconnect
} e opts]} {puts stderr "EXPORT_ERROR: $e";exit 1}
exit
'@.Replace('@RUN@',$run).Replace('@NAME@',$Name).Replace('@RECORD@',$record).Replace('@FRAME@',$frameRead) | Set-Content "$run/export-$Name.tcl" -Encoding ascii
 & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat "$run/export-$Name.tcl" *> "$run/export-$Name.log"
 if($LASTEXITCODE -or !(Select-String "$run/export-$Name.log" -Pattern '^LONG_RECORD_EXPORTED' -Quiet)){throw 'Long raw export failed'}
 & D:/Python/python.exe -X utf8 "$run/analyze_adc_live_record.py" "$run/$Name.bin" --rate $rate --crc $crc --output "$run/$Name.json" *> "$run/$Name-audit.log"
 if($LASTEXITCODE){throw 'Independent long record CRC/rate audit failed'}
 $entry=@{name=$Name;id=$s.ui.display_id;rate=$rate;crc32=$crc;sha256=(Get-FileHash "$run/$Name.bin").Hash;ui=$s.ui;adc=$s.adc}
 if($Frame){
  & D:/Python/python.exe -X utf8 "$run/decode_board_frame.py" "$run/$Name-frame.bin" $frameCrc "$run/$Name-frame.png" *> "$run/$Name-frame-audit.log"
  if($LASTEXITCODE){throw 'Long actual frame CRC audit failed'}
  $entry.frame_crc=$frameCrc;$entry.frame_sha256=(Get-FileHash "$run/$Name-frame.bin").Hash
  Send-AcqCommands @('resume');$state.resume_needed=$false
 }
 $state.long_records+=@($entry)
 Note-Acq "$Name-independent-DDR-CRC-and-timebase" $s
}
function Test-LongTimebaseControls {
 $state.long_records=@()
 Send-AcqCommands @('source 2','trigsource 2','trigger rise 500','triggermode auto','dual 1','cursors 0','acqrun')
 # Current known1kHz signal is inside the useful passband of62.5kSa/s.
 foreach($index in @(4,5,6)){
  $before=Read-AcqState;$old=[uint32]$before.ui.display_id
  [void](Set-LongTimebase $index)
  $s=Wait-AcqState {param($x) [uint32]$x.ui.display_id -gt $old -and $x.adc.display_rate -eq '62500' -and $x.adc.display_epoch -eq $x.ui.epoch -and $x.ui.triggered -eq '1'}
  Assert-LongProfile $s $index $true;Note-Acq "long-timebase-$index-full-triggered-window" $s
  Send-AcqCommands @('single');$s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0'}
  Assert-LongProfile $s $index $true;Note-Acq "long-timebase-$index-single-complete" $s
  if($index -eq 6){Export-LongRecord 'long-5ms' $true}
  Send-AcqCommands @('acqrun')
 }
 foreach($index in @(7,8,9,10,11,12)){
  $before=Read-AcqState;$old=[uint32]$before.ui.display_id
  [void](Set-LongTimebase $index)
  $targetRate=if($index -lt 10){6250}else{625}
  $s=Wait-AcqState {param($x) [uint32]$x.ui.display_id -gt $old -and [int]$x.adc.display_rate -eq $targetRate -and $x.adc.display_epoch -eq $x.ui.epoch} 13
  Assert-LongProfile $s $index $false;Note-Acq "long-timebase-$index-full-auto-window" $s
  if($index -eq 9){
   Send-AcqCommands @('acqstop');Export-LongRecord 'long-50ms';Send-AcqCommands @('acqrun')
  }
 }
 # At625Sa/s the1kHz input is intentionally filtered; do not force a false
 # positive trigger by lowering the level. Require a complete AUTO record.
 Send-AcqCommands @('acqstop');Export-LongRecord 'long-500ms' $true
 $held=Read-AcqState;$heldId=$held.ui.display_id;$heldCrc=$held.adc.display_crc
 Send-AcqCommands @('acqrun');$s=Wait-AcqState {param($x) $x.adc.pending -eq '1'}
 Send-AcqCommands @('source 1');$s=Read-AcqState
 $epoch=$s.ui.epoch
 Send-AcqCommands @('source 2');$s=Read-AcqState
 if($s.ui.epoch -ne $epoch){throw 'Measurement switch restarted slow capture'}
 Note-Acq 'slow-pending-measurement-switch-keeps-epoch' $s
 Send-AcqCommands @('acqstop');$s=Wait-AcqState {param($x) $x.adc.pending -eq '0' -and $x.ui.running -eq '0'}
 if($s.ui.display_id -ne $heldId -or $s.adc.display_crc -ne $heldCrc){throw 'Slow capture cancellation modified held record'}
 Note-Acq 'slow-capture-cancel-keeps-frozen-record' $s
 # Out-of-range trigger at slow rate must remain armed beyond old100ms.
 Send-AcqCommands @('trigger rise 4500','single')
 $s=Wait-AcqState {param($x) $x.ui.single_armed -eq '1' -and $x.adc.pending -eq '1'}
 $raw=[uint32]$s.adc.id;$captureClock=[Diagnostics.Stopwatch]::StartNew()
 $s=Wait-AcqState {param($x) [uint32]$x.adc.id -gt $raw -and $x.ui.single_armed -eq '1'} 13
 $state.slow_capture_elapsed_seconds=$captureClock.Elapsed.TotalSeconds
 if($state.slow_capture_elapsed_seconds -lt 6){throw 'Slow record completed too early for4096at625Sa/s'}
 if($s.ui.single_complete -ne '0' -or $s.ui.display_id -ne $heldId -or $s.adc.display_crc -ne $heldCrc){throw 'Slow no-trigger single falsely completed or modified display'}
 Note-Acq 'slow-single-complete-record-without-match-keeps-waiting' $s
 Send-AcqCommands @('acqstop','trigger rise 500');[void](Set-LongTimebase 2)
 Send-AcqCommands @('single');$s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.adc.display_rate -eq '625000' -and $x.adc.display_epoch -eq $x.ui.epoch}
 Assert-LongProfile $s 2 $true;Note-Acq 'fast-single-restores-after-slow-cancellation' $s
 Export-LongRecord 'long-fast-restored'
 Send-AcqCommands @('triggermode auto','dual 1','cursors 0','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.ui.triggered -eq '1' -and $x.adc.display_rate -eq '625000'}
 Note-Acq 'final-CH2-200us-div-auto-running' $s
}
