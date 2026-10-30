# Restore the immutable, physically acceptedM4B snapshot; temporary PL/RAM only.
param([string]$Snapshot='D:/FPGA_Project/SP2_Autoset_Build/board/20261024-212611-0805')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$expected=@{'platform.bit'='BE748A2A543047688010B3FD6179CDA517EB5EDBA76A27962D62EFBCA90A3FBE';'application.elf'='D6157F4521680BDCDB58473F4B23684A72D986C34AAB212CE750BF78DABD42BB';'ps7_init.tcl'='4CA66BDB7DC3F213619CA01133B46DCC3804B70E2DCD49776FD046876FB72541';'board_init_helpers.tcl'='C9AC48C6C33C1EA113A152892180CA6C6F7EB54B19FBFB96240C190BF9122262'}
foreach($item in $expected.GetEnumerator()){if((Get-FileHash -LiteralPath "$Snapshot/$($item.Key)").Hash -ne $item.Value){throw 'Accepted50M snapshot hash mismatch'}}
if(Get-NetTCPConnection -LocalPort 3121 -State Listen -ErrorAction SilentlyContinue){throw 'Another server owns3121'}
$run='D:/FPGA_Project/SP2_Memory_Build/restore/'+(Get-Date -Format 'yyyyMMdd-HHmmss-ffff')
New-Item -ItemType Directory $run -Force|Out-Null
# Recreate loader from the trusted repository template and pinned snapshots.
(Get-Content "$root/firmware/touch/boot.tcl" -Raw).Replace('@RUN@',$Snapshot).Replace('TOUCH_APP_RAM_LOADED','ACCEPTEDM4B_RESTORED')|Set-Content "$run/boot.tcl" -Encoding ascii
. "$root/scripts/touch_uart_checks.ps1"
. "$root/scripts/acq_ui_session_helpers.ps1"
. "$root/scripts/long_timebase_session_helpers.ps1"
$Highspeed50M=$false;$Highspeed50M=$true;$Highspeed10M=$false
$captured=[Text.StringBuilder]::new();$state=@{state='restoring';snapshot=$Snapshot;flash_written=$false;controls=@();resume_needed=$false}
$port=[IO.Ports.SerialPort]::new('COM18',115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One);$port.DtrEnable=$false;$port.RtsEnable=$false;$port.ReadBufferSize=65536
$launcher=$null;$owned=$null;$launcherTicks=$null;$launchedAt=$null;$restoreLate=$null
function Pull{$chunk=$port.ReadExisting();if($chunk){[void]$captured.Append($chunk);[IO.File]::AppendAllText("$run/uart.log",$chunk)};if($captured.ToString() -match 'APP_FATAL|APP_LOG_ERROR|ADC_LIVE_FAIL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT') {throw '50M restore app failure'}}
function OwnedRestoreServer($candidate){
 if(!$launcher -or !$candidate -or $candidate.Name -ne 'hw_server.exe' -or $candidate.ExecutablePath -ne 'D:\Xilinx_2020.2\Vitis\2020.2\bin\unwrapped\win64.o\hw_server.exe' -or $candidate.CreationDate.ToUniversalTime() -lt $launchedAt){return $null}
 $ancestor=$candidate
 for($i=0;$i -lt 5 -and $ancestor;$i++){if($ancestor.ParentProcessId -eq $launcher.Id){return @{pid=$candidate.ProcessId;parent=$candidate.ParentProcessId;ticks=$candidate.CreationDate.ToUniversalTime().Ticks}};$ancestor=Get-CimInstance Win32_Process -Filter "ProcessId=$($ancestor.ParentProcessId)"}
 return $null
}
try{
 $port.Open();$port.DiscardInBuffer()
 $launchedAt=[DateTime]::UtcNow
 $launcher=Start-Process D:/Xilinx_2020.2/Vitis/2020.2/bin/hw_server.bat -ArgumentList '-s','tcp::3121' -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/server.log" -RedirectStandardError "$run/server-error.log"
 try{$launcherTicks=$launcher.StartTime.ToUniversalTime().Ticks}catch{$launcherTicks=$null}
 $watch=[Diagnostics.Stopwatch]::StartNew()
 do{$listener=Get-NetTCPConnection -LocalPort 3121 -State Listen -ErrorAction SilentlyContinue|Select-Object -First 1;if(!$listener){Start-Sleep -Milliseconds 100};if($watch.Elapsed.TotalSeconds -gt 15){throw 'Restore server timeout'}}while(!$listener)
 $candidate=Get-CimInstance Win32_Process -Filter "ProcessId=$($listener.OwningProcess)"
 if($candidate.Name -ne 'hw_server.exe' -or $candidate.ExecutablePath -ne 'D:\Xilinx_2020.2\Vitis\2020.2\bin\unwrapped\win64.o\hw_server.exe'){throw 'Restore server identity mismatch'}
 $owned=OwnedRestoreServer $candidate
 if(!$owned){throw 'Restore server is unrelated'}
 & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat "$run/boot.tcl" *> "$run/boot.log"
 if($LASTEXITCODE -or !(Select-String "$run/boot.log" -Pattern '^ACCEPTEDM4B_RESTORED' -Quiet)){throw '50M temporary restore failed'}
 $watch.Restart()
 do{Pull;$ready=@(Get-TouchReadyLines $captured.ToString());if($watch.Elapsed.TotalSeconds -gt 40){throw '50M ready timeout'};if($ready.Count -lt 3){Start-Sleep -Milliseconds 50}}while($ready.Count -lt 3)
 foreach($d in $ready){if($d.Value -notmatch '(?:^| )locked=1(?: |$)' -or $d.Value -notmatch 'pixel_locked=1' -or $d.Value -notmatch 'dma_errors=00000000'){throw '50M restore video failure'}}
 if($captured.ToString() -notmatch 'conversion_hz=50000000[^\r\n]*ABI=00070000'){throw 'Wrong restored ADC platform'}
 Send-AcqCommands @('source 2','trigsource 2','trigger rise 500','triggermode auto','dual 1','cursors 0','acqrun');[void](Set-LongTimebase 5)
 $s=Wait-AcqState {param($x) $x.ui.running -eq '1' -and $x.ui.source -eq 'ADC2' -and $x.adc.display_rate -eq '50000000' -and $x.adc.display_epoch -eq $x.ui.epoch}
 $state.final=$s;$state.state='verified-restored-acceptedM4B'
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{
 $cleanup=@()
 try{if($port.IsOpen){Pull}}catch{$cleanup+=@($_.Exception.Message);$restoreLate=$_}
 try{if($port.IsOpen){$port.Close()}}catch{$cleanup+=@($_.Exception.Message);$restoreLate=$_}
 try{$port.Dispose()}catch{$cleanup+=@($_.Exception.Message);$restoreLate=$_}
 try{if($launcher -and $null -ne $launcherTicks){$p=Get-CimInstance Win32_Process -Filter "ProcessId=$($launcher.Id)";if($p -and $p.CreationDate.ToUniversalTime().Ticks -eq $launcherTicks){Stop-Process -Id $launcher.Id}}}catch{$cleanup+=@($_.Exception.Message);$restoreLate=$_}
 try{
  if(!$owned -and $launcher){foreach($candidate in @(Get-CimInstance Win32_Process -Filter "Name='hw_server.exe'")){$identity=OwnedRestoreServer $candidate;if($identity){$owned=$identity;break}}}
  if($owned){$p=Get-CimInstance Win32_Process -Filter "ProcessId=$($owned.pid)";if($p -and $p.Name -eq 'hw_server.exe' -and $p.ParentProcessId -eq $owned.parent -and $p.CreationDate.ToUniversalTime().Ticks -eq $owned.ticks){Stop-Process -Id $owned.pid;$state.server='owned-server-stopped'}}
 }catch{$cleanup+=@($_.Exception.Message);$restoreLate=$_}
 if($cleanup.Count){$state.state='failed';$state.cleanup_errors=$cleanup}
 $state.completed_utc=[DateTime]::UtcNow.ToString('o')
 $state|ConvertTo-Json -Depth 8|Set-Content "$run/session.json" -Encoding utf8
}
if($restoreLate){throw $restoreLate}
Write-Output "ACCEPTEDM4B_RESTORE_PASS: $run;noFlash"
