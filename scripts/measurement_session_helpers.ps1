# Run only inside the authorized acquisition runner's owned UART/JTAG session.
function Read-MeasurementState {
 [void](Invoke-AcqCommand '__query_boundary__' '(?m)^COMMANDS:[^\r\n]+\r?\n')
 $begin=$captured.Length
 [void](Invoke-AcqCommand 'measurestatus' '(?m)^MEASURE_END display_id=\d+\r?\n')
 $body=$captured.ToString().Substring($begin)
 $headers=[regex]::Matches($body,'(?m)^MEASURE_STATUS [^\r\n]+\r?\n')
 $channels=[regex]::Matches($body,'(?m)^MEASURE_CHANNEL [^\r\n]+\r?\n')
 $ends=[regex]::Matches($body,'(?m)^MEASURE_END [^\r\n]+\r?\n')
 if($headers.Count -ne 1 -or $channels.Count -ne 2 -or $ends.Count -ne 1){throw 'Incomplete atomic measurement reply'}
 $h=Convert-AcqFields $headers[0].Value;$e=Convert-AcqFields $ends[0].Value
 if($h.display_id -ne $e.display_id){throw 'Measurement reply mixed records'}
 $out=@{context=$h;channels=@()}
 for($ch=0;$ch -lt 2;$ch++){
  $values=Convert-AcqFields $channels[$ch].Value
  if([int]$values.ch -ne $ch+1 -or $values.valid -ne '1'){throw 'Measurement channel unavailable'}
  foreach($key in @('min_v','max_v','mean_v','vpp_v','rms_total_v','rms_ac_v','frequency_hz','period_us')){
   if(!$values.ContainsKey($key)){throw "Missing measurement$key"}
   $v=[double]$values[$key];if([double]::IsNaN($v) -or [double]::IsInfinity($v)){throw 'Nonfinite measurement'}
  }
  $out.channels+=@($values)
 }
 return $out
}
function Note-Measurement([string]$Name){
 $s=Read-AcqState;$m=Read-MeasurementState
 if($s.ui.running -ne '0' -or $m.context.display_id -ne $s.ui.display_id -or $m.context.rate -ne $s.adc.display_rate -or $m.context.count -ne '4096' -or $m.context.epoch -ne $s.ui.epoch){throw 'Measurement did not identify frozen display'}
 $state.measurement_checks+=@{test=$Name;ui=$s.ui;adc=$s.adc;measurement=$m}
 [void](Note-Acq $Name $s)
 return $m
}
function Export-Measurement([string]$Name){
 $m=Read-MeasurementState
 Export-LongRecord $Name $true
 $entry=$state.long_records[-1]
 if($entry.id -ne $m.context.display_id -or [int]$entry.rate -ne [int]$m.context.rate -or $m.context.panel -ne '1'){throw 'Measurement export lost record/panel binding'}
 $entry.measurement=$m
}
function Assert-MeasurementHeld($Before,$After){
 Assert-Highspeed50MHeld $Before $After
 if($Before.ui.epoch -ne $After.ui.epoch -or $Before.ui.trig_channel -ne $After.ui.trig_channel -or $Before.ui.single_complete -ne $After.ui.single_complete){throw 'Measurement dialog changed acquisition ownership'}
}
function Test-MeasurementControls {
 $state.measurement_checks=@();$prior=$state.long_records;$state.long_records=@()
 try{
  Send-AcqCommands @('measurements 0','source 2','trigsource 2','trigger rise 500','triggermode auto','cursors 0','dual 1','acqrun')
  [void](Set-LongTimebase 5);$s=Wait-Highspeed50MSingle 5;$held=$s
  Send-AcqCommands @('tap 620 32')
  $m=Note-Measurement 'measurement-RAW50M-pointer-open-frozen'
  if($m.context.panel -ne '1' -or $m.context.channel -ne '2' -or $m.channels[1].frequency_valid -ne '1' -or [math]::Abs([double]$m.channels[1].period_us-20) -gt 1){throw 'Raw50M measurement frequency/period wrong'}
  Export-Measurement 'measure-raw-ch2'
  Send-AcqCommands @('tap 238 162')
  $m=Note-Measurement 'measurement-pointer-CH1-keeps-independent-trigger'
  $s=Read-AcqState;Assert-MeasurementHeld $held $s
  if($m.context.channel -ne '1' -or $m.channels[0].frequency_valid -ne '0' -or [double]$m.channels[0].period_us -ne 0){throw 'EmptyCH1 invented a period'}
  Export-Measurement 'measure-raw-ch1'
  Send-AcqCommands @('tap 402 162')
  $m=Note-Measurement 'measurement-pointer-CH2-restores-same-record'
  $s=Read-AcqState;Assert-MeasurementHeld $held $s
  Send-AcqCommands @('vertical 2 3 1000')
  $m=Note-Measurement 'measurement-independent-of-vertical-scale-position'
  Send-AcqCommands @('vertical 2 2 0','tap 811 112')
  $m=Note-Measurement 'measurement-pointer-close-keeps-frozen'
  if($m.context.panel -ne '0'){throw 'Measurement close pointer failed'}
  Send-AcqCommands @('measurements 2');$m=Note-Measurement 'measurement-invalid-panel-command-rejected'
  if($m.context.panel -ne '0'){throw 'Invalid measurement panel command changed state'}
  [void](Set-LongTimebase 6);Send-AcqCommands @('measurements 1')
  $m=Note-Measurement 'measurement-frozen-RAW-keeps-own-rate-while-requesting-CIC'
  $s=Read-AcqState;Assert-Highspeed50MHeld $held $s
  if($m.context.rate -ne '50000000'){throw 'Frozen50M record was relabeled6.25M'}
  Send-AcqCommands @('measurements 0','acqrun');$null=Wait-Highspeed50MSingle 6
  Send-AcqCommands @('measurements 1');$m=Note-Measurement 'measurement-new-CIC6m25-record'
  Export-Measurement 'measure-cic6m25'
  Send-AcqCommands @('measurements 0','acqrun');[void](Set-LongTimebase 10);$null=Wait-Highspeed50MSingle 10
  Send-AcqCommands @('measurements 1');$m=Note-Measurement 'measurement-new-CIC625k-record'
  Export-Measurement 'measure-cic625k'
  Send-AcqCommands @('source 3');$s=Read-AcqState
  $reply=Invoke-AcqCommand 'measurements 1' '(?m)^MEASUREMENTS_REJECTED\r?\n'
  if(!$reply){throw 'DEMO allowed measurement dialog'}
  Note-Acq 'measurement-DEMO-panel-rejected' $s
  Send-AcqCommands @('source 2','measurements 0','acqrun');[void](Set-LongTimebase 5)
  $s=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.ui.triggered -eq '1' -and $x.adc.display_rate -eq '50000000' -and $x.adc.display_epoch -eq $x.ui.epoch}
  Note-Acq 'final-measurement-CH2-RAW50M-auto-running' $s
 }finally{$state.measurement_records=$state.long_records;$state.long_records=$prior}
}
