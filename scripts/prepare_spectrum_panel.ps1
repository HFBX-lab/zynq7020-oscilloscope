# Finish this stage with the spectrum range set to automatic; no acquisition change.
param([Parameter(Mandatory=$true)][string]$Run)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/acq_ui_session_helpers.ps1"
. "$PSScriptRoot/memory_session_helpers.ps1"
. "$PSScriptRoot/spectrum_session_helpers.ps1"
$runPath=(Resolve-Path -LiteralPath $Run).Path
$captured=[Text.StringBuilder]::new()
$port=[IO.Ports.SerialPort]::new('COM18',115200);$port.ReadBufferSize=65536
function Pull{$chunk=$port.ReadExisting();if($chunk){[void]$captured.Append($chunk);[IO.File]::AppendAllText("$runPath/panel-ready-uart.log",$chunk)}}
try{
 $port.Open();$port.DiscardInBuffer();$before=Read-AcqState;$fftBefore=Read-SpectrumState
 Send-AcqCommands @('fftrange auto')
 $after=Read-AcqState;$fftAfter=Read-SpectrumState
 Assert-MemoryFields $before.ui $after.ui @('epoch','running','source','time','trig_channel','trig_normal','trig_rising','level_mv','ch1_scale','ch2_scale','ch1_pos','ch2_pos') 'Panel preparation acquisition'
 Assert-SpectrumCacheHeld $fftBefore $fftAfter
 if($fftAfter.status.panel -ne '0' -or $fftAfter.status.auto_range -ne '1' -or $after.ui.running -ne '1'){throw 'Panel preparation did not finish closed/running/automatic'}
 @{state='verified-spectrum-panel-ready';before=$before;after=$after;fft_before=$fftBefore;fft_after=$fftAfter;hardware_reset=$false;flash_written=$false}|ConvertTo-Json -Depth 8|Set-Content "$runPath/panel-ready.json" -Encoding utf8
 Write-Output 'SPECTRUM_PANEL_READY: CH2/Hann/mean-on/automatic range; acquisition unchanged'
}finally{if($port.IsOpen){$port.Close()};$port.Dispose()}
