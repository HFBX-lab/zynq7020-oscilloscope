function Assert-ChannelHeld($s){
 if($s.ui.display_id -ne $channelHeldId -or $s.adc.display_crc -ne $channelHeldCrc -or $s.ui.epoch -ne $channelHeldEpoch -or $s.ui.running -ne '0' -or $s.ui.single_complete -ne '1' -or $s.ui.view_start -ne $channelHeldStart -or $s.ui.crossing -ne $channelHeldCross -or $s.ui.cursor_dv -ne $channelHeldDv){throw 'Vertical display change modified frozen data/trigger/cursor'}
}
function Test-ChannelControls {
 Send-AcqCommands @('source 2','trigsource 2','vertical 1 2 0','vertical 2 2 0','single')
 $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0'}
 $channelHeldId=$s.ui.display_id;$channelHeldCrc=$s.adc.display_crc;$channelHeldEpoch=$s.ui.epoch
 $channelHeldStart=$s.ui.view_start;$channelHeldCross=$s.ui.crossing;$channelHeldDv=$s.ui.cursor_dv
 Send-AcqCommands @('vertical 1 1 1500');$s=Read-AcqState;Assert-ChannelHeld $s
 if($s.ui.ch1_scale -ne '1' -or $s.ui.ch1_pos -ne '1500' -or $s.ui.ch2_scale -ne '2' -or $s.ui.ch2_pos -ne '0' -or $s.ui.scale -ne '2'){throw 'CH1 independent display configuration failed'}
 Note-Acq 'vertical-CH1-independent-keeps-frozen-raw-trigger-cursor' $s
 Send-AcqCommands @('vertical 2 3 -1500');$s=Read-AcqState;Assert-ChannelHeld $s
 if($s.ui.ch2_scale -ne '3' -or $s.ui.ch2_pos -ne '-1500' -or $s.ui.ch1_scale -ne '1' -or $s.ui.ch1_pos -ne '1500' -or $s.ui.scale -ne '3'){throw 'CH2 independent display configuration failed'}
 Note-Acq 'vertical-CH2-independent-active-scale-alias' $s
 Send-AcqCommands @('vertical 3 2 0','vertical 2 7 0','vertical 1 1 3501');$s=Read-AcqState;Assert-ChannelHeld $s
 if($s.ui.ch1_scale -ne '1' -or $s.ui.ch1_pos -ne '1500' -or $s.ui.ch2_scale -ne '3' -or $s.ui.ch2_pos -ne '-1500'){throw 'Invalid vertical command changed channel configuration'}
 Note-Acq 'vertical-invalid-channel-scale-position-rejected' $s
 Send-AcqCommands @('tap 900 498');$s=Wait-AcqState {param($x) $x.ui.channel_panel -eq '1' -and $x.ui.config_ch -eq '2'};Assert-ChannelHeld $s
 Note-Acq 'vertical-pointer-opens-channel-settings' $s
 Send-AcqCommands @('tap 410 192','tap 614 283','tap 614 373')
 $s=Wait-AcqState {param($x) $x.ui.config_ch -eq '1' -and $x.ui.ch1_scale -eq '2' -and $x.ui.ch1_pos -eq '1750'};Assert-ChannelHeld $s
 if($s.ui.source -ne 'ADC2' -or $s.ui.ch2_scale -ne '3' -or $s.ui.ch2_pos -ne '-1500'){throw 'Editing CH1 changed trigger/measurement channel or CH2 display'}
 Note-Acq 'vertical-pointer-configure-other-channel-without-source-change' $s
 Send-AcqCommands @('tap 410 437');$s=Wait-AcqState {param($x) $x.ui.ch1_pos -eq '0'};Assert-ChannelHeld $s
 if($s.ui.ch2_pos -ne '-1500'){throw 'Zero-position reset affected another channel'}
 Note-Acq 'vertical-pointer-zero-only-selected-channel' $s
 Send-AcqCommands @('tap 614 192','tap 410 283','tap 410 373')
 $s=Wait-AcqState {param($x) $x.ui.ch2_scale -eq '2' -and $x.ui.scale -eq '2' -and $x.ui.ch2_pos -eq '-1750'};Assert-ChannelHeld $s
 Note-Acq 'vertical-pointer-active-channel-scale-and-position' $s
 Send-AcqCommands @('tap 655 437');$s=Wait-AcqState {param($x) $x.ui.channel_panel -eq '0'};Assert-ChannelHeld $s
 Send-AcqCommands @('vertical 1 1 1500','vertical 2 3 -1500')
 Export-DualChannelFrame 'channel'
 $state.channel_frame_ui|ConvertTo-Json -Depth 3|Set-Content "$run/channel-frame-status.json" -Encoding utf8
 & D:/Python/python.exe -X utf8 "$run/check_channel_frame.py" "$run" *> "$run/channel-plot-audit.log"
 if($LASTEXITCODE){throw 'Independent raw-to-channel-plot audit failed'}
 Note-Acq 'vertical-independent-frame-position-scale-and-zero-references' (Read-AcqState)
 Send-AcqCommands @('source 1');$s=Read-AcqState
 if($s.ui.scale -ne '1' -or $s.ui.ch1_pos -ne '1500'){throw 'Source switch lost CH1 scale/position'}
 Send-AcqCommands @('source 3');$s=Read-AcqState
 if($s.ui.scale -ne '2' -or $s.ui.dual_visible -ne '0'){throw 'Demo inherited ADC vertical scale'}
 Send-AcqCommands @('source 2');$s=Read-AcqState
 if($s.ui.scale -ne '3' -or $s.ui.ch1_scale -ne '1' -or $s.ui.ch2_pos -ne '-1500'){throw 'Return to CH2 lost independent display state'}
 Note-Acq 'vertical-source-and-demo-scale-retention' $s
 Send-AcqCommands @('triggermode auto','trigger rise 500','dual 1','cursors 0','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC2' -and $x.ui.running -eq '1' -and $x.ui.triggered -eq '1' -and $x.ui.ch1_pos -eq '1500' -and $x.ui.ch2_pos -eq '-1500'}
 Note-Acq 'final-independent-CH1-CH2-running' $s
}
