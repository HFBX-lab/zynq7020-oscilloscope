param([switch]$RedProbe)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. "$root/scripts/acq_ui_session_helpers.ps1"
. "$root/scripts/memory_session_helpers.ps1"
. "$root/scripts/settings_session_helpers.ps1"
$script:checks=0
function Check([bool]$Condition,[string]$Name){$script:checks++;if(!$Condition){throw "FAIL: $Name"}}
function Reject([scriptblock]$Action,[string]$Name){
 $failed=$false;try{& $Action}catch{$failed=$true}
 Check $failed "reject $Name"
}
function Copy-Map($Value){
 if($Value -is [System.Collections.IDictionary]){$out=@{};foreach($key in $Value.Keys){$out[$key]=Copy-Map $Value[$key]};return $out}
 if($Value -is [array]){return ,@($Value|ForEach-Object {Copy-Map $_})}
 return $Value
}
$header='PARAM_STATUS valid=1 replay=0 panel=0 notice=2 blob_address=01a00000 blob_bytes=96 blob_crc=1234abcd epoch=44 running=0 display_id=77 rate=6250000 record_address=13000000 record_bytes=16384 record_crc=deadbeef actual_conversion=50000000 wave_valid=1 wave_crc=89abcdef'
$config='PARAM_CONFIG time_ns=10000 origin_conversion=50000000 selected_channel=1 trigger_channel=1 level_mv=-250 flags=23 ch1_scale=1 ch2_scale=3 ch1_pos=-500 ch2_pos=1500 cursor_a=290 cursor_b=730 language=1'
$end='PARAM_END epoch=44'
$frame="$header`r`n$config`r`n$end`r`n"
$parsed=Convert-ParameterStatusFrame $frame
Check ($parsed.status.epoch -eq '44' -and $parsed.config.level_mv -eq '-250') 'real UART frame and signed preference'
Check ((Convert-ParameterStatusFrame $frame.Replace("`r`n","`n")).status.record_crc -eq 'deadbeef') 'LF accepted'
Reject {Convert-ParameterStatusFrame "$header`n$end`n"} 'missing configuration'
Reject {Convert-ParameterStatusFrame ($frame+$frame)} 'duplicate status frame'
Reject {Convert-ParameterStatusFrame "$config`n$header`n$end`n"} 'configuration before status'
Reject {Convert-ParameterStatusFrame "$header`n$end`n$config`n"} 'end before configuration'
Reject {Convert-ParameterStatusFrame $frame.Replace('PARAM_END epoch=44','PARAM_END epoch=45')} 'mixed end epoch'
foreach($key in @('valid','replay','panel','notice','blob_address','blob_bytes','blob_crc','epoch','running','display_id','rate','record_address','record_bytes','record_crc','actual_conversion','wave_valid','wave_crc')){
 $bad=[regex]::Replace($frame,"(?m)(?<=PARAM_STATUS | )$key=[^\s]+ ?",'',1)
 Reject {Convert-ParameterStatusFrame $bad} "missing status $key"
}
foreach($key in @('time_ns','origin_conversion','selected_channel','trigger_channel','level_mv','flags','ch1_scale','ch2_scale','ch1_pos','ch2_pos','cursor_a','cursor_b','language')){
 $bad=[regex]::Replace($frame,"(?m)(?<=PARAM_CONFIG | )$key=[^\s]+ ?",'',1)
 Reject {Convert-ParameterStatusFrame $bad} "missing configuration $key"
}
foreach($mutation in @(@('blob_bytes=96','blob_bytes=95'),@('valid=1','valid=2'),@('replay=0','replay=3'),@('actual_conversion=50000000','actual_conversion=25000000'),@('blob_address=01a00000','blob_address=0x1a00000'),@('blob_crc=1234abcd','blob_crc=1234xyz0'),@('record_address=13000000','record_address=1300000'),@('record_crc=deadbeef','record_crc=deadbeef0'),@('wave_crc=89abcdef','wave_crc=-1'))){
 Reject {Convert-ParameterStatusFrame $frame.Replace($mutation[0],$mutation[1])} "metadata $($mutation[0])"
}
# Exercise query fencing using the production reader, without a UART device.
$captured=[Text.StringBuilder]::new();[void]$captured.Append($frame)
$script:commands=@()
function Invoke-AcqCommand([string]$Command,[string]$Pattern){
 $script:commands+=@($Command)
 if($Command -eq '__query_boundary__'){$text="COMMANDS: status | tap x y`r`n"}
 elseif($Command -eq 'paramstatus'){$text=$frame}
 else{throw 'Unexpected mock command'}
 Check ([regex]::IsMatch($text,$Pattern)) "query acknowledgement $Command"
 [void]$captured.Append($text);return [regex]::Match($text,$Pattern).Value
}
$fresh=Read-ParameterState
Check ($fresh.status.epoch -eq '44' -and $script:commands.Count -eq 2 -and $script:commands[0] -eq '__query_boundary__') 'reader excludes older complete frame'
function New-Snapshot {
 $ui=@{epoch='44';running='0';display_id='77';seen_id='77';time='6';source='ADC2';ch1_scale='1';ch2_scale='3';ch1_pos='-500';ch2_pos='1500';trig_channel='2';trig_normal='1';trig_rising='1';level_mv='-250';dual='1';grid='0';cursors='1';cursor_a='290';cursor_b='730';lang='en'}
 $view=@{record_address='13000000';record_bytes='16384';record_crc='deadbeef';rate='6250000';conversion='50000000';actual_conversion='50000000';filter='3';display_epoch='43'}
 $wave=@{valid='1';blob_address='01b00000';blob_bytes='16512';blob_crc='89abcdef';saved_id='70';saved_rate='6250000';saved_epoch='40';replay='0'}
 $channel=@{min_v='0.1';max_v='0.9';mean_v='0.5';vpp_v='0.8';rms_total_v='0.57';rms_ac_v='0.28';frequency_hz='50000';period_us='20'}
 return @{ui=$ui;memory=@{view=$view;status=$wave};measurement=@{channels=@((Copy-Map $channel),(Copy-Map $channel))};parameter=(Convert-ParameterStatusFrame $frame)}
}
$script:mockSnapshot=New-Snapshot;$script:mockParameter=$parsed
function Read-MemorySnapshot {return Copy-Map $script:mockSnapshot}
function Read-ParameterState {return Copy-Map $script:mockParameter}
$coherent=Read-ParameterSnapshot
Check ($coherent.parameter.status.record_crc -eq 'deadbeef') 'coherent snapshot accepted'
foreach($key in @('epoch','running','display_id','record_address','record_crc','rate','wave_crc','wave_valid','record_bytes')){
 $script:mockParameter=Copy-Map $parsed;$script:mockParameter.status[$key]='bad'
 Reject {Read-ParameterSnapshot} "mixed snapshot $key"
}
$script:mockParameter=Copy-Map $parsed
$before=New-Snapshot;$after=Copy-Map $before
Assert-ParameterRawHeld $before $after 0;Check $true 'unchanged data and zero epoch accepted'
$after.ui.epoch='45';Assert-ParameterRawHeld $before $after 1;Check $true 'one epoch allowed with old raw epoch retained'
$wrap=Copy-Map $before;$wrap.ui.epoch='4294967295';$after=Copy-Map $wrap;$after.ui.epoch='0'
Assert-ParameterRawHeld $wrap $after 1;Check $true 'epoch wrap accepted'
# Mutation mode demonstrates the negative assertions detect a no-op protection.
if($RedProbe){function Assert-ParameterRawHeld($Before,$After,[uint64]$Advance){}}
foreach($group in @(@('ui',@('display_id','seen_id','running')),@('view',@('record_address','record_crc','rate','conversion','filter','display_epoch')),@('wave',@('valid','blob_crc','saved_id','saved_rate','saved_epoch')))){
 foreach($key in $group[1]){
  $after=Copy-Map $before
  if($group[0] -eq 'ui'){$after.ui[$key]='bad'}elseif($group[0] -eq 'view'){$after.memory.view[$key]='bad'}else{$after.memory.status[$key]='bad'}
  Reject {Assert-ParameterRawHeld $before $after 0} "held $($group[0]).$key"
 }
}
$after=Copy-Map $before;$after.ui.epoch='46';Reject {Assert-ParameterRawHeld $before $after 1} 'wrong epoch advance'
foreach($ch in 0,1){foreach($key in @('min_v','max_v','mean_v','vpp_v','rms_total_v','rms_ac_v','frequency_hz','period_us')){
 $after=Copy-Map $before;$after.measurement.channels[$ch][$key]='bad';Reject {Assert-ParameterRawHeld $before $after 0} "held measurement $ch $key"
}}
Assert-ParameterPreferences $before (Copy-Map $before);Check $true 'identical preferences accepted'
foreach($key in @('time','source','ch1_scale','ch2_scale','ch1_pos','ch2_pos','trig_channel','trig_normal','trig_rising','level_mv','dual','grid','cursors','cursor_a','cursor_b','lang')){
 $after=Copy-Map $before;$after.ui[$key]='bad';Reject {Assert-ParameterPreferences $before $after} "held preference $key"
}
$proof=@{load_segments=@(,@('0x01000000','0x02000000'))}
Assert-MemoryElfRange '01a00000' 96;Check $true 'SCP range inside ELF accepted'
foreach($range in @(@('01a00001',96),@('02000000',96),@('01ffffe0',96),@('fffffff0',96),@('1a00000',96),@('01a00000',0))){Reject {Assert-MemoryElfRange $range[0] $range[1]} 'unsafe parameter blob range'}
# Run only the production pre-export guards. The Tcl body and every hardware
# command are excluded before constructing this scriptblock.
$helper=Get-Content "$root/scripts/settings_session_helpers.ps1" -Raw
$prefix=[regex]::Match($helper,'(?s)function Export-Parameter\(\[string\]\$Name\)\{(.*?)\r?\n @''')
Check $prefix.Success 'isolated export guard prefix found'
Check ($prefix.Groups[1].Value -notmatch 'xsct|Start-Process|mrd -bin') 'export guard prefix contains no hardware operations'
$exportGuard=[scriptblock]::Create('param([string]$Name)'+"`n"+$prefix.Groups[1].Value)
function Read-ParameterSnapshot {return Copy-Map $script:mockSnapshot}
$script:mockSnapshot=New-Snapshot;$state=@{resume_needed=$false}
$script:frameReply="SNAPSHOT_READY addr=10200000 bytes=1843200 format=BGR888 crc=11223344 frames=5`r`n"
function Invoke-AcqCommand([string]$Command,[string]$Pattern){
 Check ($Command -eq 'snapshot') 'pre-export uses snapshot command'
 if(![regex]::IsMatch($script:frameReply,$Pattern)){throw 'Snapshot protocol mismatch'}
 return $script:frameReply
}
& $exportGuard 'param-valid';Check $state.resume_needed '10200000 production frame accepted'
$script:frameReply=$script:frameReply.Replace('10200000','10000000');& $exportGuard 'param-valid';Check $true '10000000 production frame accepted'
foreach($mutation in @(@('addr=10000000','addr=11000000'),@('bytes=1843200','bytes=16384'),@('format=BGR888','format=RGB888'),@('crc=11223344','crc=notacrc'))){
 $good=$script:frameReply;$script:frameReply=$good.Replace($mutation[0],$mutation[1]);Reject {& $exportGuard 'param-valid'} "frame metadata $($mutation[0])";$script:frameReply=$good
}
Reject {& $exportGuard '../escape'} 'export path name'
$script:mockSnapshot.parameter.status.valid='0';Reject {& $exportGuard 'param-valid'} 'missing parameter slot';$script:mockSnapshot=New-Snapshot
$script:mockSnapshot.parameter.status.wave_valid='0';Reject {& $exportGuard 'param-valid'} 'missing waveform slot';$script:mockSnapshot=New-Snapshot
$script:mockSnapshot.parameter.status.blob_address='02000000';Reject {& $exportGuard 'param-valid'} 'SCP beyond ELF';$script:mockSnapshot=New-Snapshot
# The command reader must see two distinct acknowledgements after its fence.
$captured=[Text.StringBuilder]::new();[void]$captured.Append("UI_STATUS os=freertos running=1 epoch=0 display_id=0`r`n")
$script:queuedBody="PARAM_COMMAND paramsave OK`r`nUI_STATUS os=freertos running=1 epoch=44 display_id=77`r`nUI_STATUS os=freertos running=0 epoch=45 display_id=77`r`n"
function Invoke-AcqCommand([string]$Command,[string]$Pattern){
 if($Command -eq '__query_boundary__'){$text="COMMANDS: status | tap x y`r`n"}
 elseif($Command -eq "paramsave`nacqstop"){$text=$script:queuedBody}else{throw 'Unexpected queued mock command'}
 if(![regex]::IsMatch($text,$Pattern)){throw 'Queued acknowledgement mismatch'}
 [void]$captured.Append($text);return [regex]::Match($text,$Pattern).Value
}
$ack=Invoke-ParameterQueuedStop;Check ($ack.save_ack.running -eq '1' -and $ack.stop_ack.epoch -eq '45') 'queued ACK fence and ordering'
$running=New-Snapshot;$running.ui.running='1';$updated=New-Snapshot;$updated.ui.epoch='45';$waveBefore=@{status=@{blob_crc='89abcdef'}}
Assert-ParameterRunningSave $running $waveBefore $ack $updated;Check $true 'running save keeps epoch; stop advances one'
foreach($change in @('save-epoch','stop-epoch','save-running','stop-running','stop-id','updated-id','updated-epoch','wave-crc')){
 $badAck=Copy-Map $ack;$badUpdated=Copy-Map $updated
 switch($change){
  'save-epoch' {$badAck.save_ack.epoch='45'}
  'stop-epoch' {$badAck.stop_ack.epoch='44'}
  'save-running' {$badAck.save_ack.running='0'}
  'stop-running' {$badAck.stop_ack.running='1'}
  'stop-id' {$badAck.stop_ack.display_id='78'}
  'updated-id' {$badUpdated.ui.display_id='78'}
  'updated-epoch' {$badUpdated.ui.epoch='46'}
  'wave-crc' {$badUpdated.parameter.status.wave_crc='00000000'}
 }
 Reject {Assert-ParameterRunningSave $running $waveBefore $badAck $badUpdated} "queued-save $change"
}
$normal=$script:queuedBody
$script:queuedBody=$normal.Replace('PARAM_COMMAND paramsave OK','PARAM_COMMAND paramsave REJECTED');Reject {Invoke-ParameterQueuedStop} 'save rejected'
$script:queuedBody=$normal+"UI_STATUS os=freertos running=0 epoch=45 display_id=77`r`n";Reject {Invoke-ParameterQueuedStop} 'extra stale ACK'
$script:queuedBody=$normal.Replace("UI_STATUS os=freertos running=1 epoch=44 display_id=77`r`n",'');Reject {Invoke-ParameterQueuedStop} 'missing save ACK'
Write-Output "SETTINGS_HELPER_TEST_PASS: $script:checks checks; offline mocks only; no UART/JTAG"
