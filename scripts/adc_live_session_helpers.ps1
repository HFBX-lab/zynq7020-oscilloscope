function Export-AdcLiveRecords($Root,$Run,$Port,$Captured,$State) {
 for($index=0;$index -lt 2;$index++){
  $start=$Captured.Length
  $Port.Write("snapshot`nadcstatus`n");$State.resume_needed=$true
  $timer=[Diagnostics.Stopwatch]::StartNew()
  do {
   Pull
   $fresh=$Captured.ToString().Substring($start)
   $snapshot=[regex]::Match($fresh,'SNAPSHOT_READY[^\r\n]+')
   $metadata=[regex]::Match($fresh,'ADC_LIVE_STATUS available=1 pending=[01] id=(\d+) address=(1300[04]000) bytes=16384 rate=625000 crc32=([0-9a-fA-F]{8}) status=([0-9a-fA-F]{8})')
   if($timer.Elapsed.TotalSeconds -gt 8){throw 'ADC snapshot metadata timeout'}
   if(!$snapshot.Success -or !$metadata.Success){Start-Sleep -Milliseconds 40}
  }while(!$snapshot.Success -or !$metadata.Success)
  $id=[uint32]$metadata.Groups[1].Value
  if(!$id){throw 'No external ADC record published'}
  if($State.records.Count -and $id -le $State.records[-1].id){throw 'ADC ID did not advance'}
  if(([Convert]::ToUInt32($metadata.Groups[4].Value,16) -band 0x7c) -ne 0){throw 'ADC error/reset/internal status while exporting'}
  $address=$metadata.Groups[2].Value;$crc=$metadata.Groups[3].Value.ToUpperInvariant()
  $script=@'
if {[catch {
connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "APU"}
configparams force-mem-accesses 1
mrd -bin -file {@RUN@/record@INDEX@.bin} 0x@ADDRESS@ 4096
puts ADC_LIVE_RECORD_EXPORTED
disconnect
} e opts]} {puts stderr "EXPORT_ERROR: $e";exit 1}
exit
'@
  $script.Replace('@RUN@',$Run).Replace('@INDEX@',[string]$index).Replace('@ADDRESS@',$address) | Set-Content "$Run/export$index.tcl" -Encoding ascii
  & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat "$Run/export$index.tcl" *> "$Run/export$index.log"
  if($LASTEXITCODE -or !(Select-String "$Run/export$index.log" -Pattern '^ADC_LIVE_RECORD_EXPORTED' -Quiet)){throw 'APU export failed'}
  & D:/Python/python.exe -X utf8 "$Run/analyze_adc_live_record.py" "$Run/record$index.bin" --crc $crc --output "$Run/record$index.json" *> "$Run/record$index-audit.log"
  if($LASTEXITCODE){throw "Host record$index audit failed"}
  $State.records+=@{id=$id;address=$address;crc32=$crc;file="record$index.bin";sha256=(Get-FileHash "$Run/record$index.bin").Hash}
  $Port.Write("resume`n");$State.resume_needed=$false
  $timer.Restart();while($timer.Elapsed.TotalSeconds -lt 1.5){Pull;Start-Sleep -Milliseconds 40}
 }
 $Port.Write("adcstatus`nstatus`ntouchstatus`n");Start-Sleep -Milliseconds 250;Pull
 foreach($d in @(Get-TouchReadyLines $Captured.ToString())){
  if($d.Value -notmatch '(?:^| )locked=1(?: |$)' -or $d.Value -notmatch 'pixel_locked=1' -or $d.Value -notmatch 'dma_errors=00000000'){throw "Post-export video diagnostic failed: $d"}
 }
}
