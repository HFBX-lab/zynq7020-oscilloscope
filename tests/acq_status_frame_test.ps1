$ErrorActionPreference='Stop'
. "$PSScriptRoot/../scripts/acq_ui_session_helpers.ps1"
$fields='source=ADC2 display_id=2 seen_id=3 trig_normal=1 trig_rising=1 level_mv=500 triggered=0 view_start=0 crossing=0 waiting=1 single_armed=0 single_complete=0 epoch=2'
$stale="UI_STATUS source=ADC1 display_id=99`nADC_LIVE_STATUS available=1 id=99 display_crc=bad`n"
$first="COMMANDS: status | snapshot | resume`r`n"
$body="UI_STATUS $fields`r`nADC_LIVE_STATUS available=1 id=3 display_crc=12345678`r`n"
if($null -ne (Convert-AcqStatusFrame ($stale+$first+$body+'COMMANDS: partial'))){throw 'Partial closing frame accepted'}
$result=Convert-AcqStatusFrame ($stale+$first+$body+$first)
if($result.ui.display_id -ne '2' -or $result.adc.id -ne '3'){throw 'Stale unframed status affected result'}
Write-Output 'ACQ_STATUS_FRAME_TEST_PASS: complete framing excludes stale and partial UART responses'
