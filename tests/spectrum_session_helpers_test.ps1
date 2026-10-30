param([switch]$RedProbe)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. "$root/scripts/acq_ui_session_helpers.ps1"
. "$root/scripts/memory_session_helpers.ps1"
. "$root/scripts/settings_session_helpers.ps1"
. "$root/scripts/spectrum_session_helpers.ps1"
$script:checks=0
function Check([bool]$Condition,[string]$Name){$script:checks++;if(!$Condition){throw "FAIL: $Name"}}
function Reject([scriptblock]$Action,[string]$Name){$failed=$false;try{& $Action}catch{$failed=$true};Check $failed "reject $Name"}
function Copy-Map($Value){
 if($Value -is [System.Collections.IDictionary]){$out=@{};foreach($key in $Value.Keys){$out[$key]=Copy-Map $Value[$key]};return $out}
 if($Value -is [array]){return ,@($Value|ForEach-Object {Copy-Map $_})};return $Value
}
$header='FFT_STATUS valid=1 panel=1 seq=9 channel=1 window=1 remove_mean=1 auto_range=1 end_bin=256 count=4096 rate=6250000 id=77 source_epoch=43 conversion=50000000 actual_conversion=50000000 filter=3 raw_address=002d0000 raw_bytes=16384 raw_crc=deadbeef bins_address=002e0000 bins_bytes=8196 bins_crc=abcdef12 plot_address=002f0000 plot_bytes=2880 plot_crc=01234567 elapsed_ms=8'
$values='FFT_VALUES bin_hz=1525.878906250 mean_v=0.500000000 gain=0.500000000 peak_valid=1 peak_bin=32.768000000 peak_hz=50000.000000000 peak_vrms=0.350000000 peak_dbv=-9.118639 overrange=0 ui_epoch=44 running=0 display_id=77 live_rate=6250000 replay=0 wave_crc=89abcdef param_crc=1234abcd'
$end='FFT_END seq=9'
$frame="$header`r`n$values`r`n$end`r`n"
$parsed=Convert-SpectrumStatusFrame $frame
Check ($parsed.status.seq -eq '9' -and $parsed.values.peak_dbv -eq '-9.118639') 'complete current production diagnostic'
Check ((Convert-SpectrumStatusFrame $frame.Replace("`r`n","`n")).status.raw_crc -eq 'deadbeef') 'LF framing'
Reject {Convert-SpectrumStatusFrame "$header`n$end`n"} 'missing values'
Reject {Convert-SpectrumStatusFrame "$values`n$end`n"} 'missing status'
Reject {Convert-SpectrumStatusFrame "$header`n$values`n"} 'missing end'
Reject {Convert-SpectrumStatusFrame ($frame+$frame)} 'duplicate frames'
Reject {Convert-SpectrumStatusFrame "$values`n$header`n$end`n"} 'values before status'
Reject {Convert-SpectrumStatusFrame "$header`n$end`n$values`n"} 'end before values'
Reject {Convert-SpectrumStatusFrame $frame.Replace('FFT_END seq=9','FFT_END seq=10')} 'mixed sequence'
foreach($key in $parsed.status.Keys){
 $badHeader=[regex]::Replace($header,"(?<= |^)$key=[^\s]+ ?",'')
 Reject {Convert-SpectrumStatusFrame "$badHeader`n$values`n$end`n"} "missing status $key"
}
foreach($key in $parsed.values.Keys){
 $badValues=[regex]::Replace($values,"(?<= |^)$key=[^\s]+ ?",'')
 Reject {Convert-SpectrumStatusFrame "$header`n$badValues`n$end`n"} "missing values $key"
}
foreach($key in @('valid','panel','channel','window','remove_mean','auto_range')){
 $badHeader=[regex]::Replace($header,"(?<= |^)$key=[^\s]+",$key+'=2')
 Reject {Convert-SpectrumStatusFrame "$badHeader`n$values`n$end`n"} "invalid status flag $key"
}
foreach($key in @('peak_valid','overrange','running','replay')){
 $badValues=[regex]::Replace($values,"(?<= |^)$key=[^\s]+",$key+'=2')
 Reject {Convert-SpectrumStatusFrame "$header`n$badValues`n$end`n"} "invalid value flag $key"
}
foreach($mutation in @(@('raw_bytes=16384','raw_bytes=16380'),@('bins_bytes=8196','bins_bytes=8192'),@('plot_bytes=2880','plot_bytes=2876'),@('actual_conversion=50000000','actual_conversion=25000000'),@('count=4096','count=4095'),@('rate=6250000','rate=0'),@('end_bin=256','end_bin=15'),@('end_bin=256','end_bin=2049'),@('raw_address=002d0000','raw_address=0x2d0000'),@('bins_address=002e0000','bins_address=002e000'),@('plot_address=002f0000','plot_address=002f00000'),@('raw_crc=deadbeef','raw_crc=deadbeeg'),@('bins_crc=abcdef12','bins_crc=abcdef1'),@('plot_crc=01234567','plot_crc=-1'),@('wave_crc=89abcdef','wave_crc=89abcdeg'),@('param_crc=1234abcd','param_crc=1234abc'))){
 Reject {Convert-SpectrumStatusFrame $frame.Replace($mutation[0],$mutation[1])} "metadata $($mutation[0])"
}
foreach($key in @('bin_hz','mean_v','gain','peak_bin','peak_hz','peak_vrms','peak_dbv')){
 foreach($bad in @('NaN','Infinity','not-a-number')){
  $badValues=[regex]::Replace($values,"(?<= |^)$key=[^\s]+",$key+'='+$bad)
  Reject {Convert-SpectrumStatusFrame "$header`n$badValues`n$end`n"} "nonfinite/malformed $key $bad"
 }
}
# No result has been published yet: addresses/sizes still exist but dimensions
# and bin values legitimately start at zero. Do not mistake this for corruption.
$unready=$frame.Replace('valid=1 panel','valid=0 panel').Replace('seq=9','seq=0').Replace('count=4096','count=0').Replace('rate=6250000 id=77','rate=0 id=0').Replace('end_bin=256','end_bin=0')
Check ((Convert-SpectrumStatusFrame $unready).status.valid -eq '0') 'valid0 initial cache accepted'
$meanOff=$frame.Replace('remove_mean=1','remove_mean=0').Replace('peak_valid=1','peak_valid=0').Replace('peak_bin=32.768000000','peak_bin=0.000000000').Replace('peak_hz=50000.000000000','peak_hz=0.000000000').Replace('peak_vrms=0.350000000','peak_vrms=0.000000000').Replace('peak_dbv=-9.118639','peak_dbv=-120.000000')
Check ((Convert-SpectrumStatusFrame $meanOff).values.peak_valid -eq '0') 'strong retained DC may have no AC peak'

