param([ValidateSet('sim','all')][string]$Task='all',
      [string]$BuildRoot='D:\FPGA_Project\SP2_ADC_Build')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$id=Get-Date -Format 'yyyyMMdd-HHmmss-ffff'
$run=Join-Path $BuildRoot "runs\$id"
$stage=Join-Path $run 'source'
$stageTool=$stage.Replace('\','/')
$evidence=Join-Path $root 'build\adc'
New-Item -ItemType Directory -Path $stage,$evidence -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'rtl'),(Join-Path $PSScriptRoot 'sim') -Destination $stage -Recurse
Get-ChildItem -LiteralPath $PSScriptRoot -File | Copy-Item -Destination $stage
if(Test-Path (Join-Path $PSScriptRoot 'constraints')){
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'constraints') -Destination $stage -Recurse
}
$state=@{state='running';scope='offline-only';task=$Task;run=$run;board_test='not-run';sources=@()}
$state.pinmap_test_sha256=(Get-FileHash (Join-Path $root 'tests/adc_pinmap_test.py')).Hash
$state.sources=@(Get-ChildItem $stage -File -Recurse | ForEach-Object {
  @{file=$_.FullName.Substring($stage.Length+1).Replace('\','/');sha256=(Get-FileHash $_.FullName).Hash}
})
if(Test-Path "$evidence/status.json"){
  $history=Join-Path $evidence "history\$id";New-Item -ItemType Directory $history -Force | Out-Null
  Get-ChildItem $evidence -File | Move-Item -Destination $history
}
function Save-State {$state | ConvertTo-Json -Depth 8 | Set-Content "$evidence/status.json" -Encoding utf8}
function Run-Tool([string]$exe,[string[]]$arguments,[string]$log){
  & $exe @arguments *> $log
  if($LASTEXITCODE){Get-Content $log -Tail 20;throw "$exe failed: $log"}
}
Save-State
$xbin='D:\Xilinx_2020.2\Vivado\2020.2\bin'
$previousTemp=$env:TEMP;$previousTmp=$env:TMP
$taskTemp=Join-Path $run 'temp'
New-Item -ItemType Directory -Path $taskTemp -Force | Out-Null
$env:TEMP=$taskTemp;$env:TMP=$taskTemp
Push-Location $run
try {
  Run-Tool 'D:/Python/python.exe' @('-X','utf8',"$root/tests/adc_pinmap_test.py") 'pinmap-tests.log'
  Run-Tool "$xbin/xvlog.bat" @('--sv',"$stageTool/rtl/adc_snapshot_core.v","$stageTool/sim/tb_adc_snapshot.sv") 'compile.log'
  $cases=@(@{name='full';bits=12;warmup=4},@{name='minimum';bits=1;warmup=4},@{name='no_warmup';bits=4;warmup=0})
  foreach($case in $cases){
    $name=$case.name
    # cmd batch argument forwarding splits unquoted '='; retain literal quotes.
    $bitsArgument='"ADDR_BITS='+$case.bits+'"'
    $warmupArgument='"WARMUP='+$case.warmup+'"'
    Run-Tool "$xbin/xelab.bat" @('tb_adc_snapshot','-generic_top',$bitsArgument,'-generic_top',$warmupArgument,'-s',$name) "elaborate-$name.log"
    Run-Tool "$xbin/xsim.bat" @($name,'-tclbatch','source/run-sim.tcl','-log',"simulation-$name.log") "run-$name.log"
    if(!(Select-String "simulation-$name.log" -Pattern '^PASS: ADC snapshot' -Quiet) -or
       (Select-String "simulation-$name.log" -Pattern '^Fatal:|^ERROR:' -Quiet)){throw "Simulation failed: $name"}
    Select-String "simulation-$name.log" -Pattern '^PASS:' | ForEach-Object {$_.Line | Write-Host}
  }
  $state.simulation='passed'
  if($Task -eq 'all'){
    Run-Tool "$xbin/vivado.bat" @('-mode','batch','-source','source/offline.tcl','-log','vivado.log','-journal','vivado.jou') 'vivado-console.log'
    if(!(Select-String 'vivado.log' -Pattern '^ADC_OFFLINE_SYNTH_PASS' -Quiet)){throw 'No synthesis success marker'}
    $state.synthesis='passed'
    Run-Tool "$xbin/xvlog.bat" @('--sv','-d','POST_SYNTH','adc_snapshot_funcsim.v',"$stageTool/sim/tb_adc_snapshot.sv",'D:/Xilinx_2020.2/Vivado/2020.2/data/verilog/src/glbl.v') 'netlist-compile.log'
    Run-Tool "$xbin/xelab.bat" @('tb_adc_snapshot','glbl','-L','unisims_ver','-s','netlist') 'netlist-elaborate.log'
    Run-Tool "$xbin/xsim.bat" @('netlist','-tclbatch','source/run-sim.tcl','-log','simulation-netlist.log') 'netlist-run.log'
    if(!(Select-String 'simulation-netlist.log' -Pattern '^PASS: ADC snapshot' -Quiet) -or
       (Select-String 'simulation-netlist.log' -Pattern '^Fatal:|^ERROR:' -Quiet)){throw 'Post-synthesis functional simulation failed'}
    $state.netlist_simulation='passed'
    Write-Host 'PASS: post-synthesis functional netlist, full 4096-sample record'
  }else{$state.synthesis='not-run'}
  $state.artifacts=@(Get-ChildItem $run -File | Where-Object Extension -in '.log','.rpt','.csv','.dcp','.v' | ForEach-Object {
    @{file=$_.Name;sha256=(Get-FileHash $_.FullName).Hash}
  })
  $state.state='succeeded'
  Get-ChildItem $run -File | Where-Object Extension -in '.log','.rpt','.csv','.json' | Copy-Item -Destination $evidence -Force
}catch{$state.state='failed';$state.error=$_.Exception.Message;throw}
finally{Pop-Location;$env:TEMP=$previousTemp;$env:TMP=$previousTmp;Save-State}
Write-Host "ADC_OFFLINE_PASS: $Task; no JTAG connection, no bitstream, run=$run"
