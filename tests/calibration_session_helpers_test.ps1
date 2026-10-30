$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. "$root/scripts/acq_ui_session_helpers.ps1"
$helper="$root/scripts/calibration_session_helpers.ps1"
. $helper
$valid="CALIBRATION_STATUS display_id=5 epoch=2 instrument_flags=259 instrument_id=2b474854 record_flags=259 record_id=2b474854 spectrum_valid=0 spectrum_flags=0 spectrum_id=00000000`r`n"
foreach($name in @('instrument','record','spectrum')){$valid+="CALIBRATION_COEFFICIENTS profile=$name slope1_bits=3c19e024 offset1_bits=c0976997 slope2_bits=3c158507 offset2_bits=c094a49b`r`n"}
$valid+="CALIBRATION_END display_id=5 epoch=2`r`n"
$parsed=Convert-CalibrationStatusFrame $valid
Assert-CalibrationIdentity $parsed '2b474854'
if($parsed.profiles.Count -ne 3){throw 'Valid profile parse failed'}
$bad=@(
 $valid.Replace('CALIBRATION_END display_id=5','CALIBRATION_END display_id=6'),
 $valid.Replace('CALIBRATION_END display_id=5 epoch=2','CALIBRATION_END display_id=5 epoch=3'),
 $valid.Replace('profile=record','profile=instrument'),
 $valid.Replace('slope1_bits=3c19e024','slope1_bits=garbage'),
 $valid.Replace('record_flags=259','record_flags=260'),
 $valid.Replace('spectrum_valid=0','spectrum_valid=2'),
 $valid.Substring(0,$valid.IndexOf('CALIBRATION_END')),
 ($valid+$valid)
)
foreach($body in $bad){$rejected=$false;try{$null=Convert-CalibrationStatusFrame $body}catch{$rejected=$true};if(!$rejected){throw 'Malformed calibration frame accepted'}}
$wrong=$false;try{Assert-CalibrationIdentity $parsed '00000001'}catch{$wrong=$true};if(!$wrong){throw 'Wrong profile accepted'}
Write-Output 'CALIBRATION_HELPER_TEST_PASS: valid atomic frame and9 rejection cases; offline'
