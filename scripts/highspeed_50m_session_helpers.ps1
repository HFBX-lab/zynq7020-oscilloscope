# 50M controls share the parent runner serial and server connections.
# Required input: CH2/IN2 sine50kHz,1Vpp,+.5VDC,common ground.
function Assert-Highspeed50MRecord($s,[int]$Index,[bool]$Frozen=$true){
 Assert-HardwareRecord $s $true
 $rate=if($Index -lt 6){50000000}elseif($Index -lt 9){6250000}elseif($Index -lt 12){625000}elseif($Index -lt 15){62500}elseif($Index -lt 18){6250}else{625}
 $code=if($Index -lt 6){4}elseif($Index -lt 9){5}elseif($Index -lt 12){0}elseif($Index -lt 15){1}elseif($Index -lt 18){2}else{3}
 $kind=if($code -eq 4){0}else{3}
 foreach($key in @('applied_rate','display_rate','rate_index','filter_kind','record_filter','display_filter','conversion_hz','display_conversion_hz','id','display_id','pending')){
  if(!$s.adc.ContainsKey($key)){throw ('Missing50M record field:'+ $key)}
 }
 if($Index -lt 0 -or $Index -gt 20 -or [int]$s.ui.time -ne $Index -or [int]$s.adc.applied_rate -ne $rate -or [int]$s.adc.display_rate -ne $rate -or [int]$s.adc.rate_index -ne $code -or
    [int]$s.adc.filter_kind -ne $kind -or [int]$s.adc.record_filter -ne $kind -or [int]$s.adc.display_filter -ne $kind -or
    $s.adc.conversion_hz -ne '50000000' -or $s.adc.display_conversion_hz -ne '50000000' -or $s.adc.capture_epoch -ne $s.ui.epoch){throw '50M rate/filter/conversion/epoch contract mismatch'}
 if($Frozen -and ($s.ui.running -ne '0' -or $s.ui.single_complete -ne '1' -or $s.adc.pending -ne '0' -or $s.adc.id -ne $s.ui.display_id -or $s.adc.display_id -ne $s.ui.display_id)){throw '50M single did not freeze the identified raw record'}
}
function Wait-Highspeed50MSingle([int]$Index){
 $before=Read-AcqState
 $oldSeen=[uint64]$before.ui.seen_id;$expectedEpoch=([uint64]$before.ui.epoch+1)%4294967296
 Send-AcqCommands @('single')
 $s=Wait-AcqState {param($x)
  $delta=([uint64]$x.ui.display_id+4294967296-$oldSeen)%4294967296
  $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0' -and [uint64]$x.ui.epoch -eq $expectedEpoch -and $x.adc.display_epoch -eq $x.ui.epoch -and $delta -gt 0 -and $delta -lt 2147483648
 }
 Assert-Highspeed50MRecord $s $Index
 return $s
}
function Assert-Highspeed50MHeld($Before,$After){
 foreach($key in @('display_id','display_address','display_crc','display_rate','display_epoch','display_filter','display_conversion_hz')){
  if(!$Before.adc.ContainsKey($key) -or !$After.adc.ContainsKey($key) -or $Before.adc[$key] -ne $After.adc[$key]){throw ('50M frozen record changed:'+ $key)}
 }
 if($After.ui.running -ne '0' -or $Before.ui.display_id -ne $After.ui.display_id){throw '50M held record resumed or changed ID'}
}
function Assert-Highspeed50MCursor($s,[double]$ExpectedUs){
 foreach($key in @('cursor_dt_us','cursor_inv_hz','cursor_dv','cursor_valid','cursors')){if(!$s.ui.ContainsKey($key)){throw ('Missing50M cursor field:'+ $key)}}
 $delta=[double]$s.ui.cursor_dt_us;$inverse=[double]$s.ui.cursor_inv_hz;$voltage=[double]$s.ui.cursor_dv
 if([double]::IsNaN($delta) -or [double]::IsInfinity($delta) -or [double]::IsNaN($inverse) -or [double]::IsInfinity($inverse) -or [double]::IsNaN($voltage) -or [double]::IsInfinity($voltage) -or
    [math]::Abs($delta-$ExpectedUs) -gt .000001 -or [math]::Abs($inverse-1000000/[math]::Abs($ExpectedUs)) -gt .00001 -or $s.ui.cursor_valid -ne '1' -or $s.ui.cursors -ne '1'){throw '50M cursor interval/inverse/valid mismatch'}
}
function Wait-Highspeed50MWaitingProgress([uint32]$Samples){
 $anchor=Wait-AcqState {param($x) $x.adc.pending -eq '1' -and $x.adc.hw_waiting -eq '1' -and $x.adc.capture_epoch -eq $x.ui.epoch}
 Assert-HardwareFields $anchor
 $progress=[uint64]$anchor.adc.hw_progress;$epoch=$anchor.ui.epoch
 $s=Wait-AcqState {param($x)
  $delta=([uint64]$x.adc.hw_progress+4294967296-$progress)%4294967296
  $x.adc.pending -eq '1' -and $x.adc.hw_waiting -eq '1' -and $x.adc.capture_epoch -eq $epoch -and $delta -ge $Samples -and $delta -lt 2147483648
 }
 foreach($key in @('id','display_id','display_crc','display_rate','display_filter','display_conversion_hz','display_epoch','display_address')){
  if($anchor.adc[$key] -ne $s.adc[$key]){throw ('Waiting RAW50M record changed:'+ $key)}
 }
 if($anchor.ui.epoch -ne $s.ui.epoch -or $anchor.ui.display_id -ne $s.ui.display_id){throw 'Waiting RAW50M epoch/ID changed'}
 $state.highspeed50m_wait_progress=@{anchor=$anchor;after=$s;minimum_samples=$Samples}
 return $s
}
function Test-Highspeed50MControls {
 $state.long_records=@()
 Send-AcqCommands @('acqstop','source 2','trigsource 2','trigger rise 500','triggermode normal','dual 1','cursors 0')
 foreach($index in @(5,4,3,2,1,0)){
  [void](Set-LongTimebase $index);$s=Wait-Highspeed50MSingle $index
  Note-Acq "raw50M-fast-timebase-$index-single-complete" $s
  if($index -eq 5){Export-LongRecord 'raw50m-5us' $true}
  if($index -eq 0){
   $held=$s;Send-AcqCommands @('cursors 1','cursor 250 270');$s=Read-AcqState
   Assert-Highspeed50MHeld $held $s;Assert-Highspeed50MCursor $s .020
   Note-Acq 'raw50M-cursor20ns-frozen' $s;Export-LongRecord 'raw50m-100ns-cursor' $true
  }
 }
 $held=Read-AcqState;[void](Set-LongTimebase 6);$s=Read-AcqState
 Assert-Highspeed50MHeld $held $s;Note-Acq 'RAW50M-held-while-requesting-CIC6m25' $s
 Send-AcqCommands @('cursors 0')
 foreach($index in @(6,8)){
  [void](Set-LongTimebase $index);$s=Wait-Highspeed50MSingle $index
  Note-Acq "CIC6m25-timebase-$index-single-complete" $s
  if($index -eq 6){Export-LongRecord 'cic6m25-10us' $true}else{Export-LongRecord 'cic6m25-50us'}
 }
 $held=Read-AcqState;[void](Set-LongTimebase 10);$s=Read-AcqState
 Assert-Highspeed50MHeld $held $s;Note-Acq 'CIC6m25-held-while-requesting-CIC625k' $s
 $s=Wait-Highspeed50MSingle 10;Note-Acq 'CIC625k-restored-on50MHz-converter' $s
 Export-LongRecord 'cic625k-50mhz' $true
 $held=Read-AcqState;[void](Set-LongTimebase 5);$s=Read-AcqState
 Assert-Highspeed50MHeld $held $s;Note-Acq 'CIC625k-held-while-requesting-RAW50M' $s
 Send-AcqCommands @('trigger fall 500');$s=Wait-Highspeed50MSingle 5
 Note-Acq 'RAW50M-falling-trigger-positive' $s;Export-LongRecord 'raw50m-fall'
 $held=$s
 Send-AcqCommands @('trigger rise 500','trigsource 1','single')
 $s=Wait-Highspeed50MWaitingProgress 32768
 if($s.ui.single_armed -ne '1' -or $s.ui.single_complete -ne '0' -or $s.ui.running -ne '1' -or $s.ui.trig_channel -ne '1' -or $s.adc.id -ne $held.adc.id -or $s.ui.display_id -ne $held.ui.display_id -or $s.adc.display_crc -ne $held.adc.display_crc){throw 'EmptyCH1 published or changed a waiting RAW50M single'}
 Note-Acq 'RAW50M-empty-CH1-waits-while-samples-progress' $s
 $waiting=$s;Send-AcqCommands @('source 1','source 2');$s=Read-AcqState
 if($s.ui.epoch -ne $waiting.ui.epoch -or $s.ui.single_armed -ne '1' -or $s.ui.trig_channel -ne '1' -or $s.adc.id -ne $waiting.adc.id -or $s.ui.display_id -ne $waiting.ui.display_id -or $s.adc.display_crc -ne $waiting.adc.display_crc){throw 'RAW50M measurement switch changed pending single/record'}
 Note-Acq 'RAW50M-measurement-switch-keeps-waiting-epoch' $s
 $beforeSource=$s
 Send-AcqCommands @('trigsource 2')
 $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0' -and $x.adc.display_epoch -eq $x.ui.epoch}
 $expectedEpoch=([uint64]$beforeSource.ui.epoch+1)%4294967296
 $recordDelta=([uint64]$s.ui.display_id+4294967296-[uint64]$beforeSource.ui.seen_id)%4294967296
 if([uint64]$s.ui.epoch -ne $expectedEpoch -or $recordDelta -eq 0 -or $recordDelta -ge 2147483648 -or $s.ui.trig_channel -ne '2'){throw 'RAW50M trigger-source change failed new epoch/new record'}
 Assert-Highspeed50MRecord $s 5;Note-Acq 'RAW50M-trigger-source-new-epoch-completes-single' $s
 $held=$s;Send-AcqCommands @('cursors 1','cursor 250 650');$s=Read-AcqState
 Assert-Highspeed50MHeld $held $s;Assert-Highspeed50MCursor $s 20
 Note-Acq 'RAW50M-5us-cursor20us-and50kHz' $s;Export-LongRecord 'raw50m-cursors' $true
 Send-AcqCommands @('cursors 0','triggermode auto','dual 1','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC2' -and $x.ui.trig_channel -eq '2' -and $x.ui.running -eq '1' -and $x.ui.trig_normal -eq '0' -and $x.ui.triggered -eq '1' -and $x.adc.display_rate -eq '50000000' -and $x.adc.display_epoch -eq $x.ui.epoch}
 Assert-Highspeed50MRecord $s 5 $false;Note-Acq 'final-RAW50M-CH2-5us-div-auto-running' $s
}
