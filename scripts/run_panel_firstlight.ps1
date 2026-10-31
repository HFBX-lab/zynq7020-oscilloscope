# Manual panel JTAG test; no Flash commands.
param([Parameter(Mandatory=$true)][ValidateSet('colorbars','lvgl')][string]$Stage)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& D:/Python/python.exe -X utf8 (Join-Path $root 'scripts/validate_ps_platform.py')
if($LASTEXITCODE){throw 'Display platform validation failed'}
if($Stage -eq 'lvgl'){
  & D:/Python/python.exe -X utf8 (Join-Path $root 'scripts/validate_board_ui_build.py') freertos
  if($LASTEXITCODE){throw 'FreeRTOS LVGL validation failed'}
}
$manifest=Get-Content (Join-Path $root 'build/panel-bringup/prepared.json') -Raw | ConvertFrom-Json
$image=@($manifest.images | Where-Object stage -eq $Stage)
if($image.Count -ne 1){throw 'Prepared stage missing or ambiguous'}
$image=$image[0];$run=$image.path
foreach($property in $image.files.PSObject.Properties){
  if((Get-FileHash (Join-Path $run $property.Name)).Hash -ne $property.Value){throw "Prepared file changed: $($property.Name)"}
}
if((Get-FileHash (Join-Path $root 'artifacts/panel-arrival-audit/audit.json')).Hash -ne $manifest.manual_audit_sha256){throw 'Manual pin audit changed'}
$hardware=Get-Content (Join-Path $root 'build/ps-screen/build-status.json') -Raw | ConvertFrom-Json
$software=Get-Content (Join-Path $root $(if($Stage -eq 'colorbars'){'build/ps-firmware/build-status.json'}else{'build/board-lvgl/freertos/build-status.json'})) -Raw | ConvertFrom-Json
$elfName=if($Stage -eq 'colorbars'){'screen_bringup.elf'}else{'scope_lvgl_freertos.elf'}
if($image.files.'platform.bit' -ne ($hardware.artifacts | Where-Object file -eq 'sp2_ps_screen.bit').sha256 -or
   $image.files.'application.elf' -ne ($software.artifacts | Where-Object file -eq $elfName).sha256){throw 'Prepared images differ from validated delivery'}
$port=[IO.Ports.SerialPort]::new('COM18',115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$port.DtrEnable=$false;$port.RtsEnable=$false;$port.ReadBufferSize=65536
$text=[Text.StringBuilder]::new()
$log=Join-Path $run 'panel-uart.log'
if(Test-Path $log){throw 'This prepared stage already has an execution log; preserve it and prepare a new run'}
$state=@{state='running';stage=$Stage;run=$run;physical_panel='awaiting-user-observation';touch='not-implemented';
  real_adc='not-run';flash_written=$false;images=$image.files;loader_sha256=(Get-FileHash $PSCommandPath).Hash}
$late=$null
function Pull {
  $data=$port.ReadExisting()
  if($data.Length){[void]$text.Append($data);[IO.File]::AppendAllText($log,$data)}
  if($text.ToString() -match 'APP_FATAL|DDR_FAIL|BRINGUP_STOP|FRAMEBUFFER_DDR_READBACK_FAIL|VIDEO_PRESENT_FAIL|VIDEO_STOP_TIMEOUT'){throw 'Panel program reported failure; inspect UART log'}
}
try {
  $port.Open();$port.DiscardInBuffer()
  & D:/Xilinx_2020.2/Vitis/2020.2/bin/xsct.bat (Join-Path $run 'boot.tcl') *> (Join-Path $run 'panel-boot.log')
  if($LASTEXITCODE -or !(Select-String (Join-Path $run 'panel-boot.log') -Pattern '^PANEL_APP_RAM_LOADED' -Quiet)){throw 'Panel JTAG load failed'}
  $watch=[Diagnostics.Stopwatch]::StartNew()
  do {
    Pull
    if($Stage -eq 'colorbars'){$ready=$text.ToString() -match 'VIDEO_INTERNAL_LOCK_STABLE:'}
    else{$ready=($text.ToString() -match 'LVGL_READY os=freertos') -and ([regex]::Matches($text.ToString(),'APP_DIAG os=freertos').Count -ge 3)}
    if($watch.Elapsed.TotalSeconds -gt 45){throw 'Panel UART readiness timeout'}
    if(!$ready){Start-Sleep -Milliseconds 50}
  }while(!$ready)
  if($Stage -eq 'colorbars'){
    if($text.ToString() -notmatch 'FB0_COLORBARS_CRC32=db424875 FB1_GRID_CRC32=6a57b038'){throw 'Unexpected colorbar framebuffer CRC'}
    $lines=[regex]::Matches($text.ToString(),'VIDEO_DIAG:[^\r\n]+')
  }else{$lines=[regex]::Matches($text.ToString(),'APP_DIAG os=freertos[^\r\n]+')}
  if($lines.Count -lt 3){throw 'Insufficient diagnostic samples'}
  foreach($line in $lines){
    if($line.Value -notmatch 'locked=1' -or $line.Value -notmatch 'dma_errors=00000000' -or
       $line.Value -notmatch 'pixel_(clock_)?locked=1'){throw "Invalid display diagnostic: $line"}
  }
  $state.state='internal-display-check-passed-awaiting-physical-feedback'
  $state.diagnostic_samples=$lines.Count
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{
  try{if($port.IsOpen){try{Pull}catch{$late=$_;$state.state='failed';if(!$state.error){$state.error=$_.Exception.Message}};$port.Close()}}
  finally{
    $port.Dispose()
    if(Test-Path $log){$state.uart_sha256=(Get-FileHash $log).Hash}
    $state | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $root "build/panel-bringup/$Stage-session.json") -Encoding utf8
  }
}
if($late){throw $late}
Write-Host "PANEL_INTERNAL_READY: $Stage; actual LCD appearance requires user feedback; no Flash written"
