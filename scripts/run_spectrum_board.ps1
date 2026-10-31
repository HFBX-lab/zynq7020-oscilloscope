# FFT board test using temporary PL/RAM state; no SD or Flash writes.
param([switch]$PrepareOnly,[string]$RetestFrom='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent;Set-Location -LiteralPath $root
$out='D:/FPGA_Project/SP2_Spectrum_Build';$success=$false
$SpectrumPython='D:/Python/python.exe'
$checkpoint=@{state='running';started_utc=[DateTime]::UtcNow.ToString('o');flash_written=$false;sd_used=$false}
try{
 & ./scripts/run_acq_ui_board_test.ps1 -PrepareOnly:$PrepareOnly -Spectrum -SpectrumRetest:([bool]$RetestFrom) -PriorSpectrumRun $RetestFrom -FirmwareDirectory build/settings-ui-firmware-spectrum/freertos -HardwareDirectory build/highspeed-50m-soc -RtlDirectory build/highspeed-50m-rtl -PlacementFile D:/FPGA_Project/SP2_Highspeed_Build/50m/io-audit/capture-placements.csv -BoardRoot "$out/board" -InputDescription 'CH2/IN2 sine50kHz1Vpp fixed+.5VDC common ground; no input adjustment; readonly current program confirmed approx50kHz; original volatile slots backed up before power cycle'
 if($LASTEXITCODE){throw 'Spectrum board runner failed'}
 if($PrepareOnly){$success=$true;$checkpoint.state='prepared-offline';return}
 $run=(Get-Content build/settings-ui-firmware-spectrum/freertos/board-session.json -Raw|ConvertFrom-Json).run;$checkpoint.run=$run
 $oldRun=if($RetestFrom){$RetestFrom}else{$run}
 if($RetestFrom){$checkpoint.prior_full_run=$RetestFrom}
 foreach($audit in @('audit_highspeed_50m_evidence.py','audit_measurement_evidence.py','audit_autoset_evidence.py','audit_memory_evidence.py','audit_settings_evidence.py','audit_zero_markers_evidence.py')){
  & D:/Python/python.exe -X utf8 "scripts/$audit" $oldRun *> "$run/$audit.log"
  if($LASTEXITCODE){throw "Independent audit failed:$audit"}
 }
 & $SpectrumPython -X utf8 scripts/audit_spectrum_evidence.py $run *> "$run/audit_spectrum_evidence.py.log"
 if($LASTEXITCODE){throw 'Independent all-bin spectrum audit failed'}
 & $SpectrumPython -X utf8 tests/spectrum_evidence_negative_test.py $run *> "$run/spectrum-negative-tests.log"
 if($LASTEXITCODE){throw 'Spectrum actual-evidence negative tests failed'}
 $checkpoint.state='verified-board-independent-spectrum-awaiting-physical-feedback';$success=$true
 Write-Output "SPECTRUM_BOARD_AND_AUDIT_PASS: $run; noFlash/noSD"
}catch{$checkpoint.state='failed';$checkpoint.error=$_.Exception.Message;Write-Output "SPECTRUM_BOARD_FAILED: $($checkpoint.error)";throw}
finally{
 if(!$success -and !$PrepareOnly){try{& ./scripts/restore_accepted_m5b.ps1;$checkpoint.fallback='restored-accepted-M5B-DF5555DF'}catch{$checkpoint.fallback='failed';$checkpoint.fallback_error=$_.Exception.Message;Write-Output "RESTORE_ERROR: $($_.Exception.Message)"}}
 $checkpoint.completed_utc=[DateTime]::UtcNow.ToString('o');$checkpoint|ConvertTo-Json -Depth 5|Set-Content "$out/checkpoint.json" -Encoding utf8
}
