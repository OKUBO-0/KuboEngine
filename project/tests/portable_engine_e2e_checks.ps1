param([Parameter(Mandatory=$true)][string]$EngineDirectory,[string]$NativeSdkDirectory,[string]$SelectedEditor,[string]$ExpectedEngineVersion,[switch]$RollbackToBundled,[string]$ExpectedBundledEngineVersion,[switch]$KeepFixture)
$ErrorActionPreference='Stop'
function Require($value,[string]$message) {if(!$value) {throw $message}}
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class HubTestWindow {
 delegate bool Callback(IntPtr w,IntPtr s);
 [DllImport("user32.dll")] static extern bool EnumWindows(Callback c,IntPtr s);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr w,StringBuilder n,int c);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetWindowTextW(IntPtr w,StringBuilder n,int c);
 [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr w,uint m,IntPtr a,IntPtr b);
 [DllImport("user32.dll")] static extern IntPtr GetDlgItem(IntPtr w,int id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr SendMessageW(IntPtr w,uint m,IntPtr a,IntPtr b);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] static extern IntPtr ReadControlText(IntPtr w,uint m,IntPtr c,StringBuilder n);
 [DllImport("user32.dll")] static extern IntPtr GetWindowLongPtrW(IntPtr w,int index);
 [DllImport("user32.dll")] static extern IntPtr SendMessageTimeoutW(IntPtr w,uint m,IntPtr a,IntPtr b,uint flags,uint timeout,out IntPtr result);
 public static IntPtr Find(uint pid,string cls) {
  IntPtr found=IntPtr.Zero;EnumWindows((w,s)=> {uint p;GetWindowThreadProcessId(w,out p);if(p!=pid)return true;
   var n=new StringBuilder(128);GetClassNameW(w,n,n.Capacity);if(n.ToString()!=cls)return true;found=w;return false;},IntPtr.Zero);return found;
 }
 public static bool Close(IntPtr w) {return PostMessageW(w,0x10,IntPtr.Zero,IntPtr.Zero);}
 public static string Describe(uint pid) {
  var result=new StringBuilder();EnumWindows((w,s)=>{uint p;GetWindowThreadProcessId(w,out p);if(p!=pid)return true;
   var cls=new StringBuilder(128);var title=new StringBuilder(512);GetClassNameW(w,cls,cls.Capacity);GetWindowTextW(w,title,title.Capacity);
   result.Append(" hwnd=").Append(w).Append(" class=").Append(cls).Append(" title=").Append(title);return true;},IntPtr.Zero);return result.ToString();
 }
 public static int RecentCount(IntPtr w) {return SendMessageW(GetDlgItem(w,100),0x18b,IntPtr.Zero,IntPtr.Zero).ToInt32();}
 public static bool HasNativeCreateButton(IntPtr w) {return GetDlgItem(w,105)!=IntPtr.Zero;}
 static IntPtr EngineControl(IntPtr w,int id) {var viewport=GetDlgItem(w,245);return GetDlgItem(viewport==IntPtr.Zero?w:viewport,id);}
 public static int EngineSelection(IntPtr w) {return SendMessageW(EngineControl(w,108),0x147,IntPtr.Zero,IntPtr.Zero).ToInt32();}
 public static bool SelectBundled(IntPtr w) {IntPtr result;var combo=EngineControl(w,108);return SendMessageTimeoutW(combo,0x14e,IntPtr.Zero,IntPtr.Zero,2,1000,out result)!=IntPtr.Zero && PostMessageW(w,0x111,new IntPtr(108|(1<<16)),combo);}
 public static string ProjectDetails(IntPtr w) {var n=new StringBuilder(1024);ReadControlText(GetDlgItem(w,106),0xD,new IntPtr(n.Capacity),n);return n.ToString();}
 public static bool DetailsReadOnly(IntPtr w) {var d=GetDlgItem(w,106);return d!=IntPtr.Zero && (GetWindowLongPtrW(d,-16).ToInt64()&0x800)!=0;}
 public static string EngineDetails(IntPtr w) {var n=new StringBuilder(1024);ReadControlText(EngineControl(w,110),0xD,new IntPtr(n.Capacity),n);return n.ToString();}
 public static bool EngineDetailsReadOnly(IntPtr w) {var d=EngineControl(w,110);return d!=IntPtr.Zero && (GetWindowLongPtrW(d,-16).ToInt64()&0x800)!=0;}
}
'@
$hubRoot=[IO.Path]::GetFullPath($EngineDirectory)
if($RollbackToBundled){Require (![string]::IsNullOrWhiteSpace($SelectedEditor)) 'Rollback probe requires an external Editor';Require (![IO.Path]::GetFullPath($SelectedEditor).Equals((Join-Path $hubRoot 'KuboEditor.exe'),[StringComparison]::OrdinalIgnoreCase)) 'Rollback must exercise two distinct executable paths'}
$hubFixture=Join-Path ([IO.Path]::GetTempPath()) ('kubo-portable-hub-'+[guid]::NewGuid().ToString('N'))
$hubLauncher=$null;$hubEditor=$null;$hubPlayer=$null
$hubSucceeded=$false;$script:hubEditorSequence=0
Write-Output "PORTABLE_HUB_FIXTURE=$hubFixture"
function Start-HubEditor([string]$manifest) {
    $script:hubEditorSequence++
    Start-Process -FilePath (Join-Path $hubRoot 'KuboEditor.exe') -ArgumentList ('--project "'+$manifest+'"') -WorkingDirectory (Join-Path $hubFixture 'Unrelated') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $hubFixture ("editor-$script:hubEditorSequence.stdout.log")) -RedirectStandardError (Join-Path $hubFixture ("editor-$script:hubEditorSequence.stderr.log"))
}
function Wait-HubWindow($process,[string]$class) {
    for($attempt=0;$attempt -lt 150;$attempt++) {
        if($process.HasExited) {throw "Owned process exited before $class appeared"}
        $window=[HubTestWindow]::Find($process.Id,$class)
        if($window -ne [IntPtr]::Zero) {return $window}
        Start-Sleep -Milliseconds 100
    };throw "Owned window timeout: $class"
}
function Close-HubProcess($process,$window) {
    Write-Output "Closing owned $($process.ProcessName) pid=$($process.Id)"
    Require ([HubTestWindow]::Close($window)) 'Owned native close failed'
    if(!$process.WaitForExit(15000)) {
        Write-Output "PORTABLE_CLOSE_TIMEOUT_WINDOWS=$([HubTestWindow]::Describe([uint32]$process.Id))"
        try {Write-Output "PORTABLE_CLOSE_TIMEOUT_CPU_MS=$($process.TotalProcessorTime.TotalMilliseconds) THREADS=$($process.Threads.Count)"}catch{Write-Output 'PORTABLE_CLOSE_TIMEOUT_PROCESS_DIAGNOSTIC_UNAVAILABLE=1'}
        throw "Owned native close timed out: $($process.ProcessName) pid=$($process.Id)"
    }
    $process.WaitForExit();Require ($process.ExitCode -eq 0) "Owned process did not exit normally: $($process.ExitCode)"
}
function Create-HubGeneric([string]$manifest,[int]$expectedExit=0) {
    $creation=Start-Process -FilePath (Join-Path $hubRoot 'KuboLauncher.exe') -ArgumentList @('--create-project',('"'+$manifest+'"'),'--no-launch') -WindowStyle Hidden -PassThru
    try {
        $handle=$creation.Handle
        Require ($handle -ne [IntPtr]::Zero -and $creation.WaitForExit(30000)) 'Portable Hub generic creation timed out'
        $creation.WaitForExit();Require ($creation.ExitCode -eq $expectedExit) "Portable Hub generic creation returned $($creation.ExitCode), expected $expectedExit"
    } finally {if(!$creation.HasExited){Stop-Process -Id $creation.Id;$creation.WaitForExit()};$creation.Dispose()}
}
try {
    $hubName=-join ([char[]]@(0x65e5,0x672c,0x8a9e))
    $genericRoot=Join-Path $hubFixture ($hubName+' Generic Project');$genericManifest=Join-Path $genericRoot ($hubName+'.kubo')
    Create-HubGeneric $genericManifest
    Require (Test-Path -LiteralPath (Join-Path $genericRoot 'Assets\Scenes\Main.scene')) 'Hub generic creation omitted default Scene'
    $genericBytes=[IO.File]::ReadAllText($genericManifest);Create-HubGeneric $genericManifest 1
    Require ([IO.File]::ReadAllText($genericManifest) -ceq $genericBytes) 'Repeated generic creation overwrote manifest'
    $occupiedRoot=Join-Path $hubFixture 'Occupied';New-Item -ItemType Directory -Path $occupiedRoot -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $occupiedRoot 'user.txt'),'preserve');Create-HubGeneric (Join-Path $occupiedRoot 'Refused.kubo') 1
    Require ([IO.File]::ReadAllText((Join-Path $occupiedRoot 'user.txt')) -ceq 'preserve') 'Hub nonempty refusal changed user data'
    Require (!(Test-Path -LiteralPath (Join-Path $occupiedRoot 'Assets'))) 'Hub nonempty refusal partially created Assets'
    Require (@(Get-ChildItem -LiteralPath $hubFixture -Force | Where-Object {$_.Name -like '.kubo-project-stage-*' -or $_.Name -like '*.kubo-create-journal.json'}).Count -eq 0) 'Generic creation leaked live stage/journal'
    New-Item -ItemType Directory -Path (Join-Path $hubFixture 'Unrelated') -Force | Out-Null
    $hubEditor=Start-HubEditor $genericManifest
    $hubEditorHandle=$hubEditor.Handle;$genericWindow=Wait-HubWindow $hubEditor 'WindowClass';Close-HubProcess $hubEditor $genericWindow
    Write-Output 'PASS: actual packaged Hub generic staged creation, Unicode default template -> Editor, repeated/nonempty refusal and no live stage/journal (not physical New dialog/crash QA)'
    $hubProject=Join-Path $hubFixture ($hubName+' Project')
    if(![string]::IsNullOrWhiteSpace($NativeSdkDirectory)) {
        $hubCreationManifest=Join-Path $hubProject ($hubName+'.kubo')
        $hubCreateProcess=Start-Process -FilePath (Join-Path $hubRoot 'KuboLauncher.exe') -ArgumentList @('--create-native',('"'+$hubCreationManifest+'"'),'--sdk',('"'+[IO.Path]::GetFullPath($NativeSdkDirectory)+'"'),'--no-launch') -WindowStyle Hidden -PassThru
        try {
            $hubCreateHandle=$hubCreateProcess.Handle
            Require ($hubCreateHandle -ne [IntPtr]::Zero -and $hubCreateProcess.WaitForExit(30000)) 'Portable Hub native creation timed out'
            $hubCreateProcess.WaitForExit();Require ($hubCreateProcess.ExitCode -eq 0) 'Portable Hub native creation failed'
            Require (Test-Path -LiteralPath (Join-Path $hubProject 'Native\Starter.vcxproj')) 'Portable Hub omitted native project'
        } finally {if(!$hubCreateProcess.HasExited) {Stop-Process -Id $hubCreateProcess.Id;$hubCreateProcess.WaitForExit()};$hubCreateProcess.Dispose()}
    }
    $hubAssetName=$hubName+'/Content';$hubSceneName=$hubName+'/Scenes/'+$hubName+'.scene'
    New-Item -ItemType Directory -Path (Join-Path $hubProject $hubAssetName),(Split-Path -Parent (Join-Path $hubProject $hubSceneName)),(Join-Path $hubFixture 'AppData'),(Join-Path $hubFixture 'Unrelated') -Force | Out-Null
    $hubManifest=Join-Path $hubProject ($hubName+'.kubo')
    $hubManifestText=@{name='HubTest';engineVersion='0.1.0';startupScene=$hubSceneName;assetDirectory=$hubAssetName} | ConvertTo-Json -Compress
    [IO.File]::WriteAllText($hubManifest,$hubManifestText)
    [IO.File]::WriteAllText((Join-Path $hubProject $hubSceneName),'{"version":1,"entities":[]}')
    $hubLegacyLayout="[Window][UnicodeMigrationMarker]`nPos=13,27`nSize=222,333`nCollapsed=1`n"
    [IO.File]::WriteAllText((Join-Path $hubProject 'imgui.ini'),$hubLegacyLayout)
    $hubStart=New-Object Diagnostics.ProcessStartInfo
    $hubStart.FileName=Join-Path $hubRoot 'KuboLauncher.exe';$hubStart.Arguments='--project "'+$hubManifest+'"'
    $hubStart.WorkingDirectory=Join-Path $hubFixture 'Unrelated';$hubStart.UseShellExecute=$false;$hubStart.CreateNoWindow=$true;$hubStart.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
    $hubStart.EnvironmentVariables['APPDATA']=Join-Path $hubFixture 'AppData'
    $hubExpectedEditor=Join-Path $hubRoot 'KuboEditor.exe'
    if(![string]::IsNullOrWhiteSpace($SelectedEditor)) {
        $hubExpectedEditor=[IO.Path]::GetFullPath($SelectedEditor)
        Require (Test-Path -LiteralPath $hubExpectedEditor -PathType Leaf) 'Selected Editor fixture is missing'
        Require ((Split-Path -Leaf $hubExpectedEditor) -ieq 'KuboEditor.exe') 'Selected Editor probe expects KuboEditor.exe'
        $hubCatalog=Join-Path $hubFixture 'AppData\KuboEngine\engine_catalog.json'
        New-Item -ItemType Directory -Path (Split-Path -Parent $hubCatalog) -Force | Out-Null
        $hubCatalogBytes=ConvertTo-Json -InputObject @('kubo-engine-catalog-1',$hubExpectedEditor,$hubExpectedEditor) -Compress
        [IO.File]::WriteAllText($hubCatalog,$hubCatalogBytes)
    }
    $hubLauncher=[Diagnostics.Process]::Start($hubStart);$hubLauncherHandle=$hubLauncher.Handle
    $hubWindow=Wait-HubWindow $hubLauncher 'KuboLauncherWindow'
    Require ([HubTestWindow]::HasNativeCreateButton($hubWindow)) 'Packaged Hub lacks native project create control'
    $hubChild=$null
    for($attempt=0;$attempt -lt 100;$attempt++) {
        $hubCandidate=Get-CimInstance Win32_Process -Filter "ParentProcessId=$($hubLauncher.Id) AND Name='KuboEditor.exe'" | Select-Object -First 1
        if($hubCandidate -and ![string]::IsNullOrWhiteSpace($hubCandidate.ExecutablePath)) {$hubChild=$hubCandidate;break};Start-Sleep -Milliseconds 100
    }
    Require ($hubChild -and [IO.Path]::GetFullPath($hubChild.ExecutablePath) -eq $hubExpectedEditor) 'Launcher did not launch the explicitly expected Editor'
    if(![string]::IsNullOrWhiteSpace($SelectedEditor)) {
        Require ([HubTestWindow]::EngineSelection($hubWindow) -eq 1) 'Persisted engine choice is not selected in actual Hub control'
        Require ([IO.File]::ReadAllText($hubCatalog) -ceq $hubCatalogBytes) 'Launching changed catalog bytes'
    }
    $hubEditor=[Diagnostics.Process]::GetProcessById($hubChild.ProcessId);$hubEditorHandle=$hubEditor.Handle
    $hubEditorWindow=Wait-HubWindow $hubEditor 'WindowClass'
    $hubRecent=Join-Path $hubFixture 'AppData\KuboEngine\recent_projects.json'
    $hubRecentEntries=[IO.File]::ReadAllText($hubRecent) | ConvertFrom-Json
    $hubRecentEntries=@($hubRecentEntries)
    Require ($hubRecentEntries.Count -eq 1 -and $hubRecentEntries[0] -ceq $hubManifest) 'Launcher did not persist Unicode project in isolated recent history'
    Require ([HubTestWindow]::RecentCount($hubWindow) -eq 1) 'Actual Launcher list did not show successful project launch'
    $hubDetails=[HubTestWindow]::ProjectDetails($hubWindow)
    Write-Output ('HUB_DETAILS_TEXT='+$hubDetails.Replace("`r`n",' | '))
    $hubDetailTitle=($hubDetails -split '\r?\n',2)[0]
    Require ([HubTestWindow]::DetailsReadOnly($hubWindow) -and $hubDetailTitle -cin @('HubTest','Project: HubTest') -and
        $hubDetails.Contains('engineVersion: 0.1.0') -and $hubDetails.Contains($hubSceneName) -and
        $hubDetails.Contains('compatibility is not verified')) 'Packaged Hub project details missing, editable or claim compatibility'
    Require ([IO.File]::ReadAllText($hubManifest) -ceq $hubManifestText) 'Hub details inspection changed project manifest'
    Write-Output 'PASS: packaged Hub selected-project name/version metadata/read-only details and no automatic compatibility/migration claim'
    if(![string]::IsNullOrEmpty($ExpectedEngineVersion)) {
        $hubReleaseDetails=''
        for($releaseAttempt=0;$releaseAttempt -lt 50;$releaseAttempt++) {
            if($hubLauncher.HasExited){throw 'Hub exited while reading package declaration'}
            $hubReleaseDetails=[HubTestWindow]::EngineDetails($hubWindow)
            if($hubReleaseDetails.Contains('Package-declared version: '+$ExpectedEngineVersion)){break}
            Start-Sleep -Milliseconds 100
        }
        Require ([HubTestWindow]::EngineDetailsReadOnly($hubWindow) -and $hubReleaseDetails.Contains('Package-declared version: '+$ExpectedEngineVersion) -and $hubReleaseDetails.Contains('Windows-x64') -and $hubReleaseDetails.Contains('not verified')) 'Packaged Hub selected-Editor declaration missing, editable, stale or claims verification'
        Write-Output "PASS: actual packaged Hub declares version $ExpectedEngineVersion separately from project metadata; integrity/signature/ABI explicitly unverified (not physical/pixel QA)"
    }
    Close-HubProcess $hubEditor $hubEditorWindow;Close-HubProcess $hubLauncher $hubWindow
    if(![string]::IsNullOrWhiteSpace($SelectedEditor)) {
        $hubLauncher=[Diagnostics.Process]::Start($hubStart);$hubLauncherHandle=$hubLauncher.Handle
        $hubWindow=Wait-HubWindow $hubLauncher 'KuboLauncherWindow';$hubChild=$null
        for($attempt=0;$attempt -lt 100;$attempt++) {
            $hubCandidate=Get-CimInstance Win32_Process -Filter "ParentProcessId=$($hubLauncher.Id) AND Name='KuboEditor.exe'" | Select-Object -First 1
            if($hubCandidate -and ![string]::IsNullOrWhiteSpace($hubCandidate.ExecutablePath)){$hubChild=$hubCandidate;break};Start-Sleep -Milliseconds 100
        }
        Require ($hubChild -and [IO.Path]::GetFullPath($hubChild.ExecutablePath) -eq $hubExpectedEditor) 'Hub restart lost the explicit Editor path'
        Require ([HubTestWindow]::EngineSelection($hubWindow) -eq 1 -and [IO.File]::ReadAllText($hubCatalog) -ceq $hubCatalogBytes) 'Hub restart changed selected engine/catalog'
        $hubEditor=[Diagnostics.Process]::GetProcessById($hubChild.ProcessId);$hubEditorHandle=$hubEditor.Handle
        $hubEditorWindow=Wait-HubWindow $hubEditor 'WindowClass'
        if($RollbackToBundled){
            Require ([HubTestWindow]::SelectBundled($hubWindow)) 'Could not request production Hub combo rollback'
            $hubRolledBack=$false
            for($attempt=0;$attempt -lt 100;$attempt++){
                $hubRollbackBytes=[IO.File]::ReadAllText($hubCatalog);$hubRollbackState=$hubRollbackBytes | ConvertFrom-Json;$hubRollbackState=@($hubRollbackState)
                if($hubRollbackState.Count -eq 3 -and $hubRollbackState[0] -ceq 'kubo-engine-catalog-1' -and $hubRollbackState[1] -ceq '' -and $hubRollbackState[2] -ceq $hubExpectedEditor){$hubRolledBack=$true;break}
                Start-Sleep -Milliseconds 50
            }
            Require ($hubRolledBack -and [HubTestWindow]::EngineSelection($hubWindow) -eq 0 -and !$hubEditor.HasExited) 'Rollback did not persist or terminated the running selected Editor'
            Write-Output "HUB_ROLLBACK_SAVED_FROM=$hubExpectedEditor TO=$(Join-Path $hubRoot 'KuboEditor.exe') RUNNING_EDITOR_KEPT=$($hubEditor.Id)"
        }
        Close-HubProcess $hubEditor $hubEditorWindow;Close-HubProcess $hubLauncher $hubWindow
        Write-Output ('PASS: explicit registered Editor actual launch/restart/control selection/catalog preservation: '+$hubExpectedEditor)
        if($RollbackToBundled){
            $hubLauncher=[Diagnostics.Process]::Start($hubStart);$hubLauncherHandle=$hubLauncher.Handle
            $hubWindow=Wait-HubWindow $hubLauncher 'KuboLauncherWindow';$hubChild=$null
            for($attempt=0;$attempt -lt 100;$attempt++){
                $hubCandidate=Get-CimInstance Win32_Process -Filter "ParentProcessId=$($hubLauncher.Id) AND Name='KuboEditor.exe'" | Select-Object -First 1
                if($hubCandidate -and ![string]::IsNullOrWhiteSpace($hubCandidate.ExecutablePath)){$hubChild=$hubCandidate;break};Start-Sleep -Milliseconds 100
            }
            $hubBundledEditor=Join-Path $hubRoot 'KuboEditor.exe'
            Require ($hubChild -and [IO.Path]::GetFullPath($hubChild.ExecutablePath) -eq $hubBundledEditor -and [HubTestWindow]::EngineSelection($hubWindow) -eq 0 -and [IO.File]::ReadAllText($hubCatalog) -ceq $hubRollbackBytes) 'Restart did not launch/save the explicitly rolled-back Editor'
            $hubEditor=[Diagnostics.Process]::GetProcessById($hubChild.ProcessId);$hubEditorHandle=$hubEditor.Handle
            $hubEditorWindow=Wait-HubWindow $hubEditor 'WindowClass'
            if($ExpectedBundledEngineVersion){
                for($attempt=0;$attempt -lt 50;$attempt++){$hubRollbackDetails=[HubTestWindow]::EngineDetails($hubWindow);if($hubRollbackDetails.Contains('Package-declared version: '+$ExpectedBundledEngineVersion)){break};Start-Sleep -Milliseconds 100}
                Require ($hubRollbackDetails.Contains('Package-declared version: '+$ExpectedBundledEngineVersion) -and $hubRollbackDetails.Contains('not verified')) 'Rollback displayed the wrong package declaration or trust claim'
            }
            Require ([IO.File]::ReadAllText($hubManifest) -ceq $hubManifestText -and (Test-Path -LiteralPath $hubExpectedEditor)) 'Rollback migrated project or deleted installed Editor'
            Write-Output "HUB_ROLLBACK_RESTART_PID=$($hubEditor.Id) EXE=$($hubChild.ExecutablePath) CATALOG_SHA256=$((Get-FileHash -LiteralPath $hubCatalog -Algorithm SHA256).Hash)"
            Close-HubProcess $hubEditor $hubEditorWindow;Close-HubProcess $hubLauncher $hubWindow
            Write-Output 'PASS: original packaged Hub saved external installed Editor launch/restart -> production combo rollback -> bundled Editor launch/restart; old+installed executables and project bytes retained (not physical dialog/ABI/signature proof)'
        }
    }
    $hubLayout=Join-Path $hubProject 'UserSettings\editor-layout.ini'
    Require ((Test-Path -LiteralPath $hubLayout) -and [IO.File]::ReadAllText($hubLayout).Contains('UnicodeMigrationMarker')) 'Packaged Editor did not migrate/persist project layout'
    Require ([IO.File]::ReadAllText((Join-Path $hubProject 'imgui.ini')) -eq $hubLegacyLayout) 'Packaged Editor altered legacy layout'
    $hubEditor=Start-HubEditor $hubManifest
    $hubEditorHandle=$hubEditor.Handle;$hubEditorWindow=Wait-HubWindow $hubEditor 'WindowClass'
    Close-HubProcess $hubEditor $hubEditorWindow
    Require ([IO.File]::ReadAllText($hubLayout).Contains('UnicodeMigrationMarker')) 'Packaged Editor restart lost persisted layout'
    # Unreadable optional settings must not be replaced by startup defaults on close.
    [IO.File]::WriteAllBytes($hubLayout,[byte[]]@(0xff,0xfe,0xff))
    $hubWindowsSettings=Join-Path $hubProject 'UserSettings\editor-windows.ini'
    $hubNativeSettings=Join-Path $hubProject 'UserSettings\native-build.json'
    $hubNativeBytes='{"version":1,"native":true,"sdk":"C:/private SDK","project":"Native/Game.vcxproj","output":"Builds/Native"}'
    [IO.File]::WriteAllText($hubNativeSettings,$hubNativeBytes)
    [IO.File]::WriteAllBytes($hubWindowsSettings,[byte[]]@(0xff,0xfe,0xff))
    $hubEditor=Start-HubEditor $hubManifest
    $hubEditorHandle=$hubEditor.Handle;$hubEditorWindow=Wait-HubWindow $hubEditor 'WindowClass'
    Close-HubProcess $hubEditor $hubEditorWindow
    Require ([Convert]::ToBase64String([IO.File]::ReadAllBytes($hubLayout)) -eq '//7/') 'Editor close replaced unreadable layout with defaults'
    Require ([Convert]::ToBase64String([IO.File]::ReadAllBytes($hubWindowsSettings)) -eq '//7/') 'Editor replaced unreadable visibility settings'
    Require ([IO.File]::ReadAllText($hubNativeSettings) -eq $hubNativeBytes) 'Editor startup/close rewrote local native build settings'
    $hubExport=Join-Path $hubFixture 'Export'
    & (Join-Path $hubRoot 'tools\package_project.ps1') -ProjectFile $hubManifest -OutputDirectory $hubExport -RuntimeExecutable (Join-Path $hubRoot 'KuboPlayer.exe')
    Require (!(Test-Path -LiteralPath (Join-Path $hubExport 'UserSettings\editor-layout.ini'))) 'Runtime export leaked editor layout'
    Require (!(Test-Path -LiteralPath (Join-Path $hubExport 'UserSettings\native-build.json'))) 'Runtime export leaked local native build settings'
    $hubPlayer=Start-Process -FilePath (Join-Path $hubExport 'KuboPlayer.exe') -WorkingDirectory (Join-Path $hubFixture 'Unrelated') -WindowStyle Hidden -PassThru
    $hubPlayerHandle=$hubPlayer.Handle;$hubPlayerWindow=Wait-HubWindow $hubPlayer 'WindowClass'
    Close-HubProcess $hubPlayer $hubPlayerWindow
    Write-Output 'PASS: actual portable Launcher -> packaged Editor, Unicode project/isolated recent list, bundled no-build export -> packaged Player, native close exit 0 (not dialog/pixel/clean-machine QA)'
    Write-Output 'PASS: packaged Editor legacy layout migration, UserSettings persistence across restart, unreadable settings/legacy preservation and runtime export exclusion'
    $hubSucceeded=$true
} finally {
    foreach($owned in @($hubPlayer,$hubEditor,$hubLauncher)) {if($owned -and !$owned.HasExited) {Stop-Process -Id $owned.Id;$owned.WaitForExit()}}
    $hubResolved=[IO.Path]::GetFullPath($hubFixture)
    if(!$hubResolved.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()),[StringComparison]::OrdinalIgnoreCase) -or !(Split-Path -Leaf $hubResolved).StartsWith('kubo-portable-hub-')) {throw 'Unsafe owned fixture cleanup'}
    if($hubSucceeded -and !$KeepFixture -and (Test-Path -LiteralPath $hubResolved)) {Remove-Item -LiteralPath $hubResolved -Recurse -Force}
    elseif($hubSucceeded -and (Test-Path -LiteralPath $hubResolved)) {Write-Output "PORTABLE_HUB_SUCCESS_FIXTURE_RETAINED=$hubResolved"}
    elseif(Test-Path -LiteralPath $hubResolved) {Write-Output "PORTABLE_HUB_FAILURE_FIXTURE_RETAINED=$hubResolved"}
}
