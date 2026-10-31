# Settings board test using temporary PL/RAM state; no SD or Flash writes.
param([switch]$PrepareOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $root
$out='D:/FPGA_Project/SP2_Settings_Build';$success=$false
$checkpoint=@{state='running';started_utc=[DateTime]::UtcNow.ToString('o');flash_written=$false;sd_used=$false}
try{
 & ./scripts/run_acq_ui_board_test.ps1 -PrepareOnly:$PrepareOnly -Parameters -FirmwareDirectory build/settings-ui-firmware/freertos -HardwareDirectory build/highspeed-50m-soc -RtlDirectory build/highspeed-50m-rtl -PlacementFile D:/FPGA_Project/SP2_Highspeed_Build/50m/io-audit/capture-placements.csv -BoardRoot "$out/board" -InputDescription 'CH2/IN2 sine50kHz1Vpp fixed+.5VDC common ground; user reconnected power2026-10-25; acceptedM5A restored and readonly measured49992..50050Hz'
 if($LASTEXITCODE){throw 'Settings board runner failed'}
 if($PrepareOnly){$success=$true;$checkpoint.state='prepared-offline';return}
 $run=(Get-Content build/settings-ui-firmware/freertos/board-session.json -Raw|ConvertFrom-Json).run;$checkpoint.run=$run
 foreach($audit in @('audit_highspeed_50m_evidence.py','audit_measurement_evidence.py','audit_autoset_evidence.py','audit_memory_evidence.py','audit_settings_evidence.py')){
  & D:/Python/python.exe -X utf8 "scripts/$audit" $run *> "$run/$audit.log"
  if($LASTEXITCODE){throw "Independent audit failed:$audit"}
 }
 & D:/Python/python.exe -X utf8 tests/settings_evidence_negative_test.py $run *> "$run/settings-negative-tests.log"
 if($LASTEXITCODE){throw 'Settings actual-evidence negative tests failed'}
 $checkpoint.state='verified-board-independent-settings-awaiting-current-physical-feedback';$success=$true
 Write-Output "SETTINGS_BOARD_AND_AUDIT_PASS: $run; RAM-only; noFlash/noSD"
}catch{$checkpoint.state='failed';$checkpoint.error=$_.Exception.Message;Write-Output "SETTINGS_BOARD_FAILED: $($checkpoint.error)";throw}
finally{
 if(!$success -and !$PrepareOnly){try{& ./scripts/restore_accepted_m5a.ps1;$checkpoint.fallback='restored-accepted-M5A'}catch{$checkpoint.fallback='failed';$checkpoint.fallback_error=$_.Exception.Message;Write-Output "RESTORE_ERROR: $($_.Exception.Message)"}}
 $checkpoint.completed_utc=[DateTime]::UtcNow.ToString('o');$checkpoint|ConvertTo-Json -Depth 5|Set-Content "$out/checkpoint.json" -Encoding utf8
}
