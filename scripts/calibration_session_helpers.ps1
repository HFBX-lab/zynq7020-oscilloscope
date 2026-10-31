# Offline parsers and hardware helpers sharing the parent runner connections.
function Convert-CalibrationStatusFrame([string]$Body){
 $lines=[regex]::Matches($Body,'(?m)^CALIBRATION_[^\r\n]+\r?\n')
 if($lines.Count -ne 5){throw 'Incomplete calibration transaction'}
 $expected=@('CALIBRATION_STATUS','CALIBRATION_COEFFICIENTS','CALIBRATION_COEFFICIENTS','CALIBRATION_COEFFICIENTS','CALIBRATION_END')
 for($i=0;$i -lt 5;$i++){if(!$lines[$i].Value.StartsWith($expected[$i]+' ')){throw 'Unordered calibration transaction'}}
 $head=Convert-AcqFields $lines[0].Value;$end=Convert-AcqFields $lines[4].Value
 foreach($key in @('display_id','epoch','instrument_flags','instrument_id','record_flags','record_id','spectrum_valid','spectrum_flags','spectrum_id')){if(!$head.ContainsKey($key)){throw "Missing calibration $key"}}
 if($head.display_id -ne $end.display_id -or $head.epoch -ne $end.epoch){throw 'Mixed calibration record/epoch'}
 foreach($key in @('display_id','epoch')){if($head[$key] -notmatch '^\d+$'){throw 'Invalid calibration identity'}}
 if($head.spectrum_valid -notin @('0','1')){throw 'Invalid calibration spectrum flag'}
 $profiles=@{};$names=@('instrument','record','spectrum')
 for($i=0;$i -lt 3;$i++){
  $name=$names[$i];$p=Convert-AcqFields $lines[$i+1].Value
  if($p.profile -ne $name){throw 'Duplicate or unordered calibration profile'}
  if($head[$name+'_id'] -notmatch '^[0-9a-fA-F]{8}$' -or $head[$name+'_flags'] -notin @('0','257','258','259')){throw 'Invalid calibration profile identity'}
  foreach($key in @('slope1_bits','offset1_bits','slope2_bits','offset2_bits')){if($p[$key] -notmatch '^[0-9a-fA-F]{8}$'){throw 'Invalid calibration coefficient bits'}}
  $profiles[$name]=$p
 }
 return @{status=$head;profiles=$profiles}
}
function Read-CalibrationState {
 [void](Invoke-AcqCommand '__query_boundary__' '(?m)^COMMANDS:[^\r\n]+\r?\n')
 $begin=$captured.Length
 [void](Invoke-AcqCommand 'calstatus' '(?m)^CALIBRATION_END display_id=\d+ epoch=\d+\r?\n')
 return Convert-CalibrationStatusFrame $captured.ToString().Substring($begin)
}
function Assert-CalibrationIdentity($Calibration,[string]$ExpectedId){
 $h=$Calibration.status
 if($h.instrument_flags -ne '259' -or $h.instrument_id -ne $ExpectedId){throw 'Unexpected running instrument calibration'}
}
function Read-CalibrationMeasurementState {
 $begin=$captured.Length;$m=Read-MeasurementState
 $body=$captured.ToString().Substring($begin)
 $lines=[regex]::Matches($body,'(?m)^MEASURE_(?:STATUS|CHANNEL|TIMING|END) [^\r\n]+\r?\n')
 $expected=@('MEASURE_STATUS','MEASURE_CHANNEL','MEASURE_TIMING','MEASURE_CHANNEL','MEASURE_TIMING','MEASURE_END')
 if($lines.Count -ne 6){throw 'Incomplete calibration measurement transaction'}
 for($i=0;$i -lt 6;$i++){if(!$lines[$i].Value.StartsWith($expected[$i]+' ')){throw 'Unordered calibration measurement transaction'}}
 $m.timing=@()
 for($ch=0;$ch -lt 2;$ch++){
  $timing=Convert-AcqFields $lines[2+2*$ch].Value
  if($timing.ch -ne [string]($ch+1)){throw 'Mixed timing channel'}
  foreach($flag in @('pulse_valid','rise_valid','fall_valid')){if($timing[$flag] -notin @('0','1')){throw 'Invalid timing flag'}}
  foreach($key in @('duty_percent','positive_us','negative_us','period_us','rise_us','fall_us','low_v','mid_v','high_v')){
   if(!$timing.ContainsKey($key)){throw 'Missing calibrated timing value'}
   $v=[double]$timing[$key];if([double]::IsNaN($v) -or [double]::IsInfinity($v)){throw 'Nonfinite timing value'}
  }
  $m.timing+=@($timing)
 }
 return $m
}
function Export-CalibratedRecord([string]$Name,[bool]$Frame,$Source,$Point){
 Export-LongRecord $Name $Frame
 $entry=$state.long_records[-1]
 $entry.measurement=Read-CalibrationMeasurementState
 $entry.calibration=Read-CalibrationState
 $entry.source=$Source;$entry.point=$Point
 Assert-CalibrationIdentity $entry.calibration $ExpectedCalibrationId
 if($entry.id -ne $entry.measurement.context.display_id -or $entry.id -ne $entry.calibration.status.display_id -or
    $entry.ui.epoch -ne $entry.measurement.context.epoch -or $entry.ui.epoch -ne $entry.calibration.status.epoch -or
    $entry.rate -ne [int]$entry.measurement.context.rate){throw 'Mixed calibrated record identity'}
}
