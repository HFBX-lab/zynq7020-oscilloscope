# Read-only engineering backup of the user's volatile SCW1 slot before reload.
param(
 [string]$FirmwareProof='artifacts/settings-ui-board/20261025-153656-3468/validation.json',
 [string]$ExpectedElf='DA748CA506AF441263E06923EFC0E9521DA1994B23CF32CC81885F3C67ABE6E3',
 [string]$BackupRoot='D:/FPGA_Project/SP2_ZeroMarkers_Build/user-backup'
)
$ErrorActionPreference='Stop'
$root='D:/workspace/zynq7020-oscilloscope'
$run=$BackupRoot+'/'+(Get-Date -Format yyyyMMdd-HHmmss-ffff)
New-Item -ItemType Directory -Path $run -Force|Out-Null
$proof=Get-Content (Join-Path $root $FirmwareProof) -Raw|ConvertFrom-Json
if($proof.elf_sha256 -ne $ExpectedElf){throw 'Accepted firmware proof mismatch'}
. "$root/scripts/acq_ui_session_helpers.ps1"
. "$root/scripts/settings_session_helpers.ps1"
$port=[IO.Ports.SerialPort]::new('COM18',115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One);$port.ReadBufferSize=65536
$captured=[Text.StringBuilder]::new();$launcher=$null;$owned=$null;$state=@{state='reading';hardware_reset=$false;flash_written=$false}
function Pull{$chunk=$port.ReadExisting();if($chunk){[void]$captured.Append($chunk);[IO.File]::AppendAllText("$run/uart.log",$chunk)}}
function ReadSlot{
 $start=$captured.Length;[void](Invoke-AcqCommand 'recordstatus' '(?m)^MEMORY_END display_id=\d+\r?\n')
 $m=[regex]::Match($captured.ToString().Substring($start),'(?m)^MEMORY_STATUS [^\r\n]+\r?\n')
 if(!$m.Success){throw 'Memory status unavailable'};return Convert-AcqFields $m.Value
}
try{
 $port.Open();$port.DiscardInBuffer();$before=ReadSlot;$state.before=$before
 $parameters=Read-ParameterState;$state.parameters_before=$parameters;$state.ui_before=Read-AcqState
 if($before.valid -ne '1'){$state.state='verified-no-user-slot';Write-Output 'USER_RECORD_BACKUP: no stored record';return}
 if($before.blob_address -notmatch '^[0-9a-fA-F]{8}$' -or $before.blob_bytes -ne '16512'){throw 'Invalid backup location'}
 $address=[Convert]::ToUInt64($before.blob_address,16);$safe=$false
 foreach($segment in $proof.load_segments){$low=[Convert]::ToUInt64($segment[0].Replace('0x',''),16);$high=[Convert]::ToUInt64($segment[1].Replace('0x',''),16);if($address -ge $low -and $address+16512 -le $high){$safe=$true}}
 if(!$safe){throw 'Backup slot outside accepted ELF bounds'}
 $parameterRead=''
 if($parameters.status.valid -eq '1'){
  $pa=[Convert]::ToUInt64($parameters.status.blob_address,16);$safe=$false
  foreach($segment in $proof.load_segments){$low=[Convert]::ToUInt64($segment[0].Replace('0x',''),16);$high=[Convert]::ToUInt64($segment[1].Replace('0x',''),16);if($pa -ge $low -and $pa+96 -le $high){$safe=$true}}
  if(!$safe){throw 'Parameter slot outside accepted ELF'}
  $parameterRead="mrd -bin -file {$run/user-settings.scp} 0x$($parameters.status.blob_address) 24"
 }
 $frame=Convert-AcqFields (Invoke-AcqCommand 'snapshot' '(?m)^SNAPSHOT_READY[^\r\n]+\r?\n');$state.resume_needed=$true
 if($frame.addr -notmatch '^10[02]00000$' -or $frame.crc -notmatch '^[0-9A-Fa-f]{8}$' -or $frame.bytes -ne '1843200' -or $frame.format -ne 'BGR888'){throw 'Invalid backup frame'}
 $state.frame=$frame
 if(Get-NetTCPConnection -LocalPort 3121 -State Listen -ErrorAction SilentlyContinue){throw 'Port3121 already in use'}
 $launched=[DateTime]::UtcNow
 $launcher=Start-Process D:/Xilinx_2020.2/Vitis/2020.2/bin/hw_server.bat -ArgumentList '-s','tcp::3121' -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/server.log" -RedirectStandardError "$run/server-error.log"
 $watch=[Diagnostics.Stopwatch]::StartNew()
 do{$listener=Get-NetTCPConnection -LocalPort 3121 -State Listen -ErrorAction SilentlyContinue|Select-Object -First 1;if(!$listener){Start-Sleep -Milliseconds 100};if($watch.Elapsed.TotalSeconds -gt 15){throw 'Backup server timeout'}}while(!$listener)
 $p=Get-CimInstance Win32_Process -Filter "ProcessId=$($listener.OwningProcess)"
 if($p.Name -ne 'hw_server.exe' -or $p.ExecutablePath -ne 'D:\Xilinx_2020.2\Vitis\2020.2\bin\unwrapped\win64.o\hw_server.exe' -or $p.CreationDate.ToUniversalTime() -lt $launched){throw 'Backup server identity mismatch'}
 $ancestor=$p;$child=$false;for($i=0;$i -lt 5 -and $ancestor;$i++){if($ancestor.ParentProcessId -eq $launcher.Id){$child=$true;break};$ancestor=Get-CimInstance Win32_Process -Filter "ProcessId=$($ancestor.ParentProcessId)"}
 if(!$child){throw 'Backup server unrelated'};$owned=@{pid=$p.ProcessId;parent=$p.ParentProcessId;ticks=$p.CreationDate.ToUniversalTime().Ticks}
 @'
if {[catch {
connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "APU"}
configparams force-mem-accesses 1
mrd -bin -file {@RUN@/user-waveform.scw} 0x@ADDR@ 4128
@PARAM@
mrd -bin -file {@RUN@/before-frame.bin} 0x@FRAME@ 460800
puts USER_RECORD_READ_ONLY_EXPORTED
disconnect
} e]} {puts stderr $e;exit 1}
exit
'@.Replace('@RUN@',$run).Replace('@ADDR@',$before.blob_address).Replace('@PARAM@',$parameterRead).Replace('@FRAME@',$frame.addr)|Set-Content "$run/export.tcl" -Encoding ascii
 & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat "$run/export.tcl" *> "$run/export.log"
 if($LASTEXITCODE -or !(Select-String "$run/export.log" -Pattern '^USER_RECORD_READ_ONLY_EXPORTED' -Quiet)){throw 'Backup export failed'}
 $after=ReadSlot;$state.after=$after
 foreach($key in @('valid','blob_address','blob_crc','saved_id','saved_rate')){if($before[$key] -ne $after[$key]){throw 'User slot changed while backing up'}}
 & D:/Python/python.exe -X utf8 "$root/scripts/record_blob.py" "$run/user-waveform.scw" --output "$run/record-summary.json" *> "$run/parse.log"
 if($LASTEXITCODE){throw 'Backup record independent validation failed'}
 $summary=Get-Content "$run/record-summary.json" -Raw|ConvertFrom-Json
 if($summary.crc32 -ne $before.blob_crc){throw 'Backup CRC differs from UART'}
 if($parameters.status.valid -eq '1'){
  & D:/Python/python.exe -X utf8 "$root/scripts/settings_blob.py" "$run/user-settings.scp" --output "$run/settings-summary.json" *> "$run/settings-parse.log"
  if($LASTEXITCODE){throw 'Parameter backup validation failed'}
  $p=Read-ParameterState;$state.parameters_after=$p
  $parsed=Get-Content "$run/settings-summary.json" -Raw|ConvertFrom-Json
  if($parsed.crc32 -ne $parameters.status.blob_crc -or $p.status.blob_crc -ne $parameters.status.blob_crc){throw 'Parameter backup CRC mismatch'}
 }
 & D:/Python/python.exe -X utf8 "$root/scripts/decode_board_frame.py" "$run/before-frame.bin" $frame.crc "$run/before-frame.png" *> "$run/frame-audit.log"
 if($LASTEXITCODE){throw 'Before frame CRC mismatch'}
 $state.state='verified-readonly-user-slot-backup';$state.sha256=(Get-FileHash "$run/user-waveform.scw").Hash
 Write-Output "USER_RECORD_BACKUP_PASS: $run; no reset/no program change"
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{
 try{if($port.IsOpen){if($state.resume_needed){[void](Invoke-AcqCommand 'resume' '(?m)^RENDER_RESUMED\r?\n');$state.resume_needed=$false};Pull;$port.Close()}}finally{$port.Dispose()}
 if($owned){$p=Get-CimInstance Win32_Process -Filter "ProcessId=$($owned.pid)";if($p -and $p.ParentProcessId -eq $owned.parent -and $p.CreationDate.ToUniversalTime().Ticks -eq $owned.ticks){Stop-Process -Id $owned.pid;$state.server='owned-stopped'}}
 $state|ConvertTo-Json -Depth 5|Set-Content "$run/session.json" -Encoding utf8
}
