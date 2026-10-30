# Requires the authorized board runner's owned serial/server and shared helpers.
function Read-AutosetState {
 [void](Invoke-AcqCommand '__query_boundary__' '(?m)^COMMANDS:[^\r\n]+\r?\n')
 $begin=$captured.Length
 [void](Invoke-AcqCommand 'autostatus' '(?m)^AUTOSET_END epoch=\d+\r?\n')
 $body=$captured.ToString().Substring($begin)
 $head=[regex]::Matches($body,'(?m)^AUTOSET_STATUS [^\r\n]+\r?\n')
 $ch=[regex]::Matches($body,'(?m)^AUTOSET_CHANNEL [^\r\n]+\r?\n')
 $end=[regex]::Matches($body,'(?m)^AUTOSET_END [^\r\n]+\r?\n')
 if($head.Count -ne 1 -or $ch.Count -ne 2 -or $end.Count -ne 1){throw 'Incomplete autoset status frame'}
 $context=Convert-AcqFields $head[0].Value;$tail=Convert-AcqFields $end[0].Value
 if($context.epoch -ne $tail.epoch){throw 'Autoset query mixed epochs'}
 $result=@{context=$context;channels=@()}
 for($c=0;$c -lt 2;$c++){$v=Convert-AcqFields $ch[$c].Value;if([int]$v.ch -ne $c+1){throw 'Autoset channel order mismatch'};$result.channels+=@($v)}
 return $result
}
function Read-AutosetFrozen {
 $s=Read-AcqState;$auto=Read-AutosetState;$m=Read-MeasurementState
 if($s.ui.running -ne '0' -or $m.context.display_id -ne $s.ui.display_id -or $auto.context.display_id -ne $s.ui.display_id -or $auto.context.epoch -ne $s.ui.epoch){throw 'Autoset snapshot is not a coherent frozen record'}
 return @{ui=$s.ui;adc=$s.adc;auto=$auto;measurement=$m}
}
function Note-Autoset([string]$Name,$Snapshot){
 $state.autoset_checks+=@{test=$Name;snapshot=$Snapshot}
 [void](Note-Acq $Name $Snapshot)
}
function Assert-AutosetRestored($Before,$After,[int]$EpochAdvance){
 foreach($key in @('time','scale','ch1_scale','ch2_scale','ch1_pos','ch2_pos','source','trig_channel','trig_normal','trig_rising','level_mv','running','single_armed','single_complete','display_id')){
  if($Before.ui[$key] -ne $After.ui[$key]){throw "Autoset rollback changed$key"}
 }
 foreach($key in @('display_id','display_address','display_crc','display_rate','display_filter','display_conversion_hz','display_epoch')){
  if($Before.adc[$key] -ne $After.adc[$key]){throw "Autoset rollback changed record$key"}
 }
 if(([uint64]$After.ui.epoch+4294967296-[uint64]$Before.ui.epoch)%4294967296 -ne $EpochAdvance){throw 'Autoset rollback epoch increment wrong'}
 for($ch=0;$ch -lt 2;$ch++){
  foreach($key in @('valid','overrange','min_v','max_v','mean_v','vpp_v','rms_total_v','rms_ac_v','frequency_valid','frequency_hz','period_us')){
   if($Before.measurement.channels[$ch][$key] -ne $After.measurement.channels[$ch][$key]){throw "Autoset rollback changed cached$key"}
  }
 }
}
function Wait-AutosetComplete($Before){
 $timer=[Diagnostics.Stopwatch]::StartNew()
 do{
  $a=Read-AutosetState
  if($a.context.state -eq '2' -and $a.context.busy -eq '0' -and $a.context.running -eq '1' -and [uint64]$a.context.display_id -gt [uint64]$Before.ui.display_id){return $a}
  if($a.context.state -notin @('1','2')){throw ('Autoset failed:'+($a|ConvertTo-Json -Compress -Depth 4))}
  if($timer.Elapsed.TotalSeconds -gt 5){throw 'Autoset did not confirm current50kHz record'}
  Start-Sleep -Milliseconds 30
 }while($true)
}
function Export-Autoset([string]$Name,[bool]$Frame=$false){
 $before=Read-AutosetFrozen
 [void](Export-LongRecord $Name $Frame)
 $state.long_records[-1].autoset_snapshot=$before
 return $state.long_records[-1]
}
function Test-AutosetControls {
 $prior=$state.long_records;$state.long_records=@();$state.autoset_checks=@();$state.autoset_cases=@()
 try{
  Send-AcqCommands @('measurements 0','source 2','trigsource 2','trigger rise 500','triggermode auto','cursors 0','acqrun')
  [void](Set-LongTimebase 5);$null=Wait-Highspeed50MSingle 5
  $source=Export-Autoset 'auto-source-manual'
  Send-AcqCommands @('vertical 1 4 1500','vertical 2 4 -1000','trigsource 1','trigger fall 4500','triggermode normal')
  $before=Read-AutosetFrozen;Note-Autoset 'autoset-manual-settings-before' $before
  Send-AcqCommands @('tap 504 32');$complete=Wait-AutosetComplete $before
  if($complete.context.epoch -ne ([uint64]$before.ui.epoch+1)%4294967296 -or $complete.context.time -ne '6' -or $complete.context.trigger_channel -ne '2'){throw 'Autoset did not atomically choose50kHz settings'}
  Send-AcqCommands @('acqstop');$after=Read-AutosetFrozen
  Note-Autoset 'autoset-pointer-complete-from-manual-stop' $after
  $record=Export-Autoset 'auto-result-manual' $true
  $state.autoset_cases+=@{name='manual-stop';source=$source.name;result=$record.name;before=$before;completed=$complete;after=$after}
  # A later explicit adjustment must stay in place as ordinary records arrive.
  Send-AcqCommands @('vertical 2 3 1000','acqrun')
  $first=Read-AcqState;$s=Wait-AcqState {param($x) [uint64]$x.ui.display_id -ge [uint64]$first.ui.display_id+3}
  if($s.ui.ch2_scale -ne '3' -or $s.ui.ch2_pos -ne '1000'){throw 'Autoset continued to override manual settings'}
  Send-AcqCommands @('acqstop');$s=Read-AutosetFrozen;Note-Autoset 'autoset-does-not-track-after-manual-adjustment' $s
  Send-AcqCommands @('trigger rise 500','triggermode auto','trigsource 2','acqrun');[void](Set-LongTimebase 5);$null=Wait-Highspeed50MSingle 5
  $source=Export-Autoset 'auto-source-single';$before=Read-AutosetFrozen
  if($before.ui.single_complete -ne '1'){throw 'Autoset single-origin case was not frozen'}
  Note-Autoset 'autoset-single-before' $before
  Send-AcqCommands @('autoset');$complete=Wait-AutosetComplete $before
  Send-AcqCommands @('acqstop');$after=Read-AutosetFrozen;Note-Autoset 'autoset-complete-resumes-from-single' $after
  # One real frame also verifies the transient result notice restores footer.
  Start-Sleep -Milliseconds 5200
  $record=Export-Autoset 'auto-result-single' $true
  $state.autoset_cases+=@{name='single';source=$source.name;result=$record.name;before=$before;completed=$complete;after=$after}
  Send-AcqCommands @('trigger rise 500','triggermode auto','acqrun');$null=Wait-Highspeed50MSingle 6
  $before=Read-AutosetFrozen;Note-Autoset 'autoset-cancel-before' $before
  $start=$captured.Length
  [void](Invoke-AcqCommand "autoset`nautoset`nautocancel" '(?m)^AUTOSET_CANCEL_DONE\r?\nUI_STATUS [^\r\n]+\r?\n')
  $batch=$captured.ToString().Substring($start)
  if($batch -notmatch 'AUTOSET_COMMAND STARTED' -or $batch -notmatch 'AUTOSET_COMMAND REJECTED'){throw 'Queued autoset start/repeat rejection not observed'}
  $after=Read-AutosetFrozen;Assert-AutosetRestored $before $after 2
  if($after.auto.context.state -ne '8'){throw 'Autoset cancel did not report canceled'}
  Note-Autoset 'autoset-queued-cancel-restores-single-and-record' $after
  $state.autoset_cancel=@{before=$before;after=$after;scope='queued cancel before board poll; not a late in-flight cancellation measurement'}
  Send-AcqCommands @('source 1');$before=Read-AutosetFrozen
  Send-AcqCommands @('autoset');$after=Read-AutosetFrozen;Assert-AutosetRestored $before $after 0
  if($after.auto.context.state -ne '4'){throw 'EmptyCH1 autoset not rejected'}
  Note-Autoset 'autoset-empty-CH1-preserves-settings' $after;$state.autoset_empty=@{before=$before;after=$after}
  Send-AcqCommands @('source 3');$s=Read-AcqState
  Send-AcqCommands @('autoset');$a=Read-AutosetState;$t=Read-AcqState
  if($a.context.state -ne '7' -or $s.ui.epoch -ne $t.ui.epoch -or $s.ui.time -ne $t.ui.time){throw 'DEMO autoset changed configuration'}
  Note-Acq 'autoset-DEMO-rejected' $t
  Send-AcqCommands @('source 2','trigger rise 4500','triggermode normal','single')
  $s=Wait-AcqState {param($x) $x.ui.single_armed -eq '1' -and $x.adc.hw_waiting -eq '1'}
  $epoch=$s.ui.epoch;Send-AcqCommands @('autoset');$a=Read-AutosetState;$t=Read-AcqState
  if($a.context.state -ne '7' -or $t.ui.epoch -ne $epoch -or $t.ui.single_armed -ne '1'){throw 'Autoset disrupted pending single'}
  Note-Acq 'autoset-pending-single-rejected-without-cancel' $t
  Send-AcqCommands @('acqstop','trigger rise 500','triggermode auto','trigsource 2','acqrun')
  [void](Set-LongTimebase 5);$null=Wait-Highspeed50MSingle 5
  $before=Read-AutosetFrozen;Send-AcqCommands @('autoset');$complete=Wait-AutosetComplete $before
  $state.autoset_final=$complete
  $s=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.ui.time -eq '6' -and $x.ui.trig_channel -eq '2' -and $x.ui.triggered -eq '1' -and $x.adc.display_rate -eq '6250000' -and $x.adc.display_epoch -eq $x.ui.epoch}
  Note-Acq 'final-autoset-complete-CH2-10us-auto-running' $s
 }finally{$state.autoset_records=$state.long_records;$state.long_records=$prior}
}