# Exercise the real query fence with old complete frames already in the stream.
$captured=[Text.StringBuilder]::new();[void]$captured.Append($frame)
$script:commands=@()
function Invoke-AcqCommand([string]$Command,[string]$Pattern){
 $script:commands+=@($Command)
 if($Command -eq '__query_boundary__'){$text="COMMANDS: status | tap x y`r`n"}
 elseif($Command -eq 'fftstatus'){$text=$frame.Replace('seq=9','seq=10')}
 else{throw 'Unexpected offline mock query'}
 Check ([regex]::IsMatch($text,$Pattern)) "query acknowledgement $Command"
 [void]$captured.Append($text);return [regex]::Match($text,$Pattern).Value
}
$fresh=Read-SpectrumState
Check ($fresh.status.seq -eq '10' -and $script:commands.Count -eq 2 -and $script:commands[0] -eq '__query_boundary__') 'reader excludes prior seq9 and accepts seq10'
Check ([uint64]$fresh.status.seq -gt [uint64]$parsed.status.seq) 'sequence numeric 9 to10'
$body=(Get-Command Test-SpectrumControls).ScriptBlock.ToString()
$predicate=[regex]::Match($body,'(?m)^\s*if\(\$fresh\.status\.rate[^\r\n]+').Value
Check (![string]::IsNullOrEmpty($predicate)) 'production running-update predicate present'
$liveCheck=[scriptblock]::Create($predicate)
$fresh=Copy-Map $parsed;$fresh.status.rate='50000000';$fresh.status.conversion='50000000';$fresh.status.seq='10';$fresh.values.running='1'
$held=Copy-Map $parsed;$running=@{ui=@{epoch='60'}};$epoch='60'
& $liveCheck;Check $true 'production predicate accepts numeric9 to10'
$fresh.status.seq='9';Reject {& $liveCheck} 'unchanged running sequence'
$fresh.status.seq='8';Reject {& $liveCheck} 'regressed running sequence'
$fresh.status.seq='10';$stringMutant=[scriptblock]::Create($predicate.Replace('[uint64]',''))
Reject {& $stringMutant} 'string sequence mutant rejects valid9 to10'

