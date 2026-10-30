function Assert-CursorHeld($s){
 if($s.ui.display_id -ne $cursorHeldId -or $s.adc.display_crc -ne $cursorHeldCrc -or $s.ui.epoch -ne $cursorHeldEpoch -or $s.ui.running -ne '0' -or $s.ui.single_complete -ne $cursorExpectedComplete){throw 'Cursor operation modified frozen acquisition'}
}
function Test-CursorControls {
 $cursorExpectedComplete='1'
 $wideIndex=if($Highspeed50M){11}elseif($Highspeed10M -or $Highspeed25M){8}else{3};$defaultIndex=if($Highspeed50M){10}elseif($Highspeed10M -or $Highspeed25M){7}else{2}
 Send-AcqCommands @('source 2','trigsource 2','dual 1','cursors 1','cursor 250 750','single')
 $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0' -and $x.ui.cursor_valid -eq '1'}
 $cursorHeldId=$s.ui.display_id;$cursorHeldCrc=$s.adc.display_crc;$cursorHeldEpoch=$s.ui.epoch
 if(([math]::Abs([double]$s.ui.cursor_dt_us-1000) -gt 0.0001) -or ([math]::Abs([double]$s.ui.cursor_inv_hz-1000) -gt 0.0001)){throw 'Default cursor interval wrong'}
 Note-Acq 'cursor-default-frozen-1000us' $s
 Send-AcqCommands @('cursor 1001 750');$s=Read-AcqState;Assert-CursorHeld $s
 if($s.ui.cursor_a -ne '250' -or $s.ui.cursor_b -ne '750'){throw 'Out-of-bounds cursor command was accepted'}
 Note-Acq 'cursor-invalid-position-rejected' $s
 Send-AcqCommands @('cursor 500 500');$s=Read-AcqState;Assert-CursorHeld $s
 if([double]$s.ui.cursor_dt_us -ne 0 -or [double]$s.ui.cursor_inv_hz -ne 0){throw 'Coincident cursor interval was fabricated'}
 Note-Acq 'cursor-coincident-no-inverse' $s
 Send-AcqCommands @('cursor 250 750','tap 322 260')
 $s=Wait-AcqState {param($x) [int]$x.ui.cursor_a -ge 395 -and [int]$x.ui.cursor_a -le 405}
 Assert-CursorHeld $s;if($s.ui.cursor_b -ne '750'){throw 'Pointer moved the wrong cursor'}
 Note-Acq 'cursor-LVGL-pointer-select-nearest' $s
 if($LongTimebase -or $HardwareTrigger){$cursorExpectedComplete='0'} # New sampling configuration clears completion but preserves the raw record.
 Send-AcqCommands @('cursor 250 750','tap 950 274')
 $s=Wait-AcqState {param($x) [int]$x.ui.time -eq $wideIndex}
 if([uint32]$s.ui.epoch -ne ([uint32]$cursorHeldEpoch+1)){throw 'Timebase change did not advance epoch once'}
 $cursorHeldEpoch=$s.ui.epoch;Assert-CursorHeld $s
 if([math]::Abs([double]$s.ui.cursor_dt_us-2500) -gt 0.0001){throw 'Cursor interval did not track timebase'}
 Note-Acq 'cursor-timebase-physical-interval' $s
 Send-AcqCommands @('tap 840 274');$s=Wait-AcqState {param($x) [int]$x.ui.time -eq $defaultIndex}
 if([uint32]$s.ui.epoch -ne ([uint32]$cursorHeldEpoch+1)){throw 'Timebase restore did not advance epoch once'}
 $cursorHeldEpoch=$s.ui.epoch;Assert-CursorHeld $s
 $dv=$s.ui.cursor_dv
 Send-AcqCommands @('tap 950 358');$s=Wait-AcqState {param($x) $x.ui.scale -eq '3'};Assert-CursorHeld $s
 if($s.ui.cursor_dv -ne $dv){throw 'Cursor used clipped plot values instead of raw voltage'}
 Note-Acq 'cursor-voltage-independent-of-display-scale' $s
 Send-AcqCommands @('tap 840 358','tap 738 32')
 $s=Wait-AcqState {param($x) $x.ui.cursors -eq '0'};Assert-CursorHeld $s
 Note-Acq 'cursor-pointer-off-keeps-single' $s
 Send-AcqCommands @('tap 738 32');$s=Wait-AcqState {param($x) $x.ui.cursors -eq '1'};Assert-CursorHeld $s
 Note-Acq 'cursor-pointer-on-keeps-single' $s
 if($LongTimebase -or $HardwareTrigger){
  Send-AcqCommands @('single');$s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0'}
  $cursorExpectedComplete='1';$cursorHeldId=$s.ui.display_id;$cursorHeldCrc=$s.adc.display_crc;$cursorHeldEpoch=$s.ui.epoch
  Note-Acq 'cursor-rearmed-after-timebase-reconfiguration' $s
 }
 Export-DualChannelFrame 'cursor'
 $state.cursor_frame_ui | ConvertTo-Json -Depth 3 | Set-Content "$run/cursor-frame-status.json" -Encoding utf8
 $cursorAuditOptions=@();if($Highspeed50M){$cursorAuditOptions=@("--highspeed-50m")}elseif($Highspeed10M -or $Highspeed25M){$cursorAuditOptions=@("--highspeed-10m")}
 & D:/Python/python.exe -X utf8 "$run/check_cursor_frame.py" "$run" @cursorAuditOptions *> "$run/cursor-numeric-audit.log"
 if($LASTEXITCODE){throw 'Cursor independent raw/frame audit failed'}
 Note-Acq 'cursor-independent-record-values-and-visible-lines' (Read-AcqState)
 Send-AcqCommands @('source 1');$s=Read-AcqState
 if($s.ui.source -ne 'ADC1' -or $s.ui.cursor_valid -ne '1' -or [math]::Abs([double]$s.ui.cursor_dv) -gt .1){throw 'ADC1 cursor source selection failed'}
 Note-Acq 'cursor-ADC1-readout-selection' $s
 Send-AcqCommands @('source 3');$s=Read-AcqState
 if($s.ui.source -ne 'SIM' -or $s.ui.cursor_valid -ne '0' -or $s.ui.cursors -ne '1'){throw 'Simulation cursor isolation failed'}
 Note-Acq 'cursor-hidden-in-SIM-preference-kept' $s
 Send-AcqCommands @('source 2','trigsource 2','trigger rise 500','triggermode auto','dual 1','cursors 1','cursor 250 750','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC2' -and $x.ui.cursors -eq '1' -and $x.ui.cursor_valid -eq '1' -and $x.ui.triggered -eq '1' -and $x.ui.running -eq '1'}
 Note-Acq 'final-dual-ADC2-cursors-running' $s
}
