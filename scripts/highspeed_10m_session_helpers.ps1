# M3-A tests share the authorized runner's serial/server ownership.
function Assert-Raw10M($s){
 Assert-HardwareRecord $s $true
 if($s.adc.applied_rate -ne '10000000' -or $s.adc.display_rate -ne '10000000' -or $s.adc.rate_index -ne '4' -or
    $s.adc.filter_kind -ne '0' -or $s.adc.record_filter -ne '0' -or $s.adc.display_filter -ne '0' -or
    $s.adc.conversion_hz -ne '10000000' -or $s.adc.display_conversion_hz -ne '10000000'){throw 'RAW10M rate/filter/clock contract mismatch'}
}
function Test-Highspeed10MControls {
 $state.long_records=@()
 Send-AcqCommands @('source 2','trigsource 2','trigger rise 500','triggermode normal','dual 1','cursors 0')
 foreach($index in @(4,3,2,1,0)){
  [void](Set-LongTimebase $index);Send-AcqCommands @('single')
  $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0' -and $x.adc.display_rate -eq '10000000' -and $x.adc.display_epoch -eq $x.ui.epoch}
  Assert-Raw10M $s;Note-Acq "raw10M-fast-timebase-$index-single-complete" $s
  if($index -eq 4){Export-LongRecord 'raw10m-20us' $true}
  if($index -eq 0){
   Send-AcqCommands @('cursors 1','cursor 250 260');$s=Read-AcqState
   $dt=[double]$s.ui.cursor_dt_us
   if([double]::IsNaN($dt) -or [double]::IsInfinity($dt) -or [math]::Abs($dt-.1) -gt .000001 -or $s.ui.cursor_valid -ne '1'){throw 'RAW cursor100ns interval mismatch'}
   Note-Acq 'raw10M-cursor100ns-frozen' $s;Export-LongRecord 'raw10m-1us-cursor' $true
  }
 }
 # Changing requested acquisition profile must not relabel the held RAW data.
 $s=Read-AcqState;$held=$s.ui.display_id;$crc=$s.adc.display_crc
 [void](Set-LongTimebase 7);$s=Read-AcqState
 if($s.ui.display_id -ne $held -or $s.adc.display_crc -ne $crc -or $s.adc.display_rate -ne '10000000' -or $s.adc.display_filter -ne '0' -or $s.ui.running -ne '0'){throw 'RAW frozen record relabeled asCIC ontimebase change'}
 Note-Acq 'RAW-held-metadata-preserved-while-requesting-CIC' $s
 Send-AcqCommands @('cursors 0','single');$s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.adc.display_rate -eq '625000' -and $x.adc.display_filter -eq '3'}
 Assert-HardwareRecord $s $true;Note-Acq 'CIC625k-restored-on10MHz-converter' $s
 Export-LongRecord 'cic625k-10mhz' $true
 $held=$s.ui.display_id;$crc=$s.adc.display_crc
 [void](Set-LongTimebase 4);$s=Read-AcqState
 if($s.ui.display_id -ne $held -or $s.adc.display_crc -ne $crc -or $s.adc.display_rate -ne '625000' -or $s.adc.display_filter -ne '3'){throw 'CIC heldrecord relabeled asRAW'}
 Note-Acq 'CIC-held-metadata-preserved-while-requesting-RAW' $s
 Send-AcqCommands @('trigger fall 500','single');$s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.adc.display_rate -eq '10000000'}
 Assert-Raw10M $s;Note-Acq 'RAW-falling-trigger-positive' $s;Export-LongRecord 'raw10m-fall'
 Send-AcqCommands @('trigger rise 500','trigsource 1','single')
 $s=Wait-HardwareWaitingProgress 32768
 if($s.ui.single_armed -ne '1' -or $s.ui.single_complete -ne '0'){throw 'EmptyCH1 falsely triggered RAWsingle'}
 Note-Acq 'RAW-empty-CH1-waits-while-samples-progress' $s
 Send-AcqCommands @('source 1');$s=Read-AcqState;$epoch=$s.ui.epoch
 Send-AcqCommands @('source 2');$s=Read-AcqState
 if($s.ui.epoch -ne $epoch -or $s.ui.single_armed -ne '1'){throw 'RAWmeasurementswitchcanceledsingle'}
 Note-Acq 'RAW-measurement-switch-keeps-waiting-epoch' $s
 $beforeSource=$s
 Send-AcqCommands @('trigsource 2');$s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0'}
 $expectedEpoch=([uint64]$beforeSource.ui.epoch+1)%4294967296
 $recordDelta=([uint64]$s.ui.display_id+4294967296-[uint64]$beforeSource.ui.seen_id)%4294967296
 if([uint64]$s.ui.epoch -ne $expectedEpoch -or $recordDelta -eq 0 -or $recordDelta -ge 2147483648 -or $s.ui.trig_channel -ne '2'){throw 'RAW trigger-source change failed to create a new epoch and record'}
 Assert-Raw10M $s;Note-Acq 'RAW-trigger-source-new-epoch-completes-single' $s
 Send-AcqCommands @('cursors 1','cursor 250 750');$s=Read-AcqState
 $dt=[double]$s.ui.cursor_dt_us;$inverse=[double]$s.ui.cursor_inv_hz
 if([double]::IsNaN($dt) -or [double]::IsInfinity($dt) -or [double]::IsNaN($inverse) -or [double]::IsInfinity($inverse) -or [math]::Abs($dt-100) -gt .000001 -or [math]::Abs($inverse-10000) -gt .000001){throw 'RAW20us cursor100us/10kHz mismatch'}
 Note-Acq 'RAW20us-cursor100us-and10kHz' $s;Export-LongRecord 'raw10m-cursors' $true
 Send-AcqCommands @('cursors 0','triggermode auto','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.ui.trig_normal -eq '0' -and $x.ui.triggered -eq '1' -and $x.adc.display_rate -eq '10000000'}
 Assert-Raw10M $s;Note-Acq 'final-RAW10M-CH2-20us-div-auto-running' $s
}
