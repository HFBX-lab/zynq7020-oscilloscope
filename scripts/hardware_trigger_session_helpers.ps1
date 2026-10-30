# Shares the acquisition runner's explicitly owned serial/JTAG session.
function Assert-HardwareFields($s){
 if($Highspeed10M -or $Highspeed25M -or $Highspeed50M){foreach($key in @('conversion_hz','record_filter','display_filter','display_conversion_hz')){if(!$s.adc.ContainsKey($key)){throw ('Missing highspeed metadata:'+ $key)}};if($s.adc.conversion_hz -ne $(if($Highspeed50M){'50000000'}elseif($Highspeed25M){'25000000'}else{'10000000'})){throw 'Wrong highspeed conversion clock'}}
 foreach($key in @('hw_waiting','hw_progress','hw_matched','hw_auto','hw_after','hw_start','hw_seen','display_rate','display_epoch','capture_epoch')){
  if(!$s.adc.ContainsKey($key)){throw "Missing hardware-trigger status field:$key"}
 }
}
function Wait-HardwareWaitingProgress([uint32]$Samples=12288,[int]$Seconds=6){
 $anchor=Wait-AcqState {param($x) $x.adc.pending -eq '1' -and $x.adc.hw_waiting -eq '1' -and $x.adc.capture_epoch -eq $x.ui.epoch} $Seconds
 Assert-HardwareFields $anchor
 $p=[uint64]$anchor.adc.hw_progress;$raw=$anchor.adc.id;$display=$anchor.ui.display_id;$crc=$anchor.adc.display_crc;$epoch=$anchor.ui.epoch
 $s=Wait-AcqState {param($x) $delta=([uint64]$x.adc.hw_progress+4294967296-$p)%4294967296; $x.adc.hw_waiting -eq '1' -and $x.adc.capture_epoch -eq $epoch -and $delta -ge $Samples -and $delta -lt 2147483648} $Seconds
 if($s.adc.id -ne $raw -or $s.ui.display_id -ne $display -or $s.adc.display_crc -ne $crc -or $s.ui.epoch -ne $epoch){throw 'Waiting hardware ring published or replaced a frozen record'}
 return $s
}
function Assert-HardwareRecord($s,[bool]$Matched){
 Assert-HardwareFields $s
 if([int]$s.ui.time -lt 0 -or [int]$s.ui.time -gt $(if($Highspeed50M){20}elseif($Highspeed10M -or $Highspeed25M){17}else{12})){throw 'Invalid hardware timebase'}
 Assert-LongProfile $s ([int]$s.ui.time) $Matched
 if($Matched -and $s.ui.triggered -ne '1'){throw 'Hardware matched record missing UI trigger flag'}
 if($s.adc.hw_matched -ne $(if($Matched){'1'}else{'0'}) -or $s.adc.hw_auto -ne $(if($Matched){'0'}else{'1'}) -or [uint32]$s.adc.hw_seen -lt 4096 -or [uint32]$s.adc.hw_start -ge 4096){throw 'Invalid frozen hardware trigger metadata'}
 $seconds=Get-AcqTimebaseSeconds ([int]$s.ui.time)
 $span=10*$seconds*[int]$s.adc.display_rate
 $start=[double]$s.ui.view_start;$cross=[double]$s.ui.crossing
 if([double]::IsNaN($start) -or [double]::IsInfinity($start) -or [double]::IsNaN($cross) -or [double]::IsInfinity($cross) -or $start -lt 0 -or $start+$span -gt 4095.004){throw 'Invalid hardware display window'}
 if($Matched){
  $after=[int][math]::Ceiling(.2*$span)+1
  if([int]$s.adc.hw_after -ne $after -or $cross -lt $after-1 -or $cross -gt $after -or [math]::Abs($cross-$start-.2*$span) -gt .004){throw 'Hardware designated edge not used for20percent alignment'}
 }elseif($s.ui.triggered -ne '0' -or $s.adc.hw_after -ne '0') {throw 'AUTO fallback falsely marked triggered'}
}
function Test-HardwareTriggerControls {
 $state.long_records=@()
 Send-AcqCommands @('source 2','trigsource 2','trigger rise 4500','triggermode normal','acqrun')
 $s=Wait-HardwareWaitingProgress 32768
 Note-Acq 'hardware-NORMAL-eight-rings-no-publish-or-false-timeout' $s
 Send-AcqCommands @('single');$s=Wait-HardwareWaitingProgress 32768
 if($s.ui.single_armed -ne '1' -or $s.ui.single_complete -ne '0'){throw 'Hardware no-edge single changed state'}
 Note-Acq 'hardware-SINGLE-eight-rings-no-false-completion' $s
 Send-AcqCommands @('acqstop','triggermode auto','acqrun')
 $s=Wait-AcqState {param($x) $x.adc.hw_auto -eq '1' -and $x.ui.triggered -eq '0' -and $x.adc.display_epoch -eq $x.ui.epoch}
 Assert-HardwareRecord $s $false;Note-Acq 'hardware-AUTO-complete-untriggered-fallback' $s
 Send-AcqCommands @('acqstop');Export-LongRecord 'hw-auto-fallback' $true
 Send-AcqCommands @('trigger rise 500','triggermode normal','single')
 $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0'}
 Assert-HardwareRecord $s $true;Note-Acq 'hardware-rising-positive-single' $s
 Export-LongRecord 'hw-rise' $true
 Send-AcqCommands @('trigger fall 500','single')
 $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0'}
 Assert-HardwareRecord $s $true;Note-Acq 'hardware-falling-positive-single' $s
 Export-LongRecord 'hw-fall'
 # Many earlier periodic edges exist inthisrawrecord; UI mustuse theone
 # selected byhardware at after626, not thefirst valid software-search edge.
 [void](Set-LongTimebase 6);Send-AcqCommands @('trigger rise 500','single')
 $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.adc.display_rate -eq '62500'}
 Assert-HardwareRecord $s $true;Note-Acq 'hardware-wide-window-uses-designated-edge-after626' $s
 Export-LongRecord 'hw-wide-designated' $true
 $id=$s.ui.display_id;$crc=$s.adc.display_crc;$epoch=$s.ui.epoch
 Send-AcqCommands @('source 1','source 2','cursors 1','cursor 250 750');$s=Read-AcqState
 if($s.ui.display_id -ne $id -or $s.adc.display_crc -ne $crc -or $s.ui.epoch -ne $epoch -or $s.ui.single_complete -ne '1'){throw 'Hardware record changed onmeasurement/cursor controls'}
 Note-Acq 'hardware-frozen-record-measurement-and-cursor-protection' $s
 [void](Set-LongTimebase 2);Send-AcqCommands @('cursors 0','triggermode auto','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.ui.triggered -eq '1' -and $x.adc.display_rate -eq '625000'}
 Note-Acq 'final-hardware-CH2-200us-div-auto-running' $s
}
