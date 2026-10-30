# Current user requested the layout correction; temporary PL/RAM only.
param([switch]$PrepareOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent;Set-Location -LiteralPath $root
$out='D:/FPGA_Project/SP2_ZeroMarkers_Build';$success=$false
$checkpoint=@{state='running';started_utc=[DateTime]::UtcNow.ToString('o');flash_written=$false;sd_used=$false}
try{
 & ./scripts/run_acq_ui_board_test.ps1 -PrepareOnly:$PrepareOnly -ZeroMarkers -FirmwareDirectory build/settings-ui-firmware-markers/freertos -HardwareDirectory build/highspeed-50m-soc -RtlDirectory build/highspeed-50m-rtl -PlacementFile D:/FPGA_Project/SP2_Highspeed_Build/50m/io-audit/capture-placements.csv -BoardRoot "$out/board" -InputDescription 'CH2/IN2 sine50kHz1Vpp fixed+.5VDC common ground, unchanged; both user slots exported read-only before reload'
 if($LASTEXITCODE){throw 'Zero marker board runner failed'}
 if($PrepareOnly){$success=$true;$checkpoint.state='prepared-offline';return}
 $run=(Get-Content build/settings-ui-firmware-markers/freertos/board-session.json -Raw|ConvertFrom-Json).run;$checkpoint.run=$run
 foreach($audit in @('audit_highspeed_50m_evidence.py','audit_measurement_evidence.py','audit_autoset_evidence.py','audit_memory_evidence.py','audit_settings_evidence.py','audit_zero_markers_evidence.py')){
  & D:/Python/python.exe -X utf8 "scripts/$audit" $run *> "$run/$audit.log"
  if($LASTEXITCODE){throw "Independent audit failed:$audit"}
 }
 & D:/Python/python.exe -X utf8 tests/zero_markers_evidence_test.py $run *> "$run/zero-marker-negative-tests.log"
 if($LASTEXITCODE){throw 'Zero marker actual-evidence tests failed'}
 $checkpoint.state='verified-board-independent-zero-markers-awaiting-physical-feedback';$success=$true
 Write-Output "ZERO_MARKER_BOARD_AND_AUDIT_PASS: $run; noFlash/noSD"
}catch{$checkpoint.state='failed';$checkpoint.error=$_.Exception.Message;Write-Output "ZERO_MARKER_BOARD_FAILED: $($checkpoint.error)";throw}
finally{
 if(!$success -and !$PrepareOnly){try{& ./scripts/restore_baseline_m5b.ps1;$checkpoint.fallback='restored-baseline-M5B'}catch{$checkpoint.fallback='failed';$checkpoint.fallback_error=$_.Exception.Message;Write-Output "RESTORE_ERROR: $($_.Exception.Message)"}}
 $checkpoint.completed_utc=[DateTime]::UtcNow.ToString('o');$checkpoint|ConvertTo-Json -Depth 5|Set-Content "$out/checkpoint.json" -Encoding utf8
}
