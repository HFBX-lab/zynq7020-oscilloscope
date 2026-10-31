# FFT helpers share the parent runner UART/JTAG connections.
$SpectrumPython='D:/Python/python.exe'
function Convert-SpectrumStatusFrame([string]$Body){
 $h=[regex]::Matches($Body,'(?m)^FFT_STATUS [^\r\n]+\r?\n');$v=[regex]::Matches($Body,'(?m)^FFT_VALUES [^\r\n]+\r?\n');$e=[regex]::Matches($Body,'(?m)^FFT_END [^\r\n]+\r?\n')
 if($h.Count -ne 1 -or $v.Count -ne 1 -or $e.Count -ne 1){throw 'Incomplete FFT frame'}
 $status=Convert-AcqFields $h[0].Value;$values=Convert-AcqFields $v[0].Value;$end=Convert-AcqFields $e[0].Value
 foreach($key in @('valid','panel','seq','channel','window','remove_mean','auto_range','end_bin','count','rate','id','source_epoch','conversion','actual_conversion','filter','raw_address','raw_bytes','raw_crc','bins_address','bins_bytes','bins_crc','plot_address','plot_bytes','plot_crc','elapsed_ms')){if(!$status.ContainsKey($key)){throw "Missing FFT status $key"}}
 foreach($key in @('bin_hz','mean_v','gain','peak_valid','peak_bin','peak_hz','peak_vrms','peak_dbv','overrange','ui_epoch','running','display_id','live_rate','replay','wave_crc','param_crc')){if(!$values.ContainsKey($key)){throw "Missing FFT values $key"}}
 if($status.seq -ne $end.seq -or $h[0].Index -gt $v[0].Index -or $v[0].Index -gt $e[0].Index){throw 'Mixed or unordered FFT reply'}
 foreach($key in @('valid','panel','channel','window','remove_mean','auto_range')){if($status[$key] -notin @('0','1')){throw 'Invalid FFT flags'}}
 foreach($key in @('peak_valid','overrange','running','replay')){if($values[$key] -notin @('0','1')){throw 'Invalid FFT value flags'}}
 foreach($key in @('wave_crc','param_crc')){if($values[$key] -notmatch '^[a-fA-F0-9]{8}$'){throw 'Invalid FFT slot CRC'}}
 if($status.raw_bytes -ne '16384' -or $status.bins_bytes -ne '8196' -or $status.plot_bytes -ne '2880' -or $status.actual_conversion -ne '50000000'){throw 'Invalid FFT sizes/platform'}
 foreach($key in @('raw_address','bins_address','plot_address','raw_crc','bins_crc','plot_crc')){if($status[$key] -notmatch '^[a-fA-F0-9]{8}$'){throw 'Invalid FFT hex field'}}
 if($status.valid -eq '1' -and ($status.count -ne '4096' -or [int]$status.rate -le 0 -or [int]$status.end_bin -lt 16 -or [int]$status.end_bin -gt 2048)){throw 'Invalid valid FFT dimensions'}
 foreach($key in @('bin_hz','mean_v','gain','peak_bin','peak_hz','peak_vrms','peak_dbv')){if([double]::IsNaN([double]$values[$key]) -or [double]::IsInfinity([double]$values[$key])){throw 'Nonfinite FFT values'}}
 return @{status=$status;values=$values}
}
function Read-SpectrumState {
 [void](Invoke-AcqCommand '__query_boundary__' '(?m)^COMMANDS:[^\r\n]+\r?\n');$begin=$captured.Length
 [void](Invoke-AcqCommand 'fftstatus' '(?m)^FFT_END seq=\d+\r?\n')
 return Convert-SpectrumStatusFrame $captured.ToString().Substring($begin)
}
function Read-SpectrumSnapshot {
 $s=Read-ParameterSnapshot;$s.fft=Read-SpectrumState
 Assert-MemoryFields $s.ui $s.fft.values @('running','display_id') 'FFT current context'
 if($s.ui.epoch -ne $s.fft.values.ui_epoch -or $s.parameter.status.wave_crc -ne $s.fft.values.wave_crc -or $s.parameter.status.blob_crc -ne $s.fft.values.param_crc -or $s.fft.values.live_rate -ne $s.parameter.status.rate -or $s.fft.values.replay -ne $s.memory.status.replay){throw 'FFT mixed acquisition/slots'}
 return $s
}
function Assert-SpectrumDataHeld($Before,$After){
 Assert-ParameterRawHeld $Before $After 0
 Assert-ParameterPreferences $Before $After
 Assert-MemoryFields $Before.parameter.status $After.parameter.status @('valid','blob_crc','wave_valid','wave_crc') 'FFT held slots'
}
function Assert-SpectrumCacheHeld($Before,$After){
 Assert-MemoryFields $Before.status $After.status @('valid','seq','channel','window','remove_mean','count','rate','id','source_epoch','conversion','filter','raw_address','raw_crc','bins_address','bins_crc') 'FFT frozen cache'
 Assert-MemoryFields $Before.values $After.values @('bin_hz','mean_v','gain','peak_valid','peak_bin','peak_hz','peak_vrms','peak_dbv','overrange','wave_crc','param_crc') 'FFT cached values'
}
function Note-Spectrum([string]$Name,$Snapshot){$state.spectrum_checks+=@{test=$Name;snapshot=$Snapshot};[void](Note-Acq $Name $Snapshot)}
function Export-Spectrum([string]$Name){
 if($Name -notmatch '^fft-[a-z0-9-]+$'){throw 'Unsafe FFT export name'}
 $s=Read-SpectrumState;$h=$s.status;$v=$s.values
 if($h.valid -ne '1'){throw 'No valid FFT snapshot'}
 foreach($pair in @(@('raw_address',16384),@('bins_address',8196),@('plot_address',2880))){Assert-MemoryElfRange $h[$pair[0]] ([uint64]$pair[1])}
 $f=Convert-AcqFields (Invoke-AcqCommand 'snapshot' '(?m)^SNAPSHOT_READY[^\r\n]+\r?\n');$state.resume_needed=$true
 if($f.addr -notmatch '^10[02]00000$' -or $f.crc -notmatch '^[a-fA-F0-9]{8}$' -or $f.bytes -ne '1843200' -or $f.format -ne 'BGR888'){throw 'Invalid FFT frame'}
 @'
if {[catch {
connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "APU"}
configparams force-mem-accesses 1
mrd -bin -file {@RUN@/@NAME@-raw.bin} 0x@RAW@ 4096
mrd -bin -file {@RUN@/@NAME@-bins.bin} 0x@BINS@ 2049
mrd -bin -file {@RUN@/@NAME@-plot.bin} 0x@PLOT@ 720
mrd -bin -file {@RUN@/@NAME@-frame.bin} 0x@FRAME@ 460800
puts SPECTRUM_EXPORTED
disconnect
} e opts]} {puts stderr "EXPORT_ERROR: $e";exit 1}
exit
'@.Replace('@RUN@',$run).Replace('@NAME@',$Name).Replace('@RAW@',$h.raw_address).Replace('@BINS@',$h.bins_address).Replace('@PLOT@',$h.plot_address).Replace('@FRAME@',$f.addr)|Set-Content "$run/export-$Name.tcl" -Encoding ascii
 & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat "$run/export-$Name.tcl" *> "$run/export-$Name.log"
 if($LASTEXITCODE -or !(Select-String "$run/export-$Name.log" -Pattern '^SPECTRUM_EXPORTED' -Quiet)){throw 'Spectrum export failed'}
 $meta=@{valid=$true;count=4096;rate_hz=[int]$h.rate;channel=[int]$h.channel;window=[int]$h.window;remove_mean=($h.remove_mean -eq '1');overrange=($v.overrange -eq '1');peak_valid=($v.peak_valid -eq '1');bin_hz=[double]$v.bin_hz;mean_v=[double]$v.mean_v;coherent_gain=[double]$v.gain;peak_bin=[double]$v.peak_bin;peak_hz=[double]$v.peak_hz;peak_vrms=[double]$v.peak_vrms;peak_dbv=[double]$v.peak_dbv}
 $meta|ConvertTo-Json -Depth 3|Set-Content "$run/$Name-metadata.json" -Encoding utf8
 & $SpectrumPython -X utf8 "$run/spectrum_reference.py" "$run/$Name-raw.bin" --bins "$run/$Name-bins.bin" --metadata "$run/$Name-metadata.json" --output "$run/$Name-reference.json" *> "$run/$Name-reference.log"
 if($LASTEXITCODE){throw 'Independent NumPy spectrum comparison failed'}
 & D:/Python/python.exe -X utf8 "$run/decode_board_frame.py" "$run/$Name-frame.bin" $f.crc "$run/$Name-frame.png" *> "$run/$Name-frame-audit.log"
 if($LASTEXITCODE){throw 'FFT frame CRC audit failed'}
 Send-AcqCommands @('resume');$state.resume_needed=$false
 $after=Read-SpectrumState;Assert-SpectrumCacheHeld $s $after
 Assert-MemoryFields $h $after.status @('auto_range','end_bin','plot_crc') 'FFT export projection'
 $entry=@{name=$Name;fft=$s;metadata=$meta;raw_crc=$h.raw_crc;bins_crc=$h.bins_crc;plot_crc=$h.plot_crc;frame_crc=$f.crc;raw_sha256=(Get-FileHash "$run/$Name-raw.bin").Hash;bins_sha256=(Get-FileHash "$run/$Name-bins.bin").Hash;plot_sha256=(Get-FileHash "$run/$Name-plot.bin").Hash;frame_sha256=(Get-FileHash "$run/$Name-frame.bin").Hash}
 $state.spectrum_exports+=@($entry);return $entry
}
function Test-SpectrumControls {
 $state.spectrum_checks=@();$state.spectrum_exports=@()
 Send-AcqCommands @('fft 0','parameters 0','records 0','measurements 0','source 2','trigsource 2','trigger rise 500','triggermode auto','dual 1','cursors 0','acqrun')
 [void](Set-LongTimebase 6);$null=Wait-Highspeed50MSingle 6;$before=Read-SpectrumSnapshot
 Send-AcqCommands @('measurements 1');Tap-Parameter 632 162
 $opened=Read-SpectrumSnapshot;Assert-SpectrumDataHeld $before $opened
 if($opened.fft.status.panel -ne '1' -or $opened.fft.status.channel -ne '1' -or $opened.fft.status.rate -ne '6250000' -or $opened.fft.status.id -ne $before.ui.display_id -or $opened.fft.status.source_epoch -ne $before.memory.view.display_epoch -or $opened.fft.values.peak_valid -ne '1' -or [math]::Abs([double]$opened.fft.values.peak_hz-50000) -gt 1500){throw 'Pointer FFT opening did not bind known CH2 record'}
 $state.spectrum_opened=@{before=$before;after=$opened};Note-Spectrum 'fft-pointer-open-preserves-single' $opened;$null=Export-Spectrum 'fft-hann-single'
 Tap-Parameter 800 136;$full=Read-SpectrumSnapshot;Assert-SpectrumDataHeld $opened $full;Assert-SpectrumCacheHeld $opened.fft $full.fft
 if($full.fft.status.auto_range -ne '0' -or $full.fft.status.end_bin -ne '2048'){throw 'FFT full range pointer failed'}
 Note-Spectrum 'fft-range-only-preserves-spectrum' $full;$null=Export-Spectrum 'fft-hann-full'
 Tap-Parameter 368 136;$rect=Read-SpectrumSnapshot;Assert-SpectrumDataHeld $full $rect
 if($rect.fft.status.window -ne '0' -or [uint64]$rect.fft.status.seq -ne [uint64]$full.fft.status.seq+1){throw 'FFT window did not update once'}
 Note-Spectrum 'fft-rect-window-preserves-single' $rect;$null=Export-Spectrum 'fft-rect-single'
 Tap-Parameter 368 136;Tap-Parameter 564 136;$meanOff=Read-SpectrumSnapshot;Assert-SpectrumDataHeld $rect $meanOff
 if($meanOff.fft.status.window -ne '1' -or $meanOff.fft.status.remove_mean -ne '0'){throw 'FFT mean pointer failed'}
 Note-Spectrum 'fft-keep-mean-actual-bins' $meanOff;$null=Export-Spectrum 'fft-hann-mean-off'
 Tap-Parameter 564 136;Tap-Parameter 136 136;$empty=Read-SpectrumSnapshot;Assert-SpectrumDataHeld $meanOff $empty
 if($empty.fft.status.channel -ne '0' -or $empty.fft.values.peak_valid -ne '0' -or $empty.ui.source -ne 'ADC2' -or $empty.ui.trig_channel -ne '2'){throw 'FFT channel selection altered acquisition or invented empty peak'}
 Note-Spectrum 'fft-independent-empty-CH1-no-peak' $empty;$null=Export-Spectrum 'fft-empty-ch1'
 Tap-Parameter 224 136;Tap-Parameter 874 93;Send-AcqCommands @('measurements 0','acqrun')
 [void](Set-LongTimebase 5);$live=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.adc.display_rate -eq '50000000' -and $x.adc.display_epoch -eq $x.ui.epoch}
 $held=Read-SpectrumState
 Start-Sleep -Milliseconds 1100;$advanced=Read-AcqState;$still=Read-SpectrumState;Assert-SpectrumCacheHeld $held $still
 if([uint64]$advanced.ui.display_id -le [uint64]$live.ui.display_id -or $still.status.rate -ne '6250000' -or $still.status.id -eq $advanced.ui.display_id){throw 'Background records relabelled cached FFT or failed to advance'}
 $state.spectrum_live_hold=@{before=$live;after=$advanced;fft_before=$held;fft_after=$still};[void](Note-Acq 'fft-cached-6m25-held-while-live50m' $advanced)
 $epoch=$advanced.ui.epoch;Send-AcqCommands @('fft 1','fftupdate');$fresh=Read-SpectrumState;$running=Read-AcqState
 if($fresh.status.rate -ne '50000000' -or $fresh.status.conversion -ne '50000000' -or $fresh.values.running -ne '1' -or $running.ui.epoch -ne $epoch -or [uint64]$fresh.status.seq -le [uint64]$held.status.seq){throw 'Running FFT update did not bind raw50M without restarting'}
 $state.spectrum_live_update=@{before=$advanced;after=$running;fft=$fresh};[void](Note-Acq 'fft-live-update-does-not-restart' $running);$null=Export-Spectrum 'fft-live-raw50m'
 Send-AcqCommands @('fft 0');[void](Set-LongTimebase 6);$null=Wait-Highspeed50MSingle 6
 $state.spectrum_before_replay=Read-SpectrumSnapshot
 Send-AcqCommands @('memload');$replayBefore=Read-SpectrumSnapshot
 Send-AcqCommands @('fft 1','fftupdate');$replay=Read-SpectrumSnapshot;Assert-SpectrumDataHeld $replayBefore $replay
 if($replay.fft.status.rate -ne $replay.memory.view.rate -or $replay.fft.status.id -ne $replay.ui.display_id -or $replay.fft.status.source_epoch -ne $replay.memory.view.display_epoch -or $replay.fft.status.actual_conversion -ne '50000000'){throw 'Replay FFT used live rate/ID/epoch'}
 if($replay.fft.status.rate -eq $state.spectrum_before_replay.memory.view.rate){throw 'Cross-rate replay case did not use different live/stored rates'}
 Note-Spectrum 'fft-replay-uses-stored-source-metadata' $replay;$null=Export-Spectrum 'fft-replay'
 Send-AcqCommands @('fft 0','memlive','acqstop','source 3');$demo=Read-AcqState;$cache=Read-SpectrumState;$begin=$captured.Length
 Send-AcqCommands @('fft 1','fftupdate','fftchannel 1','fftwindow rect','fftmean 0');$rejected=Read-SpectrumState;$after=Read-AcqState
 Assert-SpectrumCacheHeld $cache $rejected
 if($demo.ui.epoch -ne $after.ui.epoch -or $demo.ui.source -ne $after.ui.source){throw 'Rejected FFT altered DEMO'}
 $log=$captured.ToString().Substring($begin)
 foreach($name in @('fft','fftupdate','fftchannel','fftwindow','fftmean')){if($log -notmatch "(?m)^FFT_COMMAND $name REJECTED\r?$" ){throw 'FFT DEMO rejection not reported'}}
 $state.spectrum_demo=@{before=$demo;after=$after;fft_before=$cache;fft_after=$rejected};[void](Note-Acq 'fft-DEMO-rejections-preserve-cache' $after)
 Send-AcqCommands @('source 2','trigger rise 500','triggermode auto','trigsource 2','dual 1','cursors 0','acqrun');[void](Set-LongTimebase 6)
 $final=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.ui.triggered -eq '1' -and $x.adc.display_rate -eq '6250000' -and $x.adc.display_epoch -eq $x.ui.epoch}
 $state.spectrum_final=@{ui=$final.ui;adc=$final.adc;fft=(Read-SpectrumState);parameter=(Read-ParameterState)};[void](Note-Acq 'final-spectrum-CH2-10us-live' $final)
}
