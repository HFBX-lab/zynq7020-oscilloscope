# Timing measurements and feature regression using temporary RAM; no SD/Flash writes.
param([switch]$PrepareOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent;Set-Location -LiteralPath $root
$out='D:/FPGA_Project/SP2_Timing_Build';$success=$false
$python='D:/Python/python.exe'
$checkpoint=@{state='running';flash_written=$false;sd_used=$false;started_utc=[DateTime]::UtcNow.ToString('o')}
try{
 & ./scripts/run_acq_ui_board_test.ps1 -PrepareOnly:$PrepareOnly -Timing -FirmwareDirectory build/timing-ui-firmware/freertos -HardwareDirectory build/highspeed-50m-soc -RtlDirectory build/highspeed-50m-rtl -PlacementFile D:/FPGA_Project/SP2_Highspeed_Build/50m/io-audit/capture-placements.csv -BoardRoot "$out/board" -InputDescription 'Historical setup: CH2 sine 50kHz, 1Vpp, +0.5VDC, common ground; FFT baseline restored and approximately 50kHz observed; no TF card or professional signal generator'
 if($LASTEXITCODE){throw 'Timing board runner failed'}
 if($PrepareOnly){$success=$true;$checkpoint.state='prepared-offline';return}
 $run=(Get-Content build/timing-ui-firmware/freertos/board-session.json -Raw|ConvertFrom-Json).run;$checkpoint.run=$run
 foreach($file in @('audit_highspeed_50m_evidence.py','audit_measurement_evidence.py','audit_autoset_evidence.py','audit_memory_evidence.py','audit_settings_evidence.py','audit_zero_markers_evidence.py','audit_timing_evidence.py')){
  & D:/Python/python.exe -X utf8 "scripts/$file" $run *> "$run/$file.log"
  if($LASTEXITCODE){throw "Independent audit failed:$file"}
 }
 & $python -X utf8 scripts/audit_spectrum_evidence.py $run *> "$run/audit_spectrum_evidence.py.log"
 if($LASTEXITCODE){throw 'FFT regression audit failed'}
 & D:/Python/python.exe -X utf8 tests/timing_evidence_negative_test.py $run *> "$run/timing-negative-tests.log"
 if($LASTEXITCODE){throw 'Timing actual-evidence negative tests failed'}
 Copy-Item -LiteralPath 'scripts/timing_reference.py','scripts/audit_timing_evidence.py','tests/timing_evidence_negative_test.py' -Destination $run
 $checkpoint.state='verified-board-independent-timing-awaiting-physical-feedback';$success=$true
 Write-Output "TIMING_BOARD_AND_AUDIT_PASS: $run;noFlash/noSD"
}catch{$checkpoint.state='failed';$checkpoint.error=$_.Exception.Message;throw}
finally{
 if(!$success -and !$PrepareOnly){try{& ./scripts/restore_accepted_m6.ps1;$checkpoint.fallback='restored-accepted-M6-690E0788'}catch{$checkpoint.fallback='failed';$checkpoint.fallback_error=$_.Exception.Message}}
 $checkpoint.completed_utc=[DateTime]::UtcNow.ToString('o');$checkpoint|ConvertTo-Json -Depth 5|Set-Content "$out/checkpoint.json" -Encoding utf8
}
