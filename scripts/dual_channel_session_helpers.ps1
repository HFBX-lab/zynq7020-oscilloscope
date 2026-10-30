# Loaded by the authorized acquisition runner; shares its live serial/server ownership.
function Test-DualChannelControls {
 Send-AcqCommands @('source 2','trigsource 2','triggermode auto','trigger rise 500','dual 1','single')
 $s=Wait-AcqState {param($x) $x.ui.dual -eq '1' -and $x.ui.dual_visible -eq '1' -and $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0'}
 $held=$s.ui.display_id;$heldCrc=$s.adc.display_crc;$heldEpoch=$s.ui.epoch;$heldCross=$s.ui.crossing
 Note-Acq 'dual-single-completes-on-ADC2' $s
 foreach($enabled in @('0','1')){
  Send-AcqCommands @("dual $enabled")
  $s=Read-AcqState
  if($s.ui.dual -ne $enabled -or $s.ui.dual_visible -ne $enabled -or $s.ui.display_id -ne $held -or $s.adc.display_crc -ne $heldCrc -or $s.ui.epoch -ne $heldEpoch -or $s.ui.crossing -ne $heldCross -or $s.ui.single_complete -ne '1' -or $s.ui.running -ne '0'){throw 'Display toggle changed frozen acquisition or did not update visibility'}
  Note-Acq "dual-$enabled-preserves-single-ID-CRC-epoch" $s
 }
 # Real LVGL pointer path through the settings overlay, with the single still frozen.
 Send-AcqCommands @('tap 950 32')
 Start-Sleep -Milliseconds 180;Pull
 Send-AcqCommands @('tap 500 370')
 $s=Wait-AcqState {param($x) $x.ui.dual -eq '0' -and $x.ui.single_complete -eq '1'}
 if($s.ui.display_id -ne $held -or $s.adc.display_crc -ne $heldCrc){throw 'Pointer display switch corrupted frozen record'}
 Note-Acq 'LVGL-pointer-display-single' $s
 Send-AcqCommands @('tap 500 370')
 $s=Wait-AcqState {param($x) $x.ui.dual_visible -eq '1'}
 Note-Acq 'LVGL-pointer-display-both' $s
 Send-AcqCommands @('tap 650 490')
 Start-Sleep -Milliseconds 180;Pull
 Export-DualChannelFrame
 Send-AcqCommands @('source 3','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'SIM' -and $x.ui.dual -eq '1' -and $x.ui.dual_visible -eq '0'}
 Note-Acq 'dual-hidden-for-simulation-preference-kept' $s
 Send-AcqCommands @('source 1','trigsource 1')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC1' -and $x.ui.dual_visible -eq '1' -and $x.ui.running -eq '1'}
 Note-Acq 'dual-ADC1-trigger-source' $s
 Send-AcqCommands @('source 2','trigsource 2','trigger rise 500','triggermode auto','dual 1','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC2' -and $x.ui.dual_visible -eq '1' -and $x.ui.running -eq '1' -and $x.ui.triggered -eq '1'}
 Note-Acq 'final-dual-ADC2-auto-running' $s
}
function Export-DualChannelFrame([string]$Stem='dual') {
 if($Stem -notmatch '^[a-z]+$'){throw 'Unexpected frame export stem'}
 $ready=Invoke-AcqCommand 'snapshot' '(?m)^SNAPSHOT_READY[^\r\n]+\r?\n'
 $state.resume_needed=$true
 $meta=[regex]::Match($ready,'addr=(10[02]00000) bytes=1843200 format=BGR888 crc=([0-9a-fA-F]{8})')
 if(!$meta.Success){throw 'Missing frozen frame metadata'}
 $address=$meta.Groups[1].Value;$crc=$meta.Groups[2].Value
 $s=Read-AcqState
 if($s.ui.single_complete -ne '1' -or $s.ui.dual_visible -ne '1' -or $s.ui.running -ne '0'){throw 'Export must show frozen dual single'}
 if($s.adc.display_id -ne $s.ui.display_id -or $s.adc.id -ne $s.ui.display_id){throw 'Single raw/display record mismatch before export'}
 $recordAddress=$s.adc.display_address;$recordCrc=$s.adc.display_crc
 if($recordAddress -notmatch '^1300[04]000$' -or $recordCrc -notmatch '^[0-9a-fA-F]{8}$'){throw 'Unexpected single record address/CRC'}
 $state[$Stem+'_frame_ui']=$s.ui
 @'
if {[catch {
connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "APU"}
configparams force-mem-accesses 1
mrd -bin -file {@RUN@/@STEM@-frame.bin} 0x@ADDRESS@ 460800
mrd -bin -file {@RUN@/@STEM@-single-record.bin} 0x@RECORD@ 4096
puts DUAL_FRAME_EXPORTED
disconnect
} e opts]} {puts stderr "EXPORT_ERROR: $e";exit 1}
exit
'@.Replace('@RUN@',$run).Replace('@ADDRESS@',$address).Replace('@RECORD@',$recordAddress).Replace('@STEM@',$Stem) | Set-Content "$run/export-$Stem-frame.tcl" -Encoding ascii
 & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat "$run/export-$Stem-frame.tcl" *> "$run/export-$Stem-frame.log"
 if($LASTEXITCODE -or !(Select-String "$run/export-$Stem-frame.log" -Pattern '^DUAL_FRAME_EXPORTED' -Quiet)){throw 'Dual frame APU export failed'}
 & D:/Python/python.exe -X utf8 "$run/decode_board_frame.py" "$run/$Stem-frame.bin" $crc "$run/$Stem-frame.png" *> "$run/$Stem-frame-audit.log"
 if($LASTEXITCODE){throw 'Dual frame host CRC audit failed'}
 & D:/Python/python.exe -X utf8 "$run/analyze_adc_live_record.py" "$run/$Stem-single-record.bin" --crc $recordCrc --output "$run/$Stem-single-record.json" *> "$run/$Stem-single-record-audit.log"
 if($LASTEXITCODE){throw 'Frozen single independent raw record CRC audit failed'}
 # Count each channel's exact trace color inside the chart, excluding legends/text.
 & D:/Python/python.exe -X utf8 "$run/check_dual_frame.py" "$run/$Stem-frame.png" *> "$run/$Stem-frame-colors.log"
 if($LASTEXITCODE){throw 'Frozen frame missing a channel trace'}
 $state[$Stem+'_frame']=@{file="$Stem-frame.bin";address=$address;crc32=$crc;sha256=(Get-FileHash "$run/$Stem-frame.bin").Hash;source='APU DDR export; not a physical panel photo';record_id=$s.ui.display_id;record_crc=$recordCrc;record_sha256=(Get-FileHash "$run/$Stem-single-record.bin").Hash}
 Note-Acq "$Stem-single-actual-frame-CRC-and-two-traces" $s
 Send-AcqCommands @('resume');$state.resume_needed=$false
}
