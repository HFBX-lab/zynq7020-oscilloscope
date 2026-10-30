$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. "$root/scripts/acq_ui_session_helpers.ps1"
. "$root/scripts/storage_session_helpers.ps1"
$good="TF_STATUS busy=0 ready=1 result=0 seq=12 id=1 kind=0`r`nTF_MEDIA cid=00000000000000000000000000000000 fat=3 sector_count=61067264`r`nTF_BLOB address=00400000 bytes=16512 crc=12abcdef`r`n"
$s=Convert-TfStatusFrame $good
if($s.status.seq -ne '12' -or $s.blob.bytes -ne '16512'){throw 'Parser did not bind complete frame'}
foreach($bad in @($good+$good,$good.Replace('TF_MEDIA','MISSING'),$good.Replace('busy=0','busy=2'),$good.Replace('kind=0','kind=2'),$good.Replace('result=0','result=9'),$good.Replace('bytes=16512','bytes=96'),$good.Replace('crc=12abcdef','crc=oops'))){
 $rejected=$false;try{$null=Convert-TfStatusFrame $bad}catch{$rejected=$true};if(!$rejected){throw 'Malformed TF frame accepted'}
}
Write-Output 'TF_STATUS_PARSER_PASS valid/duplicate/missing/flag/error/length/CRC'
$video='APP_DIAG ms=12000 frames=123 locked=1 pixel_locked=1 dma_errors=00000000'
$null=Assert-StorageVideoStatus $video
foreach($bad in @($video.Replace(' locked=1',' locked=0'),$video.Replace('dma_errors=00000000','dma_errors=000000001'),$video.Replace(' pixel_locked=1',''),$video.Replace('frames=123','frames=oops'))){$rejected=$false;try{$null=Assert-StorageVideoStatus $bad}catch{$rejected=$true};if(!$rejected){throw 'Invalid video diagnostic accepted'}}
Write-Output 'VIDEO_EXACT_FIELDS_PASS lock/pixel/error/missing/numeric'
