# Replaces volatile board state through JTAG; verify the target before running.
# Resets the board and loads RAM/PL. Never writes Flash/QSPI.
param([ValidateSet('standalone','freertos')][string]$OS='standalone')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
& D:/Python/python.exe -X utf8 (Join-Path $root 'scripts/validate_board_ui_build.py') $OS
if($LASTEXITCODE){throw 'Offline build validation failed'}
$id=Get-Date -Format 'yyyyMMdd-HHmmss-ffff'
$run="D:/FPGA_Project/SP2_LVGL_Build/board/$id-$OS"
New-Item -ItemType Directory -Path $run -Force | Out-Null
$elfName="scope_lvgl_$OS.elf"
Copy-Item (Join-Path $root "build/board-lvgl/$OS/$elfName") "$run/$elfName"
Copy-Item (Join-Path $root 'build/ps-screen/sp2_ps_screen.bit') "$run/platform.bit"
Copy-Item (Join-Path $root 'build/ps-screen/ps7_init.tcl') "$run/ps7_init.tcl"
Copy-Item (Join-Path $root 'firmware/board_init_helpers.tcl') "$run/board_init_helpers.tcl"
$hashes=Get-Item "$run/$elfName","$run/platform.bit","$run/ps7_init.tcl","$run/board_init_helpers.tcl" | Get-FileHash | Select-Object Path,Hash
$state=@{state='running';os=$OS;run=$run;artifacts=$hashes;flash_written=$false;screenshots=@()}
$sessionPath=Join-Path $root "build/board-lvgl/$OS/board-session.json"
$state | ConvertTo-Json -Depth 6 | Set-Content $sessionPath -Encoding utf8
$script:serialText=[System.Text.StringBuilder]::new()
$script:uartLog="$run/uart.log"
$port=[System.IO.Ports.SerialPort]::new('COM18',115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$port.ReadBufferSize=65536;$port.Handshake=[System.IO.Ports.Handshake]::None
$port.DtrEnable=$false;$port.RtsEnable=$false;$port.NewLine="`n"
$xsct='D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat'
function Pull-Uart {
  $text=$port.ReadExisting()
  if($text.Length){[void]$script:serialText.Append($text);[System.IO.File]::AppendAllText($script:uartLog,$text)}
  if($script:serialText.ToString() -match 'APP_FATAL|DDR_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT'){throw 'Board reported a fatal failure; inspect UART log'}
}
function Wait-Match([string]$pattern,[int]$mark,[double]$seconds=20) {
  $watch=[System.Diagnostics.Stopwatch]::StartNew()
  while($watch.Elapsed.TotalSeconds -lt $seconds) {
    Pull-Uart
    $tail=$script:serialText.ToString($mark,$script:serialText.Length-$mark)
    $match=[regex]::Match($tail,$pattern)
    if($match.Success){return $match.Value}
    Start-Sleep -Milliseconds 20
  }
  throw "UART timeout waiting for $pattern"
}
function Get-Status {
  $mark=$script:serialText.Length;$port.WriteLine('status')
  $line=Wait-Match 'UI_STATUS[^\r\n]+\r?\n' $mark
  $values=@{}
  foreach($m in [regex]::Matches($line,'(\w+)=([^\s]+)')){$values[$m.Groups[1].Value]=$m.Groups[2].Value}
  return $values
}
function Expect-State([hashtable]$expected) {
  $watch=[System.Diagnostics.Stopwatch]::StartNew()
  do {
    $actual=Get-Status;$ok=$true
    foreach($key in $expected.Keys){if("$($actual[$key])" -ne "$($expected[$key])"){$ok=$false}}
    if($ok){return $actual}
    Start-Sleep -Milliseconds 100
  }while($watch.Elapsed.TotalSeconds -lt 15)
  throw "UI state mismatch: expected $($expected|ConvertTo-Json -Compress); actual $($actual|ConvertTo-Json -Compress)"
}
function Tap([int]$x,[int]$y,[hashtable]$expected=@{}) {
  $mark=$script:serialText.Length;$port.WriteLine("tap $x $y")
  [void](Wait-Match "TAP_QUEUED x=$x y=$y\r?\n" $mark)
  $expected.touch_pending=0
  [void](Expect-State $expected)
}
function Invoke-Xsct([string]$scriptFile,[string]$log,[string]$marker) {
  & $xsct $scriptFile *> $log
  if($LASTEXITCODE -or -not (Select-String $log -Pattern $marker -Quiet)){throw "XSCT step did not pass: $log"}
}
function Export-Frame([string]$name) {
  $mark=$script:serialText.Length;$port.WriteLine('snapshot')
  $line=Wait-Match 'SNAPSHOT_READY[^\r\n]+\r?\n' $mark
  $m=[regex]::Match($line,'addr=(10000000|10200000) bytes=1843200 format=BGR888 crc=([0-9a-fA-F]{8})')
  if(-not $m.Success){throw "Invalid snapshot metadata: $line"}
  $address=$m.Groups[1].Value;$crc=$m.Groups[2].Value
  $script=@'
if {[catch {
connect -url tcp:127.0.0.1:3121
targets -set -filter {name == "APU"}
configparams force-mem-accesses 1
mrd -bin -file {@FILE@} 0x@ADDRESS@ 460800
puts "DDR_EXPORT_PASS"
disconnect
} e opts]} {puts stderr "EXPORT_ERROR: $e";exit 1}
exit
'@
  $script.Replace('@FILE@',"$run/$name.bin").Replace('@ADDRESS@',$address) | Set-Content "$run/export-$name.tcl" -Encoding ascii
  Invoke-Xsct "$run/export-$name.tcl" "$run/export-$name.log" '^DDR_EXPORT_PASS'
  & D:/Python/python.exe -X utf8 (Join-Path $root 'scripts/decode_board_frame.py') "$run/$name.bin" $crc "$run/$name.png"
  if($LASTEXITCODE){throw 'DDR frame CRC/decode failed'}
  $dest=Join-Path $root "artifacts/board-lvgl/$OS"
  New-Item -ItemType Directory -Path $dest -Force | Out-Null
  Copy-Item "$run/$name.png","$run/$name.json" -Destination $dest -Force
  $state.screenshots+=@{name=$name;address=$address;crc32=$crc;binary_sha256=(Get-FileHash "$run/$name.bin").Hash}
  $mark=$script:serialText.Length;$port.WriteLine('resume');[void](Wait-Match 'RENDER_RESUMED\r?\n' $mark)
}
$boot=@'
if {[catch {
connect -url tcp:127.0.0.1:3121
if {[string first "HelloFpga JTAG-SP2 YOUR_CABLE_SERIAL" [jtag targets]]<0} {error "Expected cable absent"}
targets -set -filter {name == "ARM Cortex-A9 MPCore #0"}
set boot [mrd -value 0xF800025C]
if {($boot & 7)!=0} {error "JTAG boot mode required"}
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
dow {@RUN@/@ELF@}
con
puts "APP_RAM_LOADED"
disconnect
} e opts]} {puts stderr "BOOT_ERROR: $e";puts stderr [dict get $opts -errorinfo];exit 1}
exit
'@
$boot.Replace('@RUN@',$run).Replace('@ELF@',$elfName) | Set-Content "$run/boot.tcl" -Encoding ascii
try {
  $port.Open()
  Invoke-Xsct "$run/boot.tcl" "$run/boot.log" '^APP_RAM_LOADED'
  [void](Wait-Match "LVGL_READY os=$OS[^\r\n]+\r?\n" 0 30)
  Write-Host "PASS: $OS LVGL booted on board"
  $first=0;$last=0
  for($i=0;$i -lt 3;$i++){
    $mark=$script:serialText.Length
    $line=Wait-Match 'APP_DIAG[^\r\n]+\r?\n' $mark
    $frame=[int]([regex]::Match($line,'frames=(\d+)').Groups[1].Value)
    if($i -eq 0){$first=$frame};$last=$frame
  }
  if($last -le $first){throw 'LVGL frame counter did not progress'}
  Export-Frame 'initial'
  Tap 906 100 @{running=0}
  Tap 900 192 @{wave=1}
  Tap 976 192 @{wave=2}
  Tap 938 272 @{time=3}
  Tap 938 356 @{scale=3}
  Tap 938 442 @{freq=2000}
  Tap 970 32
  Tap 500 196 @{lang='en';wave=2;time=3;scale=3;freq=2000;running=0}
  Tap 382 254 @{grid=0}
  Tap 382 312 @{bars=1}
  Tap 382 312 @{bars=0}
  Tap 382 254 @{grid=1}
  Tap 500 196 @{lang='zh-CN';running=0}
  Tap 650 422
  Export-Frame 'configured'
  Tap 906 100 @{running=1}
  $final=Expect-State @{lang='zh-CN';wave=2;time=3;scale=3;freq=2000;grid=1;bars=0;running=1;frozen=0}
  $state.final_ui=$final
  for($i=0;$i -lt 5;$i++){[void](Wait-Match 'APP_DIAG[^\r\n]+\r?\n' $script:serialText.Length)}
  Pull-Uart
  if($OS -eq 'freertos'){
    $lines=[regex]::Matches($script:serialText.ToString(),'APP_DIAG[^\r\n]+')
    $a=$lines[0].Value;$b=$lines[$lines.Count-1].Value
    foreach($field in @('heartbeat','tick')){
      if([int64]([regex]::Match($b,"$field=(\d+)").Groups[1].Value) -le [int64]([regex]::Match($a,"$field=(\d+)").Groups[1].Value)){throw "RTOS $field did not progress"}
    }
    $a=$lines[$lines.Count-5].Value
    $ticksA=[int64]([regex]::Match($a,'tick=(\d+)').Groups[1].Value)
    $ticksB=[int64]([regex]::Match($b,'tick=(\d+)').Groups[1].Value)
    $msA=[int64]([regex]::Match($a,'ms=(\d+)').Groups[1].Value)
    $msB=[int64]([regex]::Match($b,'ms=(\d+)').Groups[1].Value)
    $ratio=($ticksB-$ticksA)/[double]($msB-$msA)
    if($ratio -lt 0.99 -or $ratio -gt 1.01){throw "Kernel/global timer drift exceeds 1%: $ratio"}
    $state.tick_per_global_ms=$ratio
    Write-Host "PASS: RTOS kernel/global timer ratio $ratio"
  }
  $state.state='verified-board-no-panel'
  Write-Host "PASS: $OS pointer input, bilingual controls, DMA swaps, CRC-verified DDR screenshots"
}catch {$state.state='failed';$state.error=$_.Exception.Message;throw}
finally {
  try {
    if($port.IsOpen){
      try {Pull-Uart}catch {$state.state='failed';$state.error=$_.Exception.Message}
      $port.Close()
    }
  }finally {
    $port.Dispose()
    $state.uart_sha256=if(Test-Path $script:uartLog){(Get-FileHash $script:uartLog).Hash}else{''}
    $state | ConvertTo-Json -Depth 8 | Set-Content $sessionPath -Encoding utf8
  }
}
