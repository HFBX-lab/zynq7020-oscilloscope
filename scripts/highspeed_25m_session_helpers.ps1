# 25M controls share the parent runner serial and server connections.
# Required input: CH2/IN2 sine50kHz,1Vpp,+.5VDC,common ground.
function Assert-Highspeed25MRecord($s,[int]$Index,[bool]$Frozen=$true){
 Assert-HardwareRecord $s $true
 $rate=if($Index -lt 4){25000000}elseif($Index -lt 6){6250000}elseif($Index -lt 9){625000}elseif($Index -lt 12){62500}elseif($Index -lt 15){6250}else{625}
 $code=if($Index -lt 4){4}elseif($Index -lt 6){5}elseif($Index -lt 9){0}elseif($Index -lt 12){1}elseif($Index -lt 15){2}else{3}
 $kind=if($code -eq 4){0}else{3}
 foreach($key in @('applied_rate','display_rate','rate_index','filter_kind','record_filter','display_filter','conversion_hz','display_conversion_hz','id','display_id','pending')){
  if(!$s.adc.ContainsKey($key)){throw ('Missing25M record field:'+ $key)}
 }
 if($Index -lt 0 -or $Index -gt 17 -or [int]$s.ui.time -ne $Index -or [int]$s.adc.applied_rate -ne $rate -or [int]$s.adc.display_rate -ne $rate -or [int]$s.adc.rate_index -ne $code -or
    [int]$s.adc.filter_kind -ne $kind -or [int]$s.adc.record_filter -ne $kind -or [int]$s.adc.display_filter -ne $kind -or
    $s.adc.conversion_hz -ne '25000000' -or $s.adc.display_conversion_hz -ne '25000000' -or $s.adc.capture_epoch -ne $s.ui.epoch){throw '25M rate/filter/conversion/epoch contract mismatch'}
 if($Frozen -and ($s.ui.running -ne '0' -or $s.ui.single_complete -ne '1' -or $s.adc.pending -ne '0' -or $s.adc.id -ne $s.ui.display_id -or $s.adc.display_id -ne $s.ui.display_id)){throw '25M single did not freeze the identified raw record'}
}
function Wait-Highspeed25MSingle([int]$Index){
 $before=Read-AcqState
 $oldSeen=[uint64]$before.ui.seen_id;$expectedEpoch=([uint64]$before.ui.epoch+1)%4294967296
 Send-AcqCommands @('single')
 $s=Wait-AcqState {param($x)
  $delta=([uint64]$x.ui.display_id+4294967296-$oldSeen)%4294967296
  $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0' -and [uint64]$x.ui.epoch -eq $expectedEpoch -and $x.adc.display_epoch -eq $x.ui.epoch -and $delta -gt 0 -and $delta -lt 2147483648
 }
 Assert-Highspeed25MRecord $s $Index
 return $s
}
function Assert-Highspeed25MHeld($Before,$After){
 foreach($key in @('display_id','display_address','display_crc','display_rate','display_epoch','display_filter','display_conversion_hz')){
  if(!$Before.adc.ContainsKey($key) -or !$After.adc.ContainsKey($key) -or $Before.adc[$key] -ne $After.adc[$key]){throw ('25M frozen record changed:'+ $key)}
 }
 if($After.ui.running -ne '0' -or $Before.ui.display_id -ne $After.ui.display_id){throw '25M held record resumed or changed ID'}
}
function Assert-Highspeed25MCursor($s,[double]$ExpectedUs){
 foreach($key in @('cursor_dt_us','cursor_inv_hz','cursor_dv','cursor_valid','cursors')){if(!$s.ui.ContainsKey($key)){throw ('Missing25M cursor field:'+ $key)}}
 $delta=[double]$s.ui.cursor_dt_us;$inverse=[double]$s.ui.cursor_inv_hz;$voltage=[double]$s.ui.cursor_dv
 if([double]::IsNaN($delta) -or [double]::IsInfinity($delta) -or [double]::IsNaN($inverse) -or [double]::IsInfinity($inverse) -or [double]::IsNaN($voltage) -or [double]::IsInfinity($voltage) -or
    [math]::Abs($delta-$ExpectedUs) -gt .000001 -or [math]::Abs($inverse-1000000/[math]::Abs($ExpectedUs)) -gt .00001 -or $s.ui.cursor_valid -ne '1' -or $s.ui.cursors -ne '1'){throw '25M cursor interval/inverse/valid mismatch'}
}
function Test-Highspeed25MControls {
 $state.long_records=@()
 Send-AcqCommands @('acqstop','source 2','trigsource 2','trigger rise 500','triggermode normal','dual 1','cursors 0')
 foreach($index in @(3,2,1,0)){
  [void](Set-LongTimebase $index);$s=Wait-Highspeed25MSingle $index
  Note-Acq "raw25M-fast-timebase-$index-single-complete" $s
  if($index -eq 3){Export-LongRecord 'raw25m-10us' $true}
  if($index -eq 0){
   $held=$s;Send-AcqCommands @('cursors 1','cursor 250 260');$s=Read-AcqState
   Assert-Highspeed25MHeld $held $s;Assert-Highspeed25MCursor $s .1
   Note-Acq 'raw25M-cursor100ns-frozen' $s;Export-LongRecord 'raw25m-1us-cursor' $true
  }
 }
 $held=Read-AcqState;[void](Set-LongTimebase 4);$s=Read-AcqState
 Assert-Highspeed25MHeld $held $s;Note-Acq 'RAW25M-held-while-requesting-CIC6m25' $s
 Send-AcqCommands @('cursors 0')
 foreach($index in @(4,5)){
  [void](Set-LongTimebase $index);$s=Wait-Highspeed25MSingle $index
  Note-Acq "CIC6m25-timebase-$index-single-complete" $s
  if($index -eq 4){Export-LongRecord 'cic6m25-20us' $true}else{Export-LongRecord 'cic6m25-50us'}
 }
 $held=Read-AcqState;[void](Set-LongTimebase 7);$s=Read-AcqState
 Assert-Highspeed25MHeld $held $s;Note-Acq 'CIC6m25-held-while-requesting-CIC625k' $s
 $s=Wait-Highspeed25MSingle 7;Note-Acq 'CIC625k-restored-on25MHz-converter' $s
 Export-LongRecord 'cic625k-25mhz' $true
 $held=Read-AcqState;[void](Set-LongTimebase 3);$s=Read-AcqState
 Assert-Highspeed25MHeld $held $s;Note-Acq 'CIC625k-held-while-requesting-RAW25M' $s
 Send-AcqCommands @('trigger fall 500');$s=Wait-Highspeed25MSingle 3
 Note-Acq 'RAW25M-falling-trigger-positive' $s;Export-LongRecord 'raw25m-fall'
 $held=$s
 Send-AcqCommands @('trigger rise 500','trigsource 1','single')
 $s=Wait-HardwareWaitingProgress 32768
 if($s.ui.single_armed -ne '1' -or $s.ui.single_complete -ne '0' -or $s.ui.running -ne '1' -or $s.ui.trig_channel -ne '1' -or $s.adc.id -ne $held.adc.id -or $s.ui.display_id -ne $held.ui.display_id -or $s.adc.display_crc -ne $held.adc.display_crc){throw 'EmptyCH1 published or changed a waiting RAW25M single'}
 Note-Acq 'RAW25M-empty-CH1-waits-while-samples-progress' $s
 $waiting=$s;Send-AcqCommands @('source 1','source 2');$s=Read-AcqState
 if($s.ui.epoch -ne $waiting.ui.epoch -or $s.ui.single_armed -ne '1' -or $s.ui.trig_channel -ne '1' -or $s.adc.id -ne $waiting.adc.id -or $s.ui.display_id -ne $waiting.ui.display_id -or $s.adc.display_crc -ne $waiting.adc.display_crc){throw 'RAW25M measurement switch changed pending single/record'}
 Note-Acq 'RAW25M-measurement-switch-keeps-waiting-epoch' $s
 $beforeSource=$s
 Send-AcqCommands @('trigsource 2')
 $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0' -and $x.adc.display_epoch -eq $x.ui.epoch}
 $expectedEpoch=([uint64]$beforeSource.ui.epoch+1)%4294967296
 $recordDelta=([uint64]$s.ui.display_id+4294967296-[uint64]$beforeSource.ui.seen_id)%4294967296
 if([uint64]$s.ui.epoch -ne $expectedEpoch -or $recordDelta -eq 0 -or $recordDelta -ge 2147483648 -or $s.ui.trig_channel -ne '2'){throw 'RAW25M trigger-source change failed new epoch/new record'}
 Assert-Highspeed25MRecord $s 3;Note-Acq 'RAW25M-trigger-source-new-epoch-completes-single' $s
 $held=$s;Send-AcqCommands @('cursors 1','cursor 250 750');$s=Read-AcqState
 Assert-Highspeed25MHeld $held $s;Assert-Highspeed25MCursor $s 50
 Note-Acq 'RAW25M-10us-cursor50us-and20kHz' $s;Export-LongRecord 'raw25m-cursors' $true
 Send-AcqCommands @('cursors 0','triggermode auto','dual 1','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC2' -and $x.ui.trig_channel -eq '2' -and $x.ui.running -eq '1' -and $x.ui.trig_normal -eq '0' -and $x.ui.triggered -eq '1' -and $x.adc.display_rate -eq '25000000' -and $x.adc.display_epoch -eq $x.ui.epoch}
 Assert-Highspeed25MRecord $s 3 $false;Note-Acq 'final-RAW25M-CH2-10us-div-auto-running' $s
}
