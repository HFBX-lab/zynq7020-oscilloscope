function Convert-AcqFields([string]$Line){
 $values=@{};foreach($m in [regex]::Matches($Line,'(\w+)=([^\s]+)')){$values[$m.Groups[1].Value]=$m.Groups[2].Value};return $values
}
function Invoke-AcqCommand([string]$Command,[string]$Pattern){
 $start=$captured.Length;$port.Write($Command+"`n");$timer=[Diagnostics.Stopwatch]::StartNew()
 do {
  Pull;$fresh=$captured.ToString().Substring($start);$match=[regex]::Match($fresh,$Pattern)
  if($timer.Elapsed.TotalSeconds -gt 3){throw "Command acknowledgement timeout: $Command"}
  if(!$match.Success){Start-Sleep -Milliseconds 20}
 }while(!$match.Success)
 return $match.Value
}
function Send-AcqCommands([string[]]$Commands){
 [void](Invoke-AcqCommand '__query_boundary__' '(?m)^COMMANDS:[^\r\n]+\r?\n')
 foreach($command in $Commands){
  $pattern=if($command.StartsWith('tap ')){'(?m)^TAP_(?:QUEUED|REJECTED)[^\r\n]*\r?\n'}elseif($command -eq 'resume'){'(?m)^RENDER_RESUMED\r?\n'}else{'(?m)^UI_STATUS [^\r\n]+\r?\n'}
  $ack=Invoke-AcqCommand $command $pattern
  if($ack -match 'TAP_REJECTED'){throw 'LVGL simulated pointer was rejected'}
 }
}
function Convert-AcqStatusFrame([string]$Fresh){
 # Unknown commands produce COMMANDS help. Two complete help responses frame
 # the intervening read-only queries without requiring another firmware build.
 $bounds=[regex]::Matches($Fresh,'(?m)^COMMANDS:[^\r\n]+\r?\n')
 if($bounds.Count -lt 2){return $null}
 $begin=$bounds[0].Index+$bounds[0].Length;$end=$bounds[1].Index
 $body=$Fresh.Substring($begin,$end-$begin)
 $u=[regex]::Matches($body,'(?m)^UI_STATUS [^\r\n]+\r?\n')
 $a=[regex]::Matches($body,'(?m)^ADC_LIVE_STATUS [^\r\n]+\r?\n')
 if(!$u.Count -or !$a.Count){throw 'Framed acquisition query missing complete state lines'}
 $ui=Convert-AcqFields $u[$u.Count-1].Value;$adc=Convert-AcqFields $a[$a.Count-1].Value
 foreach($key in @('source','display_id','seen_id','trig_normal','trig_rising','level_mv','triggered','view_start','crossing','waiting','single_armed','single_complete','epoch')){if(!$ui.ContainsKey($key)){throw "Missing UI status field: $key"}}
 if($adc.available -ne '1' -or !$adc.ContainsKey('display_crc')){throw 'Acquisition/display metadata unavailable'}
 return @{ui=$ui;adc=$adc}
}
function Read-AcqState {
 $begin=Invoke-AcqCommand '__query_boundary__' '(?m)^COMMANDS:[^\r\n]+\r?\n'
 # At most36 input bytes after the opening ACK, below the PS UART64-byte FIFO.
 $start=$captured.Length;$port.Write("status`nadcstatus`n__query_boundary__`n");$timer=[Diagnostics.Stopwatch]::StartNew()
 do {
  Pull;$fresh=$captured.ToString().Substring($start);$result=Convert-AcqStatusFrame ($begin+$fresh)
  if($timer.Elapsed.TotalSeconds -gt 3){throw 'Acquisition status query timeout'}
  if($null -eq $result){Start-Sleep -Milliseconds 25}
 }while($null -eq $result)
 return $result
}
function Wait-AcqState([scriptblock]$Condition,[int]$Seconds=6){
 $timer=[Diagnostics.Stopwatch]::StartNew()
 do {
  $current=Read-AcqState
  if(& $Condition $current){return $current}
  if($timer.Elapsed.TotalSeconds -gt $Seconds){throw ('Acquisition state wait failed: '+($current|ConvertTo-Json -Compress))}
  Start-Sleep -Milliseconds 60
 }while($true)
}
function Note-Acq([string]$Name,$Current){
 $state.controls+=@{test=$Name;ui=$Current.ui;adc=$Current.adc}
 Write-Output "ACQ_CONTROL_PASS: $Name"
}
function Assert-AcqAlignment($Current){
 if([math]::Abs([double]$Current.ui.crossing-[double]$Current.ui.view_start-250) -gt 0.004){throw '20percent trigger alignment wrong'}
}
function Test-AcqUiBeforeExport {
 Send-AcqCommands @('source 2','trigsource 2','trigger rise 500','triggermode auto','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC2' -and $x.ui.running -eq '1' -and $x.ui.triggered -eq '1' -and $x.ui.trig_rising -eq '1'}
 Assert-AcqAlignment $s
 Note-Acq 'rising-auto-alignment' $s
 Send-AcqCommands @('trigger fall 500')
 $s=Wait-AcqState {param($x) $x.ui.trig_rising -eq '0' -and $x.ui.triggered -eq '1'}
 Assert-AcqAlignment $s
 Note-Acq 'falling-auto-alignment' $s
}
function Test-AcqUiAfterExport {
 $state.resume_needed=$false
 Send-AcqCommands @('trigger rise 4500','triggermode normal','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.trig_normal -eq '1' -and $x.ui.waiting -eq '1' -and $x.ui.level_mv -eq '4500'}
 $held=$s.ui.display_id;$heldCrc=$s.adc.display_crc;$raw=[uint32]$s.adc.id
 $t=if($HardwareTrigger){Wait-HardwareWaitingProgress}else{Wait-AcqState {param($x) [uint32]$x.adc.id -ge $raw+3}}
 if($t.ui.display_id -ne $held -or $t.adc.display_crc -ne $heldCrc -or $t.ui.running -ne '1'){throw 'NORMAL wait did not protect held record'}
 Note-Acq $(if($HardwareTrigger){'normal-ring-wait-keeps-raw-and-display-while-samples-advance'}else{'normal-wait-keeps-display-CRC-while-raw-advances'}) $t
 Send-AcqCommands @('single')
 $s=Wait-AcqState {param($x) $x.ui.single_armed -eq '1' -and $x.ui.single_complete -eq '0' -and $x.ui.waiting -eq '1'}
 $raw=[uint32]$s.adc.id
 $t=if($HardwareTrigger){Wait-HardwareWaitingProgress}else{Wait-AcqState {param($x) [uint32]$x.adc.id -ge $raw+2 -and $x.ui.single_armed -eq '1'}}
 if($t.ui.display_id -ne $held -or $t.adc.display_crc -ne $heldCrc){throw 'Untriggered SINGLE changed held record'}
 Note-Acq 'single-waits-for-valid-level' $t
 $armedEpoch=[uint32]$t.ui.epoch;$seen=[uint32]$t.ui.seen_id
 Send-AcqCommands @('trigger rise 500')
 $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and $x.ui.running -eq '0' -and $x.ui.single_armed -eq '0'}
 if([uint32]$s.ui.epoch -le $armedEpoch -or [uint32]$s.ui.display_id -le $seen -or $s.ui.triggered -ne '1'){throw 'Single did not publish a new triggered record'}
 Note-Acq 'single-completes-after-new-epoch' $s
 $singleId=$s.ui.display_id;$singleRaw=$s.adc.id;$singleCrc=$s.adc.display_crc
 $watch=[Diagnostics.Stopwatch]::StartNew();while($watch.Elapsed.TotalSeconds -lt 1.2){Pull;Start-Sleep -Milliseconds 50}
 $s=Read-AcqState
 if($s.ui.display_id -ne $singleId -or $s.adc.id -ne $singleRaw -or $s.adc.display_crc -ne $singleCrc -or $s.ui.running -ne '0'){throw 'Completed single continued acquisition or changed data'}
 Note-Acq 'single-ID-and-CRC-stable' $s
 Send-AcqCommands @('triggermode auto','acqrun')
 $s=Wait-AcqState {param($x) [uint32]$x.ui.display_id -gt [uint32]$singleId -and $x.ui.running -eq '1' -and $x.ui.single_complete -eq '0'}
 Note-Acq 'resume-after-single-new-record' $s
 Send-AcqCommands @('acqstop')
 $s=Wait-AcqState {param($x) $x.ui.running -eq '0'};$pausedId=$s.ui.display_id;$pausedCrc=$s.adc.display_crc
 $watch.Restart();while($watch.Elapsed.TotalSeconds -lt .7){Pull;Start-Sleep -Milliseconds 50}
 $s=Read-AcqState
 if($s.ui.display_id -ne $pausedId -or $s.adc.display_crc -ne $pausedCrc){throw 'Manual pause did not preserve display'}
 Note-Acq 'manual-stop-preserves-display' $s
 $beforeSource=[uint32]$s.ui.seen_id
 Send-AcqCommands @('acqrun','source 1','trigsource 1')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC1' -and [uint32]$x.ui.seen_id -gt $beforeSource -and $x.ui.triggered -eq '0' -and $x.ui.running -eq '1'}
 Note-Acq 'ADC1-source-switch-new-record-without-trigger' $s
 Send-AcqCommands @('source 3')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'SIM'};Note-Acq 'simulation-source' $s
 Send-AcqCommands @('single')
 $s=Read-AcqState;if($s.ui.single_armed -ne '0'){throw 'Simulation incorrectly armed physical single'}
 Note-Acq 'single-rejected-for-simulation' $s
 Send-AcqCommands @('source 2','trigsource 2','trigger rise 500','triggermode auto','acqrun')
 $s=Wait-AcqState {param($x) $x.ui.source -eq 'ADC2' -and $x.ui.triggered -eq '1' -and $x.ui.running -eq '1'}
 $beforeTap=[uint32]$s.ui.display_id
 Send-AcqCommands @('tap 845 32')
 $s=Wait-AcqState {param($x) $x.ui.single_complete -eq '1' -and [uint32]$x.ui.display_id -gt $beforeTap}
 Note-Acq 'LVGL-simulated-pointer-single-button' $s
 $tappedSingle=[uint32]$s.ui.display_id
 Send-AcqCommands @('tap 905 100')
 $s=Wait-AcqState {param($x) $x.ui.running -eq '1' -and [uint32]$x.ui.display_id -gt $tappedSingle}
 Note-Acq 'LVGL-simulated-pointer-run-button' $s
 Note-Acq 'final-ADC2-auto-running' $s
}
