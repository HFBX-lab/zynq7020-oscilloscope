# Requires the authorized parent runner's owned UART/server. Dot sourcing is offline.
function Convert-MemoryStatusFrame([string]$Body){
 $heads=[regex]::Matches($Body,'(?m)^MEMORY_STATUS [^\r\n]+\r?\n')
 $views=[regex]::Matches($Body,'(?m)^MEMORY_VIEW [^\r\n]+\r?\n')
 $ends=[regex]::Matches($Body,'(?m)^MEMORY_END [^\r\n]+\r?\n')
 if($heads.Count -ne 1 -or $views.Count -ne 1 -or $ends.Count -ne 1){throw 'Incomplete memory status frame'}
 $h=Convert-AcqFields $heads[0].Value;$v=Convert-AcqFields $views[0].Value;$e=Convert-AcqFields $ends[0].Value
 foreach($k in @('valid','replay','panel','notice','blob_address','blob_bytes','blob_crc','saved_id','saved_rate','saved_conversion','saved_filter','saved_epoch','saved_time_ns','saved_flags')){if(!$h.ContainsKey($k)){throw "Missing memory status $k"}}
 foreach($k in @('replay','record_address','record_bytes','record_crc','display_id','seen_id','rate','conversion','actual_conversion','filter','display_epoch','epoch','time','start','crossing','running','single_complete')){if(!$v.ContainsKey($k)){throw "Missing memory view $k"}}
 if($h.replay -ne $v.replay -or $v.display_id -ne $e.display_id -or $heads[0].Index -gt $views[0].Index -or $views[0].Index -gt $ends[0].Index){throw 'Mixed or unordered memory frame'}
 return @{status=$h;view=$v}
}
function Read-MemoryState {
 [void](Invoke-AcqCommand '__query_boundary__' '(?m)^COMMANDS:[^\r\n]+\r?\n')
 $begin=$captured.Length
 [void](Invoke-AcqCommand 'recordstatus' '(?m)^MEMORY_END display_id=\d+\r?\n')
 return Convert-MemoryStatusFrame $captured.ToString().Substring($begin)
}
function Read-MemorySnapshot([bool]$Frozen=$true){
 $s=Read-AcqState;$m=Read-MemoryState;$measurement=Read-MeasurementState
 if($Frozen){
  if($s.ui.running -ne '0' -or $m.view.running -ne '0' -or $measurement.context.running -ne '0' -or $s.adc.pending -ne '0'){throw 'Memory snapshot requires canceled/frozen acquisition'}
  if($s.ui.display_id -ne $m.view.display_id -or $s.adc.display_id -ne $s.ui.display_id -or $measurement.context.display_id -ne $s.ui.display_id -or $m.view.epoch -ne $s.ui.epoch -or $measurement.context.epoch -ne $s.ui.epoch){throw 'Frozen memory snapshot mixed IDs/epochs'}
  if($s.adc.display_address -ne $m.view.record_address -or $s.adc.display_crc -ne $m.view.record_crc -or $s.adc.display_rate -ne $m.view.rate -or $measurement.context.rate -ne $m.view.rate -or $measurement.context.count -ne '4096'){throw 'Frozen memory snapshot mixed pointers/rates/CRC'}
  # Replay owns a separate UI record: the poller's raw last-ID may be newer.
  if($m.status.replay -eq '0' -and $s.adc.id -ne $s.ui.display_id){throw 'Frozen live snapshot does not identify raw last-ID'}
 }
 return @{ui=$s.ui;adc=$s.adc;memory=$m;measurement=$measurement}
}
function Note-Memory([string]$Name,$Snapshot){
 $state.memory_checks+=@{test=$Name;snapshot=$Snapshot}
 [void](Note-Acq $Name $Snapshot)
}
function Assert-MemoryEpoch($Before,$After,[uint64]$Advance){
 if(([uint64]$After.ui.epoch+4294967296-[uint64]$Before.ui.epoch)%4294967296 -ne $Advance){throw 'Memory epoch advance mismatch'}
}
function Assert-MemoryFields($Before,$After,[string[]]$Keys,[string]$Label){
 foreach($k in $Keys){if(!$Before.ContainsKey($k) -or !$After.ContainsKey($k) -or $Before[$k] -ne $After[$k]){throw "$Label changed $k"}}
}
function Assert-MemoryHeld($Before,$After){
 Assert-MemoryFields $Before.ui $After.ui @('epoch','display_id','running','single_armed','single_complete') 'Memory held UI'
 Assert-MemoryFields $Before.memory.view $After.memory.view @('record_address','record_crc','rate','conversion','filter','display_epoch') 'Memory held record'
}
function Assert-MemoryElfRange([string]$Address,[uint64]$Bytes){
 if($Address -notmatch '^[0-9A-Fa-f]{8}$' -or !$Bytes){throw 'Malformed memory export range'}
 $start=[Convert]::ToUInt64($Address,16);$end=$start+$Bytes
 if($start%4 -ne 0 -or $end -gt 4294967296){throw 'Unaligned or overflowing memory export range'}
 foreach($segment in $proof.load_segments){
  $low=[Convert]::ToUInt64(([string]$segment[0]).Replace('0x',''),16)
  $high=[Convert]::ToUInt64(([string]$segment[1]).Replace('0x',''),16)
  if($start -ge $low -and $end -le $high){return}
 }
 throw 'Memory export outside audited ELF load segments'
}
function Assert-MemoryExportMetadata($Snapshot){
 $m=$Snapshot.memory
 if($m.status.valid -ne '1' -or $m.status.blob_bytes -ne '16512' -or $m.view.record_bytes -ne '16384'){throw 'Unexpected memory blob/raw size or absent slot'}
 foreach($crc in @($m.status.blob_crc,$m.view.record_crc)){if($crc -notmatch '^[0-9A-Fa-f]{8}$'){throw 'Malformed memory CRC'}}
 if($m.view.rate -notin @('50000000','6250000','625000','62500','6250','625') -or $m.view.actual_conversion -ne '50000000'){throw 'Unexpected memory rate/conversion'}
 Assert-MemoryElfRange $m.status.blob_address 16512
 if($m.status.replay -eq '1'){Assert-MemoryElfRange $m.view.record_address 16384}
 elseif($m.status.replay -eq '0'){if($m.view.record_address -notmatch '^1300[04]000$'){throw 'Live memory export outside capture buffers'}}
 else{throw 'Invalid replay flag'}
}
function Export-Memory([string]$Name,[bool]$Frame=$true,[string]$ProtectedAddress=''){
 if($Name -notmatch '^memory-[a-z0-9-]+$'){throw 'Unsafe memory export name'}
 $before=Read-MemorySnapshot;Assert-MemoryExportMetadata $before
 $m=$before.memory;$frameRead='';$frameCrc='';$protectedRead=''
 if($ProtectedAddress){
  if($ProtectedAddress -notmatch '^1300[04]000$' -or $m.status.replay -ne '1'){throw 'Invalid protected live buffer'}
  $protectedRead="mrd -bin -file {$run/memory-replay-live-held.bin} 0x$ProtectedAddress 4096"
 }
 if($Frame){
  $reply=Invoke-AcqCommand 'snapshot' '(?m)^SNAPSHOT_READY[^\r\n]+\r?\n';$state.resume_needed=$true;$f=Convert-AcqFields $reply
  if($f.addr -notmatch '^10[02]00000$' -or $f.crc -notmatch '^[0-9A-Fa-f]{8}$' -or $f.bytes -ne '1843200' -or $f.format -ne 'BGR888'){throw 'Invalid memory frame metadata'}
  $frameCrc=$f.crc;$frameRead="mrd -bin -file {$run/$Name-frame.bin} 0x$($f.addr) 460800"
 }
 @'
if {[catch {
connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "APU"}
configparams force-mem-accesses 1
mrd -bin -file {@RUN@/@NAME@.scw} 0x@BLOB@ 4128
mrd -bin -file {@RUN@/@NAME@.bin} 0x@RECORD@ 4096
@PROTECTED@
@FRAME@
puts MEMORY_RECORD_EXPORTED
disconnect
} e opts]} {puts stderr "EXPORT_ERROR: $e";exit 1}
exit
'@.Replace('@RUN@',$run).Replace('@NAME@',$Name).Replace('@BLOB@',$m.status.blob_address).Replace('@RECORD@',$m.view.record_address).Replace('@PROTECTED@',$protectedRead).Replace('@FRAME@',$frameRead) | Set-Content "$run/export-$Name.tcl" -Encoding ascii
 & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat "$run/export-$Name.tcl" *> "$run/export-$Name.log"
 if($LASTEXITCODE -or !(Select-String "$run/export-$Name.log" -Pattern '^MEMORY_RECORD_EXPORTED' -Quiet)){throw 'Memory DDR export failed'}
 & D:/Python/python.exe -X utf8 "$run/record_blob.py" "$run/$Name.scw" --output "$run/$Name-blob.json" *> "$run/$Name-blob-audit.log"
 if($LASTEXITCODE){throw 'Independent SCW1 validation failed'}
 $blob=Get-Content "$run/$Name-blob.json" -Raw|ConvertFrom-Json
 if($blob.crc32 -ne $m.status.blob_crc -or $blob.sha256 -ne (Get-FileHash "$run/$Name.scw").Hash){throw 'Exported memory blob does not match UART snapshot'}
 & D:/Python/python.exe -X utf8 "$run/analyze_adc_live_record.py" "$run/$Name.bin" --rate $m.view.rate --crc $m.view.record_crc --output "$run/$Name.json" *> "$run/$Name-audit.log"
 if($LASTEXITCODE){throw 'Independent memory raw validation failed'}
 $entry=@{name=$Name;ui=$before.ui;adc=$before.adc;memory=$m;measurement=$before.measurement;rate=[int]$m.view.rate;id=$before.ui.display_id;blob_crc=$m.status.blob_crc;blob_sha256=(Get-FileHash "$run/$Name.scw").Hash;record_crc=$m.view.record_crc;record_sha256=(Get-FileHash "$run/$Name.bin").Hash}
 if($ProtectedAddress){
  $expected=$state.memory_sources|Where-Object name -eq 'memory-live-before'
  if(!$expected -or (Get-FileHash "$run/memory-replay-live-held.bin").Hash -ne $expected.sha256){throw 'Replay overwrote protected live DDR buffer'}
  $entry.protected_address=$ProtectedAddress;$entry.protected_crc=$expected.crc32;$entry.protected_sha256=(Get-FileHash "$run/memory-replay-live-held.bin").Hash
 }
 if($Frame){
  & D:/Python/python.exe -X utf8 "$run/decode_board_frame.py" "$run/$Name-frame.bin" $frameCrc "$run/$Name-frame.png" *> "$run/$Name-frame-audit.log"
  if($LASTEXITCODE){throw 'Independent memory frame validation failed'}
  $entry.frame_crc=$frameCrc;$entry.frame_sha256=(Get-FileHash "$run/$Name-frame.bin").Hash
  Send-AcqCommands @('resume');$state.resume_needed=$false
 }
 $after=Read-MemorySnapshot;Assert-MemoryHeld $before $after
 Assert-MemoryFields $before.memory.status $after.memory.status @('blob_address','blob_crc','saved_id','saved_rate') 'Memory export slot'
 $state.memory_exports+=@($entry);Note-Memory "$Name-independent-blob-raw-frame" $after
 return $entry
}
function Export-MemorySource([string]$Name){
 $prior=$state.long_records;$state.long_records=@()
 try{[void](Export-LongRecord $Name);$entry=$state.long_records[-1];$state.memory_sources+=@($entry);return $entry}
 finally{$state.long_records=$prior}
}
function Invoke-MemoryQueuedStop([string]$Command){
 if($Command -notin @('memlive','memsave')){throw 'Unsupported memory queued transaction'}
 [void](Invoke-AcqCommand '__query_boundary__' '(?m)^COMMANDS:[^\r\n]+\r?\n')
 $start=$captured.Length
 [void](Invoke-AcqCommand ($Command+"`nacqstop") '(?m)^UI_STATUS [^\r\n]+ running=0 [^\r\n]+\r?\n')
 $body=$captured.ToString().Substring($start)
 $ui=[regex]::Matches($body,'(?m)^UI_STATUS [^\r\n]+\r?\n')
 if($body -notmatch "(?m)^MEMORY_COMMAND $Command OK\r?$" -or $ui.Count -ne 2){throw 'Incomplete queued memory/stop acknowledgements'}
 $first=Convert-AcqFields $ui[0].Value;$last=Convert-AcqFields $ui[1].Value
 if($first.running -ne '1' -or $last.running -ne '0'){throw 'Queued memory action was not observed before stop'}
 return @{save_ack=$first;stop_ack=$last}
}
function Assert-MemoryRunningSave($Running,$Ack,$Snapshot){
 $saveEpoch=[uint64]$Ack.save_ack.epoch;$stopEpoch=($saveEpoch+1)%4294967296
 if($Ack.save_ack.running -ne '1' -or $Ack.stop_ack.running -ne '0' -or $Snapshot.ui.running -ne '0' -or
    $saveEpoch -ne [uint64]$Running.ui.epoch -or [uint64]$Ack.stop_ack.epoch -ne $stopEpoch -or [uint64]$Snapshot.ui.epoch -ne $stopEpoch){throw 'Running save or queued stop epoch contract changed'}
 if($Ack.save_ack.display_id -ne $Ack.stop_ack.display_id -or $Ack.save_ack.display_id -ne $Snapshot.ui.display_id -or
    $Snapshot.memory.status.saved_id -ne $Snapshot.ui.display_id -or $Snapshot.memory.status.saved_rate -ne $Snapshot.memory.view.rate -or
    $Snapshot.memory.view.record_crc -ne $Snapshot.adc.display_crc){throw 'Running save did not bind frozen current record/cache'}
}
function Test-MemoryControls {
 $state.memory_checks=@();$state.memory_sources=@();$state.memory_exports=@()
 Send-AcqCommands @('records 0','measurements 0','source 2','trigsource 2','trigger rise 500','triggermode auto','dual 1','cursors 1','cursor 250 650','vertical 2 2 -500','acqrun')
 [void](Set-LongTimebase 6);$null=Wait-Highspeed50MSingle 6
 $source=Export-MemorySource 'memory-source';$before=Read-MemorySnapshot
 Send-AcqCommands @('tap 252 32');$null=Wait-AcqState {param($x) $x.ui.touch_pending -eq '0'}
 $menu=Read-MemoryState;if($menu.status.panel -ne '1'){throw 'Memory pointer did not open menu'}
 Send-AcqCommands @('tap 512 244');$null=Wait-AcqState {param($x) $x.ui.touch_pending -eq '0'}
 $saved=Read-MemorySnapshot;Assert-MemoryHeld $before $saved
 if($saved.memory.status.valid -ne '1' -or $saved.memory.status.saved_id -ne $source.id -or $saved.memory.status.saved_rate -ne '6250000'){throw 'Memory pointer save did not bind source'}
 $state.memory_saved_snapshot=$saved;Note-Memory 'memory-pointer-save-preserves-single' $saved
 $null=Export-Memory 'memory-saved'
 Send-AcqCommands @('records 0','cursors 0','source 2','acqrun');[void](Set-LongTimebase 5);$null=Wait-Highspeed50MSingle 5
 Send-AcqCommands @('vertical 2 3 1000')
 $live=Export-MemorySource 'memory-live-before';$pre=Read-MemorySnapshot;$state.memory_before_replay=$pre
 if($pre.ui.display_id -eq $saved.ui.display_id){throw 'Live before replay did not advance ID'}
 Send-AcqCommands @('records 1','tap 512 298')
 $null=Wait-AcqState {param($x) $x.ui.running -eq '0' -and $x.ui.time -eq '6' -and $x.ui.display_id -eq $saved.ui.display_id -and $x.adc.display_rate -eq '6250000'}
 Send-AcqCommands @('records 0');$replay=Read-MemorySnapshot
 Assert-MemoryEpoch $pre $replay 1
 if($replay.memory.status.replay -ne '1' -or $replay.memory.view.actual_conversion -ne '50000000'){throw 'Replay did not own frozen record'}
 $null=Export-Memory 'memory-replay' $true $pre.memory.view.record_address
 Send-AcqCommands @('source 1');$empty=Read-MemorySnapshot
 if($empty.measurement.channels[0].frequency_valid -ne '0' -or $empty.measurement.channels[0].period_us -ne '0.000000'){throw 'Replay empty CH1 invented frequency/period'}
 Note-Memory 'memory-replay-CH1-has-no-period' $empty
 Send-AcqCommands @('source 2','cursors 1','cursor 350 750','vertical 2 3 500')
 $waitBefore=Read-MemorySnapshot;$replayWait=[Diagnostics.Stopwatch]::StartNew()
 Start-Sleep -Milliseconds 1100;$waitAfter=Read-MemorySnapshot;$replayWait.Stop()
 Assert-MemoryHeld $waitBefore $waitAfter
 Assert-MemoryFields $waitBefore.adc $waitAfter.adc @('id','pending') 'Replay poller'
 Assert-MemoryFields $waitBefore.memory.status $waitAfter.memory.status @('blob_crc','saved_id') 'Replay slot'
 if($waitAfter.adc.pending -ne '0' -or [uint64]$waitAfter.ui.seen_id -lt [uint64]$waitBefore.ui.seen_id){throw 'Replay acquired or regressed seen ID'}
 if($replayWait.Elapsed.TotalMilliseconds -lt 1000){throw 'Replay stability observation shorter than one second'}
 $state.memory_replay_wait=@{before=$waitBefore;after=$waitAfter;elapsed_ms=$replayWait.Elapsed.TotalMilliseconds};Note-Memory 'memory-replay-poller-stays-canceled' $waitAfter
 $null=Export-Memory 'memory-replay-cursor'
 $rejectStart=$captured.Length
 Send-AcqCommands @('memsave','single','autoset','trigger fall 4500','trigsource 1','triggermode normal')
 $rejectLog=$captured.ToString().Substring($rejectStart)
 $rejectMarkers=@('MEMORY_COMMAND memsave REJECTED','SINGLE_COMMAND REJECTED','AUTOSET_COMMAND REJECTED','TRIGGER_REJECTED','TRIGGER_SOURCE_REJECTED','TRIGGERMODE_REJECTED')
 foreach($marker in $rejectMarkers){if($rejectLog -notmatch ('(?m)^'+[regex]::Escape($marker)+'\r?$')){throw "Replay did not report rejection: $marker"}}
 $rejected=Read-MemorySnapshot;Assert-MemoryHeld $waitAfter $rejected
 Assert-MemoryFields $waitAfter.ui $rejected.ui @('trig_channel','trig_rising','trig_normal','level_mv') 'Replay rejected trigger'
 Assert-MemoryFields $waitAfter.memory.status $rejected.memory.status @('blob_crc','saved_id','saved_rate','saved_epoch') 'Replay rejected save'
 $state.memory_replay_rejections=@{before=$waitAfter;after=$rejected;markers=$rejectMarkers}
 Note-Memory 'memory-replay-rejects-single-autoset-trigger' $rejected
 $ack=Invoke-MemoryQueuedStop 'memlive';$returned=Read-MemorySnapshot
 Assert-MemoryEpoch $rejected $returned 2
 Assert-MemoryFields $pre.ui $returned.ui @('time','source','scale','ch1_scale','ch2_scale','ch1_pos','ch2_pos','dual','cursors','cursor_a','cursor_b','trig_channel','trig_normal','trig_rising','level_mv','grid','display_id') 'Return live settings'
 Assert-MemoryFields $pre.memory.view $returned.memory.view @('record_address','record_crc','rate','conversion','filter','display_epoch') 'Return live record'
 for($c=0;$c -lt 2;$c++){Assert-MemoryFields $pre.measurement.channels[$c] $returned.measurement.channels[$c] @('valid','min_v','max_v','mean_v','vpp_v','rms_total_v','rms_ac_v','frequency_valid','frequency_hz','period_us') 'Return live cache'}
 if($returned.memory.status.replay -ne '0' -or $returned.ui.running -ne '0'){throw 'Return live did not restore stopped live ownership'}
 $state.memory_return=@{before=$pre;after=$returned;start_ack=$ack.save_ack;stop_ack=$ack.stop_ack};Note-Memory 'memory-return-restores-live-before-poller' $returned
 Send-AcqCommands @('records 0','acqrun');$old=$returned.ui.display_id
 $null=Wait-AcqState {param($x) [uint64]$x.ui.display_id -gt [uint64]$old -and $x.adc.display_epoch -eq $x.ui.epoch}
 $null=Wait-Highspeed50MSingle 5;$new=Export-MemorySource 'memory-new-live'
 Send-AcqCommands @('vertical 2 4 1500','trigger fall 450','triggermode normal','cursors 0','dual 0')
 $restoreBefore=Read-MemorySnapshot
 Send-AcqCommands @('records 1','tap 512 352');$null=Wait-AcqState {param($x) $x.ui.time -eq '6' -and $x.ui.touch_pending -eq '0'}
 Send-AcqCommands @('records 0');$restoreAfter=Read-MemorySnapshot
 Assert-MemoryEpoch $restoreBefore $restoreAfter 1
 Assert-MemoryFields $saved.ui $restoreAfter.ui @('time','source','scale','ch1_scale','ch2_scale','ch1_pos','ch2_pos','dual','cursors','cursor_a','cursor_b','trig_channel','trig_normal','trig_rising','level_mv','grid') 'Stored settings restore'
 Assert-MemoryFields $restoreBefore.memory.view $restoreAfter.memory.view @('record_address','record_crc','rate','conversion','filter','display_id','display_epoch') 'Settings restore raw'
 if($restoreAfter.ui.running -ne '0' -or $restoreAfter.memory.view.rate -ne '50000000'){throw 'Settings restore changed pause or relabeled current rate'}
 $state.memory_settings_restore=@{before=$restoreBefore;after=$restoreAfter};Note-Memory 'memory-settings-restore-keeps-current-raw-rate' $restoreAfter
 Send-AcqCommands @('acqrun');[void](Set-LongTimebase 5)
 $running=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.ui.time -eq '5' -and $x.adc.display_rate -eq '50000000' -and $x.adc.display_epoch -eq $x.ui.epoch -and [uint64]$x.ui.display_id -gt [uint64]$new.id}
 $update=Invoke-MemoryQueuedStop 'memsave';$updated=Read-MemorySnapshot
 Assert-MemoryRunningSave $running $update $updated
 if($updated.memory.status.saved_id -ne $updated.ui.display_id -or $updated.memory.status.saved_rate -ne '50000000' -or $updated.memory.status.saved_id -eq $saved.ui.display_id){throw 'Running save did not explicitly update slot'}
 $state.memory_live_update=@{running_before=$running;save_ack=$update.save_ack;stop_ack=$update.stop_ack;snapshot=$updated};Note-Memory 'memory-running-save-does-not-restart' $updated
 $null=Export-Memory 'memory-updated-live'
 Send-AcqCommands @('records 0','cursors 0','source 2','trigsource 2','trigger rise 500','triggermode auto','dual 1','acqrun');[void](Set-LongTimebase 6)
 $s=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.ui.time -eq '6' -and $x.ui.source -eq 'ADC2' -and $x.ui.triggered -eq '1' -and $x.adc.display_rate -eq '6250000' -and $x.adc.display_epoch -eq $x.ui.epoch}
 $state.memory_final=Read-MemoryState;if($state.memory_final.status.replay -ne '0'){throw 'Memory tests left replay active'}
 Note-Acq 'final-memory-CH2-10us-live-auto-running' $s
}
