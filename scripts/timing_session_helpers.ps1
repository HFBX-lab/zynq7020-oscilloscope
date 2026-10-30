# New timing behavior only, inside the parent runner's owned session.
function Convert-TimingStatusFrame([string]$Body){
 $headers=[regex]::Matches($Body,'(?m)^MEASURE_STATUS [^\r\n]+\r?\n')
 $rows=[regex]::Matches($Body,'(?m)^MEASURE_TIMING [^\r\n]+\r?\n')
 $ends=[regex]::Matches($Body,'(?m)^MEASURE_END [^\r\n]+\r?\n')
 if($headers.Count -ne 1 -or $rows.Count -ne 2 -or $ends.Count -ne 1){throw 'Incomplete timing reply'}
 $h=Convert-AcqFields $headers[0].Value;$e=Convert-AcqFields $ends[0].Value
 if($h.display_id -ne $e.display_id -or [int]$h.rate -le 0 -or $h.count -ne '4096' -or $h.timing -notin @('0','1')){throw 'Mixed/invalid timing context'}
 if($headers[0].Index -gt $rows[0].Index -or $rows[1].Index -gt $ends[0].Index){throw 'Unordered timing reply'}
 $out=@{context=$h;channels=@()}
 for($ch=0;$ch -lt 2;$ch++){
  $r=Convert-AcqFields $rows[$ch].Value
  if([int]$r.ch -ne $ch+1){throw 'Wrong timing channel'}
  foreach($key in @('pulse_valid','rise_valid','fall_valid')){if($r[$key] -notin @('0','1')){throw 'Invalid timing validity flag'}}
  foreach($key in @('cycles','duty_percent','positive_us','negative_us','period_us','rise_us','fall_us','low_v','mid_v','high_v')){
   if(!$r.ContainsKey($key)){throw "Missing timing $key"}
   $value=[double]$r[$key];if([double]::IsNaN($value) -or [double]::IsInfinity($value)){throw 'Nonfinite timing'}
  }
  if($r.pulse_valid -eq '1'){
   if([int]$r.cycles -lt 2 -or [double]$r.duty_percent -le 0 -or [double]$r.duty_percent -ge 100 -or [double]$r.positive_us -le 0 -or [double]$r.negative_us -le 0 -or [double]$r.period_us -le 0){throw 'Invalid timing pulse'}
   if([math]::Abs([double]$r.positive_us+[double]$r.negative_us-[double]$r.period_us) -gt .000003 -or [math]::Abs(100*[double]$r.positive_us/[double]$r.period_us-[double]$r.duty_percent) -gt [math]::Max(.0001,.0002/[double]$r.period_us)){throw 'Inconsistent timing pulse'}
   if([double]$r.low_v -ge [double]$r.mid_v -or [double]$r.mid_v -ge [double]$r.high_v){throw 'Invalid timing thresholds'}
  }else{
   foreach($key in @('cycles','duty_percent','positive_us','negative_us','period_us')){if([double]$r[$key] -ne 0){throw 'Invalid pulse retained numbers'}}
   if($r.rise_valid -ne '0' -or $r.fall_valid -ne '0'){throw 'Edge valid without cycles'}
  }
  foreach($direction in @('rise','fall')){
   $value=[double]$r[($direction+'_us')]
   if(($r[($direction+'_valid')] -eq '0' -and $value -ne 0) -or ($r[($direction+'_valid')] -eq '1' -and $value -lt 2000000.0/[double]$h.rate-.000001)){throw 'Invalid/unresolved timing edge'}
  }
  $out.channels+=@($r)
 }
 return $out
}
function Read-TimingState {
 [void](Invoke-AcqCommand '__query_boundary__' '(?m)^COMMANDS:[^\r\n]+\r?\n');$begin=$captured.Length
 [void](Invoke-AcqCommand 'measurestatus' '(?m)^MEASURE_END display_id=\d+\r?\n')
 return Convert-TimingStatusFrame $captured.ToString().Substring($begin)
}
function Read-TimingSnapshot {
 $s=Read-ParameterSnapshot;$s.timing=Read-TimingState
 if($s.ui.running -ne '0' -or $s.ui.display_id -ne $s.timing.context.display_id -or $s.ui.epoch -ne $s.timing.context.epoch -or $s.memory.view.rate -ne $s.timing.context.rate){throw 'Timing mixed displayed record'}
 return $s
}
function Note-Timing([string]$Name,$Snapshot){$state.timing_checks+=@{test=$Name;snapshot=$Snapshot};[void](Note-Acq $Name $Snapshot)}
function Export-Timing([string]$Name){
 $before=Read-TimingSnapshot
 if($before.timing.context.panel -ne '1' -or $before.timing.context.timing -ne '1'){throw 'Timing page not visible for export'}
 if($before.memory.status.replay -eq '1'){
  $oldExports=$state.memory_exports;$oldChecks=$state.memory_checks
  try{$entry=Export-Memory $Name $true}finally{$state.memory_exports=$oldExports;$state.memory_checks=$oldChecks}
 }
 else{Export-LongRecord $Name $true;$entry=$state.long_records[-1]}
 $after=Read-TimingSnapshot;Assert-SpectrumDataHeld $before $after
 $entry.timing_snapshot=$before;$state.timing_exports+=@($entry)
 Note-Timing "$Name-numeric-raw-frame" $after
}
function Test-TimingControls {
 $state.timing_checks=@();$state.timing_exports=@();$prior=$state.long_records;$state.long_records=@()
 try{
  Send-AcqCommands @('fft 0','measurements 0','source 2','trigsource 2','trigger rise 500','triggermode auto','dual 1','cursors 0','acqrun')
  [void](Set-LongTimebase 5);$null=Wait-Highspeed50MSingle 5
  Send-AcqCommands @('memsave','paramsave','measurements 1')
  $before=Read-TimingSnapshot
  Send-AcqCommands @('tap 806 162');$null=Wait-AcqState {param($x) $x.ui.touch_pending -eq '0'}
  $s=Read-TimingSnapshot;Assert-SpectrumDataHeld $before $s
  if($s.timing.context.timing -ne '1' -or $s.timing.channels[1].pulse_valid -ne '1' -or [math]::Abs([double]$s.timing.channels[1].period_us-20) -gt 1){throw 'Known sine timing unavailable'}
  Export-Timing 'timing-raw50m-ch2';$saved=Read-TimingSnapshot
  Send-AcqCommands @('tap 238 162');$null=Wait-AcqState {param($x) $x.ui.touch_pending -eq '0'}
  $s=Read-TimingSnapshot;Assert-ParameterRawHeld $saved $s 0
  if($s.ui.source -ne 'ADC1' -or $s.timing.channels[0].pulse_valid -ne '0'){throw 'Empty CH1 invented timing'}
  Export-Timing 'timing-raw50m-ch1'
  Send-AcqCommands @('source 2','vertical 2 3 1000');$s=Read-TimingSnapshot
  Assert-ParameterRawHeld $saved $s 0
  Send-AcqCommands @('measurements 0');[void](Set-LongTimebase 6);Send-AcqCommands @('timing 1');$s=Read-TimingSnapshot
  if($s.timing.context.rate -ne '50000000'){throw 'Frozen timing relabeled requested rate'}
  Note-Timing 'timing-frozen50m-keeps-source-rate' $s
  Send-AcqCommands @('vertical 2 2 0','timing 0');$rejectStart=$captured.Length
  Send-AcqCommands @('timing 2');$s=Read-TimingSnapshot
  if($captured.ToString().Substring($rejectStart) -notmatch '(?m)^TIMING_COMMAND REJECTED\r?$'){throw 'Invalid timing command lacked rejection'}
  if($s.timing.context.timing -ne '0'){throw 'Invalid timing page command changed state'}
  Note-Timing 'timing-invalid-page-command-rejected' $s
  Send-AcqCommands @('measurements 0','acqrun');$null=Wait-Highspeed50MSingle 6
  Send-AcqCommands @('timing 1');Export-Timing 'timing-cic6m25';$live=Read-TimingSnapshot
  Send-AcqCommands @('measurements 0','memload','timing 1');$replay=Read-TimingSnapshot
  if($replay.memory.status.replay -ne '1' -or $replay.timing.context.rate -ne '50000000' -or $replay.ui.display_id -ne $saved.ui.display_id -or $live.timing.context.rate -ne '6250000'){throw 'Timing replay did not retain original50M metadata'}
  for($ch=0;$ch -lt 2;$ch++){foreach($key in @('pulse_valid','rise_valid','fall_valid','cycles','duty_percent','positive_us','negative_us','period_us','rise_us','fall_us','low_v','mid_v','high_v')){if($replay.timing.channels[$ch][$key] -ne $saved.timing.channels[$ch][$key]){throw "Replay timing cache differs:$key"}}}
  Export-Timing 'memory-timing-replay'
  Send-AcqCommands @('measurements 0');$returnAck=Invoke-MemoryQueuedStop 'memlive';$returned=Read-TimingSnapshot
  Assert-MemoryEpoch $replay $returned 2
  Assert-MemoryFields $live.memory.view $returned.memory.view @('record_address','record_crc','rate','conversion','filter','display_id','display_epoch') 'Timing return original live record'
  for($ch=0;$ch -lt 2;$ch++){foreach($key in @('pulse_valid','rise_valid','fall_valid','duty_percent','positive_us','negative_us','period_us','rise_us','fall_us')){if($returned.timing.channels[$ch][$key] -ne $live.timing.channels[$ch][$key]){throw 'Return live timing cache changed'}}}
  Note-Timing 'timing-return-live-cache-restored' $returned
  $state.timing_return=@{before=$live;after=$returned;ack=$returnAck}
  Send-AcqCommands @('acqrun');[void](Set-LongTimebase 10);$null=Wait-Highspeed50MSingle 10
  Send-AcqCommands @('timing 1');Export-Timing 'timing-cic625k'
  Send-AcqCommands @('measurements 0','source 3');$rejectStart=$captured.Length
  Send-AcqCommands @('timing 1');$s=Read-AcqState
  if($captured.ToString().Substring($rejectStart) -notmatch '(?m)^TIMING_COMMAND REJECTED\r?$'){throw 'DEMO timing command lacked rejection'}
  if($s.ui.source -ne 'SIM'){throw 'Timing demo rejection setup wrong'}
  Note-Acq 'timing-DEMO-page-rejected' $s
  Send-AcqCommands @('source 2','timing 0','measurements 0','triggermode auto','acqrun');[void](Set-LongTimebase 6)
  $s=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.adc.display_rate -eq '6250000' -and $x.adc.display_epoch -eq $x.ui.epoch}
  Note-Acq 'timing-final-CH2-auto-running' $s
 }finally{$state.long_records=$prior}
}