function New-Snapshot {
 $ui=@{epoch='44';running='0';display_id='77';seen_id='77';time='6';source='ADC2';ch1_scale='1';ch2_scale='3';ch1_pos='500';ch2_pos='-1000';trig_channel='2';trig_normal='0';trig_rising='1';level_mv='500';dual='1';grid='1';cursors='0';cursor_a='290';cursor_b='730';lang='zh-CN'}
 $view=@{record_address='13000000';record_bytes='16384';record_crc='deadbeef';rate='6250000';conversion='50000000';filter='3';display_epoch='43'}
 $wave=@{valid='1';blob_crc='89abcdef';saved_id='70';saved_rate='50000000';saved_epoch='40';replay='0'}
 $channel=@{min_v='0.1';max_v='0.9';mean_v='0.5';vpp_v='0.8';rms_total_v='0.57';rms_ac_v='0.28';frequency_hz='50000';period_us='20'}
 return @{ui=$ui;memory=@{view=$view;status=$wave};measurement=@{channels=@((Copy-Map $channel),(Copy-Map $channel))};parameter=@{status=@{valid='1';blob_crc='1234abcd';wave_valid='1';wave_crc='89abcdef';rate='6250000'}}}
}
$script:mockSnapshot=New-Snapshot;$script:mockSpectrum=Copy-Map $parsed
function Read-ParameterSnapshot {return Copy-Map $script:mockSnapshot}
function Read-SpectrumState {return Copy-Map $script:mockSpectrum}
$s=Read-SpectrumSnapshot;Check ($s.fft.status.id -eq '77') 'coherent frozen context'
foreach($key in @('running','display_id','ui_epoch','wave_crc','param_crc','live_rate','replay')){
 $script:mockSpectrum=Copy-Map $parsed;$script:mockSpectrum.values[$key]='bad'
 Reject {Read-SpectrumSnapshot} "mixed current FFT context $key"
}
# Cached result rate and ID can be older than the current live acquisition.
$script:mockSnapshot=New-Snapshot;$script:mockSnapshot.parameter.status.rate='50000000';$script:mockSnapshot.memory.view.rate='50000000';$script:mockSnapshot.ui.display_id='99'
$script:mockSpectrum=Copy-Map $parsed;$script:mockSpectrum.values.display_id='99';$script:mockSpectrum.values.live_rate='50000000'
$s=Read-SpectrumSnapshot
Check ($s.fft.status.rate -eq '6250000' -and $s.fft.status.id -eq '77' -and $s.fft.values.live_rate -eq '50000000') 'old6m25 FFT cache with current50m record accepted'
$script:mockSnapshot.memory.status.replay='1';$script:mockSpectrum.values.replay='1'
Check ((Read-SpectrumSnapshot).fft.values.replay -eq '1') 'replay context accepted independently from cache source'
# Exercise the actual end-to-end replay ownership checks: the stored SCW uses
# 50M while the record immediately before entering replay uses 6.25M.
$replayPredicate=[regex]::Match($body,'(?m)^\s*if\(\$replay\.fft\.status\.rate -ne[^\r\n]+').Value
$differentRatePredicate=[regex]::Match($body,'(?m)^\s*if\(\$replay\.fft\.status\.rate -eq[^\r\n]+').Value
Check (![string]::IsNullOrEmpty($replayPredicate) -and ![string]::IsNullOrEmpty($differentRatePredicate)) 'production replay predicates present'
$replayCheck=[scriptblock]::Create($replayPredicate);$differentRateCheck=[scriptblock]::Create($differentRatePredicate)
$replay=New-Snapshot;$replay.fft=Copy-Map $parsed;$replay.memory.view.rate='50000000';$replay.fft.status.rate='50000000'
$state=@{spectrum_before_replay=@{memory=@{view=@{rate='6250000'}}}}
& $replayCheck;& $differentRateCheck;Check $true 'production replay uses stored50M instead of prelive6m25'
foreach($key in @('rate','id','source_epoch','actual_conversion')){
 $old=$replay.fft.status[$key];$replay.fft.status[$key]='wrong';Reject {& $replayCheck} "replay ownership $key";$replay.fft.status[$key]=$old
}
$state.spectrum_before_replay.memory.view.rate='50000000';Reject {& $differentRateCheck} 'replay fixture must actually cross rates'

