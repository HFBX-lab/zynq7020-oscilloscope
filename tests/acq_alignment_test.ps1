$ErrorActionPreference='Stop'
. "$PSScriptRoot/../scripts/acq_ui_session_helpers.ps1"
$good=@{ui=@{crossing='625.725';view_start='375.725'}}
$bad=@{ui=@{crossing='625.725';view_start='374.725'}}
Assert-AcqAlignment $good
$rejected=$false
try{Assert-AcqAlignment $bad}catch{$rejected=$true}
if(!$rejected){throw 'Misaligned crossing was not rejected'}
Write-Output 'ACQ_ALIGNMENT_TEST_PASS: 250-sample alignment accepts exact data and rejects1-sample error'
