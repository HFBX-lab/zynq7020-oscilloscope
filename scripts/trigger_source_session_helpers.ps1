function Assert-IndependentFrozen($s){
 if($s.ui.display_id -ne $independentId -or $s.adc.display_crc -ne $independentCrc -or $s.ui.epoch -ne $independentEpoch -or $s.ui.running -ne '0' -or $s.ui.single_complete -ne '1' -or $s.ui.view_start -ne $independentStart -or $s.ui.crossing -ne $independentCross){throw 'Measurement selection changed frozen trigger/record state'}
}
function Test-IndependentTriggerControls {
 Send-AcqCommands @('source 2','trigsource 2','triggermode normal','trigger rise 500','dual 1','cursors 0','single')
 $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0' -and $x.ui.trig_channel -eq '2'}
 $independentId=$s.ui.display_id;$independentCrc=$s.adc.display_crc;$independentEpoch=$s.ui.epoch;$independentStart=$s.ui.view_start;$independentCross=$s.ui.crossing
 Send-AcqCommands @('source 1');$s=Read-AcqState;Assert-IndependentFrozen $s
 if($s.ui.source -ne 'ADC1' -or $s.ui.trig_channel -ne '2' -or $s.ui.triggered -ne '1'){throw 'CH1 measurement lost CH2 trigger'}
 Note-Acq 'independent-measurement-switch-preserves-completed-single' $s
 Send-AcqCommands @('trigsource 0','trigsource 3');$s=Read-AcqState;Assert-IndependentFrozen $s
 if($s.ui.trig_channel -ne '2'){throw 'Invalid trigger channel was accepted'}
 Note-Acq 'independent-invalid-trigger-source-rejected' $s
 Export-DualChannelFrame 'trigsrc'
 $state.trigsrc_frame_ui|ConvertTo-Json -Depth 3|Set-Content "$run/trigsrc-frame-status.json" -Encoding utf8
 $auditOptions=@();if($HardwareTrigger){$auditOptions=@("--hardware-trigger")}
 & D:/Python/python.exe -X utf8 "$run/check_trigger_source_record.py" "$run" @auditOptions *> "$run/trigsrc-record-audit.log"
 if($LASTEXITCODE){throw 'Independent trigger raw record audit failed'}
 Note-Acq 'independent-CH2-crossing-CH1-measurement-raw-proof' (Read-AcqState)
 Send-AcqCommands @('acqrun')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC1' -and $x.ui.trig_channel -eq '2' -and $x.ui.triggered -eq '1' -and [uint32]$x.ui.display_id -gt [uint32]$independentId}
 Note-Acq 'independent-CH2-trigger-CH1-measurement-normal-live' $s
 Send-AcqCommands @('trigsource 1')
 $s=Wait-AcqState {param($x) $x.ui.trig_channel -eq '1' -and $x.ui.waiting -eq '1'}
 $held=$s.ui.display_id;$heldCrc=$s.adc.display_crc;$raw=[uint32]$s.adc.id
 $s=if($HardwareTrigger){Wait-HardwareWaitingProgress}else{Wait-AcqState {param($x) [uint32]$x.adc.id -ge $raw+3}}
 if($s.ui.display_id -ne $held -or $s.adc.display_crc -ne $heldCrc -or $s.ui.trig_channel -ne '1' -or $s.ui.waiting -ne '1'){throw 'Empty CH1 trigger did not retain NORMAL display'}
 Note-Acq 'independent-empty-CH1-trigger-waits-keeps-display' $s
 $epoch=$s.ui.epoch
 Send-AcqCommands @('source 2');$s=Read-AcqState
 if($s.ui.source -ne 'ADC2' -or $s.ui.trig_channel -ne '1' -or $s.ui.epoch -ne $epoch -or $s.ui.waiting -ne '1' -or $s.ui.display_id -ne $held -or $s.adc.display_crc -ne $heldCrc){throw 'Measurement switch changed trigger source/epoch or bypassed NORMAL hold'}
 Note-Acq 'independent-measurement-does-not-bypass-empty-trigger' $s
 Send-AcqCommands @('single');$s=Wait-AcqState {param($x) $x.ui.single_armed -eq '1' -and $x.ui.waiting -eq '1'}
 $raw=[uint32]$s.adc.id
 $s=if($HardwareTrigger){Wait-HardwareWaitingProgress}else{Wait-AcqState {param($x) [uint32]$x.adc.id -ge $raw+2 -and $x.ui.single_armed -eq '1'}}
 Note-Acq 'independent-single-waits-for-selected-trigger' $s
 $epoch=$s.ui.epoch
 Send-AcqCommands @('source 1');$s=Read-AcqState
 if($s.ui.epoch -ne $epoch -or $s.ui.single_armed -ne '1' -or $s.ui.trig_channel -ne '1'){throw 'Measurement switch cancelled armed single'}
 Note-Acq 'independent-measurement-preserves-armed-single' $s
 $seen=[uint32]$s.ui.seen_id
 Send-AcqCommands @('trigsource 2')
 $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0' -and $x.ui.source -eq 'ADC1' -and $x.ui.trig_channel -eq '2'}
 if([uint32]$s.ui.epoch -le [uint32]$epoch -or [uint32]$s.ui.display_id -le $seen -or $s.ui.triggered -ne '1'){throw 'Trigger-source change completed with old epoch/data'}
 Note-Acq 'independent-trigger-change-new-epoch-completes-single' $s
 $independentId=$s.ui.display_id;$independentCrc=$s.adc.display_crc;$independentEpoch=$s.ui.epoch;$independentStart=$s.ui.view_start;$independentCross=$s.ui.crossing
 Send-AcqCommands @('tap 900 192');$s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC2'};Assert-IndependentFrozen $s
 Send-AcqCommands @('tap 834 192');$s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC1'};Assert-IndependentFrozen $s
 Note-Acq 'independent-frozen-pointer-measurement-selection' $s
 Send-AcqCommands @('tap 526 94');$s=Wait-AcqState {param($x) $x.ui.trig_channel -eq '1'}
 if([uint32]$s.ui.epoch -le [uint32]$independentEpoch -or $s.ui.display_id -ne $independentId -or $s.adc.display_crc -ne $independentCrc -or $s.ui.running -ne '0' -or $s.ui.source -ne 'ADC1'){throw 'Trigger-source pointer did not invalidate configuration while retaining frozen data'}
 Send-AcqCommands @('tap 526 94');$s=Wait-AcqState {param($x) $x.ui.trig_channel -eq '2'}
 Note-Acq 'independent-pointer-trigger-source-epoch-data-retention' $s
 Send-AcqCommands @('source 3');$s=Read-AcqState;$epoch=$s.ui.epoch
 Send-AcqCommands @('tap 526 94');$s=Read-AcqState
 if($s.ui.source -ne 'SIM' -or $s.ui.trig_channel -ne '2' -or $s.ui.epoch -ne $epoch){throw 'DEMO trigger preference/disabled button failed'}
 Note-Acq 'independent-DEMO-trigger-preference-disabled-pointer' $s
 Send-AcqCommands @('source 1','trigsource 2','triggermode auto','trigger rise 500','dual 1','cursors 0','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC1' -and $x.ui.trig_channel -eq '2' -and $x.ui.running -eq '1' -and $x.ui.triggered -eq '1'}
 Note-Acq 'final-CH1-measurement-CH2-trigger-running' $s
}
