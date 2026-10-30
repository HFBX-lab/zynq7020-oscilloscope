# MANUAL AUTHORIZED JTAG SESSION ONLY. Replaces volatile PL/RAM state; no Flash.
# Start hw_server separately after explicit user authorization, then invoke.
param([switch]$PrepareOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& D:/Python/python.exe -X utf8 (Join-Path $root 'scripts/validate_adc_ps.py')
if($LASTEXITCODE){throw 'ADC candidate provenance validation failed'}
$expected=Get-Content (Join-Path $root 'build/adc-soc/validation.json') -Raw | ConvertFrom-Json
$id=Get-Date -Format 'yyyyMMdd-HHmmss-ffff'
$run="D:/FPGA_Project/SP2_ADC_PS_Build/board/$id"
New-Item -ItemType Directory $run -Force | Out-Null
foreach($pair in @(
  @('build/adc-soc/sp2_adc_internal.bit','platform.bit'),
  @('build/adc-firmware/adc_internal_selftest.elf','application.elf'),
  @('build/adc-soc/ps7_init.tcl','ps7_init.tcl'),
  @('firmware/board_init_helpers.tcl','board_init_helpers.tcl'))){Copy-Item (Join-Path $root $pair[0]) "$run/$($pair[1])"}
$state=@{state='running';run=$run;mode='internal DIGITAL only';flash_written=$false;records=@();
  artifacts=@(Get-Item "$run/platform.bit","$run/application.elf","$run/ps7_init.tcl","$run/board_init_helpers.tcl" | Get-FileHash | Select-Object Path,Hash)}
foreach($item in @(@('platform.bit',$expected.bit_sha256),@('application.elf',$expected.elf_sha256),
                  @('ps7_init.tcl',$expected.ps7_init_sha256),@('board_init_helpers.tcl',$expected.board_init_helpers_sha256))){
  if((Get-FileHash "$run/$($item[0])").Hash -ne $item[1]){throw "Validated snapshot mismatch: $($item[0])"}
}
$state.loader_script_sha256=(Get-FileHash $PSCommandPath).Hash
if($PrepareOnly){
  $state.state='prepared-offline-not-run'
  $state | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $root 'build/adc-soc/prepared-session.json') -Encoding utf8
  Write-Host "ADC_PREPARED_ONLY: verified snapshots $run; no serial or JTAG connection"
  return
}
$statePath=Join-Path $root 'build/adc-soc/board-session.json'
$port=[IO.Ports.SerialPort]::new('COM18',115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$port.DtrEnable=$false;$port.RtsEnable=$false;$port.ReadBufferSize=65536
$text=[Text.StringBuilder]::new()
$lateFailure=$null
function Pull {
  $data=$port.ReadExisting()
  if($data.Length){[void]$text.Append($data);[IO.File]::AppendAllText("$run/uart.log",$data)}
  if($text.ToString() -match 'APP_FATAL|DDR_FAIL|ADC_[A-Z_]*FAIL'){throw 'Board application failure; inspect UART log'}
}
function Xsct([string]$script,[string]$log,[string]$marker){
  & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat $script *> $log
  if($LASTEXITCODE -or !(Select-String $log -Pattern $marker -Quiet)){throw "XSCT stage failed: $log"}
}
$boot=@'
if {[catch {
connect -url tcp:127.0.0.1:3121
if {[string first "HelloFpga JTAG-SP2 YOUR_CABLE_SERIAL" [jtag targets]]<0} {error "Expected cable absent"}
targets -set -filter {name == "ARM Cortex-A9 MPCore #0"}
set boot [mrd -value 0xF800025C]
if {($boot & 7)!=0} {error "JTAG boot mode required"}
puts "BOOT_MODE=$boot"
targets -set -filter {name == "APU"}
rst -system -stop
targets -set -filter {name == "xc7z020"}
fpga -file {@RUN@/platform.bit}
if {[fpga -state] ne "FPGA is configured"} {error "PL configuration failed"}
targets -set -filter {name == "ARM Cortex-A9 MPCore #0"}
rst -processor -clear-registers
source {@RUN@/ps7_init.tcl}
source {@RUN@/board_init_helpers.tcl}
sp2_init_ps_without_ddr
sp2_init_ddr_and_finish
ps7_post_config
dow {@RUN@/application.elf}
con
puts ADC_APP_RAM_LOADED
disconnect
} e opts]} {puts stderr "BOOT_ERROR: $e";puts stderr [dict get $opts -errorinfo];exit 1}
exit
'@
$boot.Replace('@RUN@',$run) | Set-Content "$run/boot.tcl" -Encoding ascii
try {
  $port.Open();$port.DiscardInBuffer()
  Xsct "$run/boot.tcl" "$run/boot.log" '^ADC_APP_RAM_LOADED'
  $clock=[Diagnostics.Stopwatch]::StartNew()
  while($text.ToString() -notmatch 'ADC_INTERNAL_SELFTEST_PASS:'){
    Pull
    if($clock.Elapsed.TotalSeconds -gt 60){throw 'UART application timeout'}
    Start-Sleep -Milliseconds 50
  }
  Pull
  if($text.ToString() -notmatch 'ADC_INTERNAL_TIMEOUT_PASS:'){throw 'Missing hardware timeout/recovery proof'}
  foreach($index in 0,1){
    $address=if($index -eq 0){'13000000'}else{'13004000'}
    $pattern="ADC_INTERNAL_RECORD_PASS: copy$index id=(\d+) address=$address bytes=16384 first_n=([0-9a-fA-F]+) crc32=([0-9a-fA-F]{8})"
    $match=[regex]::Match($text.ToString(),$pattern)
    if(!$match.Success){throw "Missing copy$index metadata"}
    $export=@'
if {[catch {
connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "APU"}
configparams force-mem-accesses 1
mrd -bin -file {@RUN@/copy@INDEX@.bin} 0x@ADDRESS@ 4096
puts ADC_RECORD_EXPORTED
disconnect
} e opts]} {puts stderr "EXPORT_ERROR: $e";exit 1}
exit
'@
    $export.Replace('@RUN@',$run).Replace('@INDEX@',[string]$index).Replace('@ADDRESS@',$address) | Set-Content "$run/export$index.tcl" -Encoding ascii
    Xsct "$run/export$index.tcl" "$run/export$index.log" '^ADC_RECORD_EXPORTED'
    $crc=$match.Groups[3].Value
    & D:/Python/python.exe -X utf8 (Join-Path $root 'scripts/verify_adc_internal_record.py') "$run/copy$index.bin" $crc --origin board-ddr
    if($LASTEXITCODE){throw 'Host record validation failed'}
    $state.records+=@{name="copy$index";address=$address;id=$match.Groups[1].Value;crc32=$crc;sha256=(Get-FileHash "$run/copy$index.bin").Hash}
  }
  $state.state='verified-board-internal-digital'
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{
  try{if($port.IsOpen){try{Pull}catch{$lateFailure=$_;$state.state='failed';if(!$state.error){$state.error=$_.Exception.Message}};$port.Close()}}
  finally{$port.Dispose();if(Test-Path "$run/uart.log"){$state.uart_sha256=(Get-FileHash "$run/uart.log").Hash};$state | ConvertTo-Json -Depth 8 | Set-Content $statePath -Encoding utf8}
}
if($lateFailure){throw $lateFailure}
Write-Host 'ADC_BOARD_INTERNAL_PASS: two PS-read DDR copies verified independently; not a real ADC test'