$before=New-Snapshot;$after=Copy-Map $before
Assert-SpectrumDataHeld $before $after;Check $true 'identical frozen acquisition accepted'
if($RedProbe){function Assert-SpectrumDataHeld($Before,$After){}}
foreach($group in @(@('ui',@('epoch','running','display_id','seen_id','time','source','ch1_scale','ch2_scale','ch1_pos','ch2_pos','trig_channel','trig_normal','trig_rising','level_mv','dual','grid','cursors','cursor_a','cursor_b','lang')),@('view',@('record_address','record_crc','rate','conversion','filter','display_epoch')),@('wave',@('valid','blob_crc','saved_id','saved_rate','saved_epoch')),@('parameter',@('valid','blob_crc','wave_valid','wave_crc')))){
 foreach($key in $group[1]){
  $after=Copy-Map $before
  switch($group[0]){'ui'{$after.ui[$key]='changed'};'view'{$after.memory.view[$key]='changed'};'wave'{$after.memory.status[$key]='changed'};'parameter'{$after.parameter.status[$key]='changed'}}
  Reject {Assert-SpectrumDataHeld $before $after} "held $($group[0]).$key"
 }
}
for($channel=0;$channel -lt 2;$channel++){
 foreach($key in $before.measurement.channels[$channel].Keys){$after=Copy-Map $before;$after.measurement.channels[$channel][$key]='changed';Reject {Assert-SpectrumDataHeld $before $after} "measurement$channel.$key"}
}
$changed=Copy-Map $parsed;$changed.values.running='1';$changed.values.ui_epoch='60';$changed.values.display_id='100';$changed.values.live_rate='50000000';$changed.status.panel='0';$changed.status.auto_range='0';$changed.status.end_bin='2048';$changed.status.plot_crc='11111111'
Assert-SpectrumCacheHeld $parsed $changed;Check $true 'live metadata and range may change without recomputing cache'
foreach($group in @(@('status',@('valid','seq','channel','window','remove_mean','count','rate','id','source_epoch','conversion','filter','raw_address','raw_crc','bins_address','bins_crc')),@('values',@('bin_hz','mean_v','gain','peak_valid','peak_bin','peak_hz','peak_vrms','peak_dbv','overrange','wave_crc','param_crc')))){
 foreach($key in $group[1]){$changed=Copy-Map $parsed;$changed[$group[0]][$key]='changed';Reject {Assert-SpectrumCacheHeld $parsed $changed} "cached $($group[0]).$key"}
}

# Run only the prefix before the production export Tcl: neither XSCT nor a
# device is contacted. This covers exact audited ranges and real frame fields.
$export=(Get-Command Export-Spectrum).ScriptBlock.ToString();$prefix=$export.Substring(0,$export.IndexOf("@'"));$prefixBlock=[scriptblock]::Create($prefix)
$script:mockSpectrum=Copy-Map $parsed;$proof=@{load_segments=@(,@('0x00200000','0x00310000'))};$state=@{}
$Name='fft-unit';$script:frameReply='SNAPSHOT_READY addr=10000000 bytes=1843200 format=BGR888 crc=01234567 frames=1'
function Invoke-AcqCommand([string]$Command,[string]$Pattern){if($Command -ne 'snapshot'){throw 'Unexpected command in export prefix'};if(![regex]::IsMatch($script:frameReply+"`n",$Pattern)){throw 'Frame pattern mismatch'};return $script:frameReply}
& $prefixBlock 'fft-unit';Check $true 'three spectrum buffers and primary frame are within allowed ranges'
$script:frameReply=$script:frameReply.Replace('addr=10000000','addr=10200000');& $prefixBlock 'fft-unit';Check $true 'secondary actual frame buffer accepted'
foreach($key in @('raw_address','bins_address','plot_address')){
 $script:mockSpectrum=Copy-Map $parsed;$script:mockSpectrum.status[$key]='13000000';Reject {& $prefixBlock 'fft-unit'} "out of ELF range $key"
 $script:mockSpectrum=Copy-Map $parsed;$script:mockSpectrum.status[$key]='002d0001';Reject {& $prefixBlock 'fft-unit'} "unaligned $key"
}
$script:mockSpectrum=Copy-Map $parsed
foreach($mutation in @(@('addr=10200000','addr=11000000'),@('bytes=1843200','bytes=1843196'),@('format=BGR888','format=RGB888'),@('crc=01234567','crc=0123456g'))){
 $good='SNAPSHOT_READY addr=10200000 bytes=1843200 format=BGR888 crc=01234567 frames=1';$script:frameReply=$good.Replace($mutation[0],$mutation[1]);Reject {& $prefixBlock 'fft-unit'} "frame metadata $($mutation[0])"
}
$script:mockSpectrum.status.valid='0';Reject {& $prefixBlock 'fft-unit'} 'no unpublished FFT data export'
Write-Output "SPECTRUM_HELPER_TEST_PASS: $script:checks checks; offline mocks only; no UART/JTAG"
