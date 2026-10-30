# Used only within the authorized parent runner's owned UART/JTAG session.
function Convert-ParameterStatusFrame([string]$Body){
 $heads=[regex]::Matches($Body,'(?m)^PARAM_STATUS [^\r\n]+\r?\n');$configs=[regex]::Matches($Body,'(?m)^PARAM_CONFIG [^\r\n]+\r?\n');$ends=[regex]::Matches($Body,'(?m)^PARAM_END [^\r\n]+\r?\n')
 if($heads.Count -ne 1 -or $configs.Count -ne 1 -or $ends.Count -ne 1){throw 'Incomplete parameter frame'}
 $h=Convert-AcqFields $heads[0].Value;$c=Convert-AcqFields $configs[0].Value;$e=Convert-AcqFields $ends[0].Value
 foreach($key in @('valid','replay','panel','notice','blob_address','blob_bytes','blob_crc','epoch','running','display_id','rate','record_address','record_bytes','record_crc','actual_conversion','wave_valid','wave_crc')){if(!$h.ContainsKey($key)){throw "Missing parameter $key"}}
 foreach($key in @('time_ns','origin_conversion','selected_channel','trigger_channel','level_mv','flags','ch1_scale','ch2_scale','ch1_pos','ch2_pos','cursor_a','cursor_b','language')){if(!$c.ContainsKey($key)){throw "Missing parameter configuration $key"}}
 if($h.epoch -ne $e.epoch -or $heads[0].Index -gt $configs[0].Index -or $configs[0].Index -gt $ends[0].Index){throw 'Mixed or unordered parameter frame'}
 if($h.blob_bytes -ne '96' -or $h.valid -notin @('0','1') -or $h.replay -notin @('0','1') -or $h.actual_conversion -ne '50000000'){throw 'Invalid parameter metadata'}
 foreach($key in @('blob_address','blob_crc','record_address','record_crc','wave_crc')){if($h[$key] -notmatch '^[A-Fa-f0-9]{8}$'){throw 'Invalid parameter address/CRC'}}
 return @{status=$h;config=$c}
}
function Read-ParameterState {
 [void](Invoke-AcqCommand '__query_boundary__' '(?m)^COMMANDS:[^\r\n]+\r?\n');$begin=$captured.Length
 [void](Invoke-AcqCommand 'paramstatus' '(?m)^PARAM_END epoch=\d+\r?\n')
 return Convert-ParameterStatusFrame $captured.ToString().Substring($begin)
}
function Read-ParameterSnapshot {
 $s=Read-MemorySnapshot;$p=Read-ParameterState
 foreach($pair in @(@('epoch','epoch'),@('running','running'),@('display_id','display_id'))){if($p.status[$pair[0]] -ne $s.ui[$pair[1]]){throw 'Parameter snapshot mixed UI'}}
 foreach($key in @('record_address','record_crc','rate')){if($p.status[$key] -ne $s.memory.view[$key]){throw 'Parameter snapshot mixed raw'}}
 if($p.status.wave_crc -ne $s.memory.status.blob_crc -or $p.status.wave_valid -ne $s.memory.status.valid -or $p.status.record_bytes -ne '16384'){throw 'Parameter snapshot mixed wave slot/length'}
 $s.parameter=$p;return $s
}
function Note-Parameter([string]$Name,$Snapshot){$state.parameter_checks+=@{test=$Name;snapshot=$Snapshot};[void](Note-Acq $Name $Snapshot)}
function Assert-ParameterRawHeld($Before,$After,[uint64]$Advance){
 Assert-MemoryEpoch $Before $After $Advance
 Assert-MemoryFields $Before.ui $After.ui @('display_id','seen_id','running') 'Parameter held UI'
 Assert-MemoryFields $Before.memory.view $After.memory.view @('record_address','record_crc','rate','conversion','filter','display_epoch') 'Parameter held raw'
 Assert-MemoryFields $Before.memory.status $After.memory.status @('valid','blob_crc','saved_id','saved_rate','saved_epoch') 'Parameter held wave'
 for($ch=0;$ch -lt 2;$ch++){Assert-MemoryFields $Before.measurement.channels[$ch] $After.measurement.channels[$ch] @('min_v','max_v','mean_v','vpp_v','rms_total_v','rms_ac_v','frequency_hz','period_us') 'Parameter held measurements'}
}
function Assert-ParameterPreferences($Before,$After){
 Assert-MemoryFields $Before.ui $After.ui @('time','source','ch1_scale','ch2_scale','ch1_pos','ch2_pos','trig_channel','trig_normal','trig_rising','level_mv','dual','grid','cursors','cursor_a','cursor_b','lang') 'Parameter preferences'
}
function Tap-Parameter([int]$X,[int]$Y){Send-AcqCommands @("tap $X $Y");$null=Wait-AcqState {param($x) $x.ui.touch_pending -eq '0'}}
function Invoke-ParameterQueuedStop {
 [void](Invoke-AcqCommand '__query_boundary__' '(?m)^COMMANDS:[^\r\n]+\r?\n');$begin=$captured.Length
 [void](Invoke-AcqCommand ("paramsave`nacqstop") '(?m)^UI_STATUS [^\r\n]+ running=0 [^\r\n]+\r?\n')
 $body=$captured.ToString().Substring($begin);$acks=[regex]::Matches($body,'(?m)^UI_STATUS [^\r\n]+\r?\n')
 if($acks.Count -ne 2 -or $body -notmatch '(?m)^PARAM_COMMAND paramsave OK\r?$'){throw 'Queued parameter save/stop missing'}
 return @{save_ack=(Convert-AcqFields $acks[0].Value);stop_ack=(Convert-AcqFields $acks[1].Value)}
}
function Assert-ParameterRunningSave($Running,$WaveBefore,$Ack,$Updated){
 $save=$Ack.save_ack;$stop=$Ack.stop_ack
 if($save.running -ne '1' -or $stop.running -ne '0' -or $save.epoch -ne $Running.ui.epoch -or [uint64]$stop.epoch -ne (([uint64]$save.epoch+1)%4294967296) -or $Updated.ui.epoch -ne $stop.epoch -or $save.display_id -ne $Updated.ui.display_id -or $save.display_id -ne $stop.display_id -or $WaveBefore.status.blob_crc -ne $Updated.parameter.status.wave_crc){throw 'Running parameter save mutated epoch/data/wave'}
}
function Export-Parameter([string]$Name){
 if($Name -notmatch '^param-[a-z0-9-]+$'){throw 'Unsafe parameter export name'}
 $s=Read-ParameterSnapshot;$p=$s.parameter
 if($p.status.valid -ne '1' -or $p.status.wave_valid -ne '1'){throw 'Both slots required for parameter export'}
 Assert-MemoryElfRange $p.status.blob_address 96;Assert-MemoryExportMetadata $s
 $f=Convert-AcqFields (Invoke-AcqCommand 'snapshot' '(?m)^SNAPSHOT_READY[^\r\n]+\r?\n');$state.resume_needed=$true
 if($f.addr -notmatch '^10[02]00000$' -or $f.crc -notmatch '^[0-9A-Fa-f]{8}$' -or $f.bytes -ne '1843200' -or $f.format -ne 'BGR888'){throw 'Invalid parameter frame metadata'}
 $frameCrc=$f.crc
 @'
if {[catch {
connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "APU"}
configparams force-mem-accesses 1
mrd -bin -file {@RUN@/@NAME@.scp} 0x@PARAM@ 24
mrd -bin -file {@RUN@/@NAME@.scw} 0x@WAVE@ 4128
mrd -bin -file {@RUN@/@NAME@.bin} 0x@RAW@ 4096
mrd -bin -file {@RUN@/@NAME@-frame.bin} 0x@FRAME@ 460800
puts PARAMETER_EXPORTED
disconnect
} e opts]} {puts stderr "EXPORT_ERROR: $e";exit 1}
exit
'@.Replace('@RUN@',$run).Replace('@NAME@',$Name).Replace('@PARAM@',$p.status.blob_address).Replace('@WAVE@',$s.memory.status.blob_address).Replace('@RAW@',$p.status.record_address).Replace('@FRAME@',$f.addr)|Set-Content "$run/export-$Name.tcl" -Encoding ascii
 & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat "$run/export-$Name.tcl" *> "$run/export-$Name.log"
 if($LASTEXITCODE -or !(Select-String "$run/export-$Name.log" -Pattern '^PARAMETER_EXPORTED' -Quiet)){throw 'Parameter export failed'}
 foreach($kind in @(@('settings_blob.py','scp','settings'),@('record_blob.py','scw','wave'))){
  & D:/Python/python.exe -X utf8 "$run/$($kind[0])" "$run/$Name.$($kind[1])" --output "$run/$Name-$($kind[2]).json" *> "$run/$Name-$($kind[2])-audit.log"
  if($LASTEXITCODE){throw 'Independent parameter/wave decode failed'}
 }
 $decoded=Get-Content "$run/$Name-settings.json" -Raw|ConvertFrom-Json
 if($decoded.crc32 -ne $p.status.blob_crc){throw 'Parameter full CRC differs from UART'}
 & D:/Python/python.exe -X utf8 "$run/analyze_adc_live_record.py" "$run/$Name.bin" --rate $p.status.rate --crc $p.status.record_crc --output "$run/$Name.json" *> "$run/$Name-raw-audit.log"
 if($LASTEXITCODE){throw 'Parameter raw audit failed'}
 & D:/Python/python.exe -X utf8 "$run/decode_board_frame.py" "$run/$Name-frame.bin" $frameCrc "$run/$Name-frame.png" *> "$run/$Name-frame-audit.log"
 if($LASTEXITCODE){throw 'Parameter frame audit failed'}
 Send-AcqCommands @('resume');$state.resume_needed=$false
 $after=Read-ParameterSnapshot;Assert-ParameterRawHeld $s $after 0
 Assert-MemoryFields $p.status $after.parameter.status @('valid','blob_crc') 'Parameter export slot'
 $entry=@{name=$Name;snapshot=$s;settings_crc=$p.status.blob_crc;settings_sha256=(Get-FileHash "$run/$Name.scp").Hash;wave_crc=$p.status.wave_crc;wave_sha256=(Get-FileHash "$run/$Name.scw").Hash;raw_crc=$p.status.record_crc;raw_sha256=(Get-FileHash "$run/$Name.bin").Hash;frame_crc=$frameCrc;frame_sha256=(Get-FileHash "$run/$Name-frame.bin").Hash}
 $state.parameter_exports+=@($entry);Note-Parameter "$Name-independent-settings-wave-raw-frame" $after;return $entry
}
function Test-ParameterControls {
 $state.parameter_checks=@();$state.parameter_exports=@()
 Send-AcqCommands @('records 0','measurements 0','source 2','trigsource 2','trigger rise 500','triggermode auto','acqrun');[void](Set-LongTimebase 6);$null=Wait-Highspeed50MSingle 6
 $empty=Read-ParameterSnapshot
 Send-AcqCommands @('paramload');$missing=Read-ParameterSnapshot;Assert-ParameterRawHeld $empty $missing 0;Assert-ParameterPreferences $empty $missing
 if($missing.parameter.status.valid -ne '0' -or $missing.parameter.status.notice -ne '1'){throw 'Empty parameter slot was not rejected'}
 $state.parameter_empty=@{before=$empty;after=$missing};Note-Parameter 'parameters-empty-restore-refused' $missing
 Send-AcqCommands @('vertical 1 1 500','vertical 2 3 -1000','cursor 290 730','cursors 1','dual 1','trigger fall 450','triggermode normal','acqrun');$null=Wait-Highspeed50MSingle 6
 # Language and grid are real pointer events. Parameter entry remains below them.
 Tap-Parameter 948 32;Tap-Parameter 512 151;Tap-Parameter 512 209
 $before=Read-ParameterSnapshot;Tap-Parameter 512 257
 $opened=Read-ParameterSnapshot
 if($opened.parameter.status.panel -ne '1'){throw 'Parameter menu pointer failed'}
 Tap-Parameter 512 252;$saved=Read-ParameterSnapshot;Assert-ParameterRawHeld $before $saved 0;Assert-ParameterPreferences $before $saved
 if($saved.parameter.status.valid -ne '1' -or $saved.ui.lang -ne 'en' -or $saved.ui.grid -ne '0' -or $saved.ui.single_complete -ne '1'){throw 'Parameter save did not preserve custom single preferences'}
 $state.parameter_saved=@{before=$before;after=$saved};Note-Parameter 'parameters-pointer-save-preserves-single-and-wave' $saved;$null=Export-Parameter 'param-saved-en'
 Tap-Parameter 720 452;Tap-Parameter 659 491
 Send-AcqCommands @('cursors 0','dual 0','vertical 1 4 -1500','vertical 2 5 2000','trigger rise 500','triggermode auto');[void](Set-LongTimebase 5)
 Tap-Parameter 948 32;Tap-Parameter 512 151;Tap-Parameter 512 209
 $changed=Read-ParameterSnapshot;Tap-Parameter 512 257;Tap-Parameter 512 306;$loaded=Read-ParameterSnapshot
 Assert-ParameterRawHeld $changed $loaded 1;Assert-ParameterPreferences $saved $loaded
 if($loaded.ui.single_armed -ne '0' -or $loaded.ui.single_complete -ne '0' -or $loaded.parameter.status.blob_crc -ne $saved.parameter.status.blob_crc){throw 'Restore did not cancel single or preserve slot'}
 $state.parameter_loaded=@{before=$changed;after=$loaded};Note-Parameter 'parameters-pointer-restore-atomic-and-frozen' $loaded;$null=Export-Parameter 'param-restored-en'
 Tap-Parameter 512 360;$defaults=Read-ParameterSnapshot;Assert-ParameterRawHeld $loaded $defaults 1
 foreach($pair in @(@('time','10'),@('source','ADC1'),@('trig_channel','1'),@('trig_normal','0'),@('trig_rising','1'),@('level_mv','500'),@('ch1_scale','2'),@('ch2_scale','2'),@('ch1_pos','0'),@('ch2_pos','0'),@('grid','1'),@('dual','0'),@('cursors','0'),@('cursor_a','250'),@('cursor_b','750'),@('lang','zh-CN'))){if($defaults.ui[$pair[0]] -ne $pair[1]){throw "Incorrect default $($pair[0])"}}
 if($defaults.parameter.status.blob_crc -ne $saved.parameter.status.blob_crc){throw 'Default cleared parameter slot'}
 $state.parameter_default=@{before=$loaded;after=$defaults};Note-Parameter 'parameters-default-exact-keeps-both-slots' $defaults;$null=Export-Parameter 'param-default-zh'
 Tap-Parameter 720 452;Tap-Parameter 659 491
 Send-AcqCommands @('acqrun');$old=$defaults.ui.display_id
 $fresh=Wait-AcqState {param($x) $x.ui.running -eq '1' -and [uint64]$x.ui.display_id -gt [uint64]$old -and $x.adc.display_epoch -eq $x.ui.epoch -and $x.ui.source -eq 'ADC1'}
 Note-Acq 'parameters-default-resume-new-epoch-empty-CH1' $fresh
 Send-AcqCommands @('acqstop','paramload');$restored=Read-ParameterSnapshot
 Assert-ParameterPreferences $saved $restored
 Send-AcqCommands @('memload');$replay=Read-ParameterSnapshot
 $rejectStart=$captured.Length;Send-AcqCommands @('paramsave','paramload','paramdefault');$rejected=Read-ParameterSnapshot
 $log=$captured.ToString().Substring($rejectStart)
 foreach($command in @('paramsave','paramload','paramdefault')){if($log -notmatch "(?m)^PARAM_COMMAND $command REJECTED\r?$" ){throw 'Replay parameter rejection missing'}}
 Assert-ParameterRawHeld $replay $rejected 0;Assert-ParameterPreferences $replay $rejected
 Assert-MemoryFields $replay.parameter.status $rejected.parameter.status @('valid','blob_crc') 'Replay parameter slot'
 $state.parameter_rejected=@{before=$replay;after=$rejected};Note-Parameter 'parameters-replay-all-actions-refused' $rejected
 Send-AcqCommands @('memlive','acqstop','paramload','triggermode auto','acqrun')
 $running=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.adc.display_epoch -eq $x.ui.epoch -and $x.adc.display_rate -eq '6250000' -and $x.ui.source -eq 'ADC2'}
 $waveBefore=Read-MemoryState;$ack=Invoke-ParameterQueuedStop;$updated=Read-ParameterSnapshot
 Assert-ParameterRunningSave $running $waveBefore $ack $updated
 $state.parameter_running_save=@{before=$running;save_ack=$ack.save_ack;stop_ack=$ack.stop_ack;snapshot=$updated};Note-Parameter 'parameters-running-save-then-stop' $updated;$null=Export-Parameter 'param-updated-running'
 Send-AcqCommands @('parameters 0','source 3');$demo=Read-AcqState;$begin=$captured.Length
 Send-AcqCommands @('paramsave','paramload','paramdefault');$afterDemo=Read-AcqState;$demoP=Read-ParameterState
 if($demo.ui.epoch -ne $afterDemo.ui.epoch -or $demoP.status.blob_crc -ne $updated.parameter.status.blob_crc){throw 'DEMO parameter actions mutated settings slot'}
 $demoLog=$captured.ToString().Substring($begin)
 foreach($command in @('paramsave','paramload','paramdefault')){if($demoLog -notmatch "(?m)^PARAM_COMMAND $command REJECTED\r?$" ){throw 'DEMO parameter rejection missing'}}
 $state.parameter_demo=@{before=$demo;after=$afterDemo;parameter=$demoP};Note-Acq 'parameters-DEMO-actions-refused' $afterDemo
 Send-AcqCommands @('source 2','paramload','triggermode auto','trigger rise 500','dual 1','cursors 0');Tap-Parameter 948 32;Tap-Parameter 512 151;Tap-Parameter 512 209;Tap-Parameter 659 491
 Send-AcqCommands @('acqrun');[void](Set-LongTimebase 6)
 $final=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.ui.triggered -eq '1' -and $x.ui.source -eq 'ADC2' -and $x.ui.time -eq '6' -and $x.adc.display_rate -eq '6250000' -and $x.adc.display_epoch -eq $x.ui.epoch -and $x.ui.lang -eq 'zh-CN'}
 $state.parameter_final=@{ui=$final.ui;adc=$final.adc;parameter=(Read-ParameterState)};Note-Acq 'final-parameter-CH2-10us-live-auto' $final
}
