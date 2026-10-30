# Runs inside the existing authorized UART/JTAG session; no new clock/platform.
function Test-ZeroMarkerControls {
 $prior=$state.long_records;$state.zero_marker_records=@();$state.zero_marker_checks=@();$state.long_records=@()
 try{
  Send-AcqCommands @('parameters 0','records 0','measurements 0','source 2','trigsource 2','trigger rise 500','triggermode auto','dual 1','cursors 0','acqrun')
  [void](Set-LongTimebase 6);$null=Wait-Highspeed50MSingle 6
  $base=Read-ParameterSnapshot;$state.zero_marker_baseline=$base
  $cases=@(
   @{name='zero-overlap';p1=0;p2=0;source=2;dual=1},
   @{name='zero-near-ch2-above';p1=0;p2=250;source=2;dual=1},
   @{name='zero-near-ch1-above';p1=250;p2=0;source=2;dual=1},
   @{name='zero-separated';p1=1500;p2=-1500;source=2;dual=1},
   @{name='zero-limits';p1=3500;p2=-3500;source=2;dual=1},
   @{name='zero-single-ch1';p1=0;p2=0;source=1;dual=0},
   @{name='zero-single-ch2';p1=0;p2=0;source=2;dual=0})
  foreach($case in $cases){
   Send-AcqCommands @("source $($case.source)","dual $($case.dual)","vertical 1 2 $($case.p1)","vertical 2 2 $($case.p2)")
   $before=Read-ParameterSnapshot;Assert-ParameterRawHeld $base $before 0
   [void](Export-LongRecord $case.name $true);$entry=$state.long_records[-1]
   $after=Read-ParameterSnapshot;Assert-ParameterRawHeld $before $after 0
   Assert-MemoryFields $base.parameter.status $after.parameter.status @('valid','blob_crc','wave_valid','wave_crc') 'Zero marker slots'
   if($after.ui.single_complete -ne '1' -or $entry.crc32 -ne $base.parameter.status.record_crc){throw 'Zero layout changed frozen single raw'}
   $state.zero_marker_checks+=@{test=$case.name;snapshot=$after};$entry.snapshot=$after
  }
  Send-AcqCommands @('source 2','dual 1','vertical 1 2 0','vertical 2 2 0')
  Tap-Parameter 948 32;Tap-Parameter 512 151;Tap-Parameter 659 491
  $before=Read-ParameterSnapshot;Assert-ParameterRawHeld $base $before 0
  [void](Export-LongRecord 'zero-overlap-en' $true);$after=Read-ParameterSnapshot;Assert-ParameterRawHeld $base $after 0
  if($after.ui.lang -ne 'en'){throw 'Marker English pointer did not work'}
  $state.long_records[-1].snapshot=$after;$state.zero_marker_checks+=@{test='zero-overlap-en';snapshot=$after}
  Tap-Parameter 948 32;Tap-Parameter 512 151;Tap-Parameter 659 491
  Send-AcqCommands @('vertical 1 1 500','vertical 2 3 -1000','source 2','dual 1','cursors 0','acqrun')
  $old=$base.ui.display_id
  $s=Wait-AcqState {param($x) [uint64]$x.ui.display_id -gt [uint64]$old -and $x.ui.running -eq '1' -and $x.ui.source -eq 'ADC2' -and $x.adc.display_rate -eq '6250000' -and $x.adc.display_epoch -eq $x.ui.epoch}
  $state.zero_marker_final=@{ui=$s.ui;adc=$s.adc;parameter=(Read-ParameterState)}
  Note-Acq 'zero-marker-final-CH2-live' $s
 }finally{$state.zero_marker_records=$state.long_records;$state.long_records=$prior}
}
