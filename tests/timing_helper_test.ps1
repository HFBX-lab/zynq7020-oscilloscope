param([string]$Root=(Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference='Stop'
. "$Root/scripts/acq_ui_session_helpers.ps1"
. "$Root/scripts/timing_session_helpers.ps1"
$header="MEASURE_STATUS panel=1 timing=1 channel=2 display_id=7 rate=50000000 count=4096 epoch=4 running=0`r`n"
$channel='MEASURE_TIMING ch=1 pulse_valid=1 rise_valid=1 fall_valid=0 cycles=3 duty_percent=20.000000 positive_us=4.000000 negative_us=16.000000 period_us=20.000000 rise_us=2.000000 fall_us=0.000000 low_v=0.100000 mid_v=0.500000 high_v=0.900000'
$body=$header+$channel+"`r`n"+$channel.Replace('ch=1','ch=2')+"`r`nMEASURE_END display_id=7`r`n"
$valid=Convert-TimingStatusFrame $body
if($valid.channels.Count -ne 2 -or $valid.context.rate -ne '50000000'){throw 'Valid timing reply rejected'}
$checks=1
foreach($bad in @($body.Replace('MEASURE_END display_id=7','MEASURE_END display_id=8'),$body.Replace('ch=2','ch=1'),$body.Replace('duty_percent=20.000000','duty_percent=NaN'),$body.Replace('fall_us=0.000000','fall_us=1.000000'),$body.Replace('pulse_valid=1','pulse_valid=2'),$body.Replace('cycles=3','cycles=1'),$body.Replace('period_us=20.000000','period_us=30.000000'),$body.Replace('rate=50000000','rate=0'),$body.Replace('low_v=0.100000','low_v=0.800000'),($body+$channel+"`r`n"))){
 $rejected=$false;try{$null=Convert-TimingStatusFrame $bad}catch{$rejected=$true};if(!$rejected){throw "Malformed timing reply accepted: $checks"};$checks++
}
"TIMING_HELPER_PASS: $checks checks"


