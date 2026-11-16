[CmdletBinding()]
param(
    [string]$Launcher=(Join-Path $PSScriptRoot '../../generated/outputs/Release/KuboLauncher.exe'),
    [switch]$Sprite2D,
    [string]$UpdateSource
)
$ErrorActionPreference='Stop'
$Launcher=(Resolve-Path -LiteralPath $Launcher).Path
$reviewRoot=Join-Path $PSScriptRoot ('../../generated/hub-review/'+[guid]::NewGuid().ToString('N'))
$reviewRoot=[IO.Path]::GetFullPath($reviewRoot)
$reviewRoaming=Join-Path $reviewRoot 'profile/Roaming'
$reviewLocal=Join-Path $reviewRoot 'profile/Local'
New-Item -ItemType Directory -Path (Join-Path $reviewRoaming 'KuboEngine'),$reviewLocal | Out-Null
$reviewPaths=@()
$reviewNames=@('日本語の試作','Lighting Lab','Sandbox')
if($Sprite2D){$reviewNames=@('2D 試作')+$reviewNames}
foreach($reviewName in $reviewNames) {
    $reviewManifest=Join-Path $reviewRoot ('projects/'+$reviewName+'/'+$reviewName+'.kubo')
    $reviewCreateArgs=@('--create-project',('"'+$reviewManifest+'"'))
    if($Sprite2D -and $reviewName -eq '2D 試作'){$reviewCreateArgs+=@('--template','sprite2d')}
    $reviewCreateArgs+='--no-launch'
    $reviewCreation=Start-Process -FilePath $Launcher -ArgumentList $reviewCreateArgs -WindowStyle Hidden -Wait -PassThru
    if($reviewCreation.ExitCode -ne 0){throw "Review fixture creation failed: $reviewName"}
    $reviewPaths+=$reviewManifest
}
$reviewPaths+=(Join-Path $reviewRoot 'projects/Moved/Moved.kubo')
$reviewPaths | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $reviewRoaming 'KuboEngine/recent_projects.json') -Encoding UTF8
if($UpdateSource){
    [IO.File]::WriteAllText((Join-Path $reviewRoaming 'KuboEngine/hub_update_source.txt'),$UpdateSource,[Text.UTF8Encoding]::new($false))
}
$reviewStart=[Diagnostics.ProcessStartInfo]::new()
$reviewStart.FileName=$Launcher
$reviewStart.WorkingDirectory=Split-Path $Launcher -Parent
$reviewStart.UseShellExecute=$false
# Interactive preview, with an isolated Hub and Editor settings profile.
$reviewStart.EnvironmentVariables['APPDATA']=$reviewRoaming
$reviewStart.EnvironmentVariables['LOCALAPPDATA']=$reviewLocal
$reviewProcess=[Diagnostics.Process]::Start($reviewStart)
$reviewReceipt=[ordered]@{format='kubo-hub-review-session-1';root=$reviewRoot;launcher=$Launcher;pid=$reviewProcess.Id;startTimeUtc=$reviewProcess.StartTime.ToUniversalTime().ToString('o');roaming=$reviewRoaming;local=$reviewLocal;projects=$reviewPaths;note='Isolated manual UI review; not an automated UI pass'}
$reviewReceipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $reviewRoot 'session.json') -Encoding UTF8
$reviewReceipt | ConvertTo-Json -Depth 5
