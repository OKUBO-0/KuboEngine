# Shared by source, portable engine and Runtime SDK packagers. No global settings.
if(-not ('Kubo.PackagePublicationNative' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace Kubo {
 public static class PackagePublicationNative {
  [StructLayout(LayoutKind.Sequential,Pack=4)] struct Info {public uint Attributes;public long Creation,Access,Write;public uint Volume,SizeHigh,SizeLow,Links,IndexHigh,IndexLow;}
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFileW(string p,uint a,uint s,IntPtr security,uint mode,uint flags,IntPtr template);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetFileInformationByHandle(SafeFileHandle h,out Info i);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool CreateDirectoryW(string p,IntPtr security);
  [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool MoveFileExW(string a,string b,uint flags);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetFileInformationByHandle(SafeFileHandle h,int kind,IntPtr data,uint size);
  static void Fail(string what) {throw new Win32Exception(Marshal.GetLastWin32Error(),what);}
  public static string Identity(string path) {
   using(var h=CreateFileW(path,0x80,7,IntPtr.Zero,3,0x02200000,IntPtr.Zero)) {
    if(h.IsInvalid) {int e=Marshal.GetLastWin32Error();if(e==2 || e==3)return "";Fail("Cannot inspect package directory");}
    Info i;if(!GetFileInformationByHandle(h,out i))Fail("Cannot read package directory identity");
    if((i.Attributes&0x10)==0 || (i.Attributes&0x400)!=0 || (i.IndexHigh==0 && i.IndexLow==0))throw new IOException("Package directory is linked or has no stable identity");
    return i.Volume.ToString("x8")+":"+i.IndexHigh.ToString("x8")+i.IndexLow.ToString("x8");
   }
  }
  public static void CreateStage(string path) {if(!CreateDirectoryW(path,IntPtr.Zero))Fail("Cannot exclusively create package stage");}
  public static void Move(string from,string to) {if(!MoveFileExW(from,to,8))Fail("Cannot move package directory without overwriting output");}
  public static FileStream Journal(string path,bool create) {
   var h=CreateFileW(path,0xc0010000,1,IntPtr.Zero,create?1u:3u,0x00200000,IntPtr.Zero);
   if(h.IsInvalid) {h.Dispose();Fail("Package publication is active or journal cannot be opened");}
   return new FileStream(h,FileAccess.ReadWrite);
  }
  public static void Discard(FileStream stream) {
   IntPtr p=Marshal.AllocHGlobal(4);try {Marshal.WriteInt32(p,1);if(!SetFileInformationByHandle(stream.SafeFileHandle,4,p,4))Fail("Cannot discard owned package journal");}finally {Marshal.FreeHGlobal(p);}
  }
  public static void Archive(FileStream stream,string path) {
   byte[] name=System.Text.Encoding.Unicode.GetBytes(path);int offset=IntPtr.Size==8?20:12;int size=offset+name.Length+2;
   IntPtr p=Marshal.AllocHGlobal(size);try {
    Marshal.Copy(new byte[size],0,p,size);Marshal.WriteInt32(p,IntPtr.Size==8?16:8,name.Length);Marshal.Copy(name,0,IntPtr.Add(p,offset),name.Length);
    if(!SetFileInformationByHandle(stream.SafeFileHandle,3,p,(uint)size))Fail("Cannot archive package recovery journal");
   } finally {Marshal.FreeHGlobal(p);}
  }
 }
}
'@
}
function Assert-KuboPackageAncestors([string]$Path) {
    $ErrorActionPreference='Stop'
    $current=$Path
    while($current) {
        if(Test-Path -LiteralPath $current) {
            $item=Get-Item -LiteralPath $current -Force
            if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {throw "Package publication refuses linked ancestors: $current"}
        }
        $next=Split-Path -Parent $current;if($next -eq $current) {break};$current=$next
    }
}
function Get-KuboPackageJournalPath([string]$Output) {return Join-Path (Split-Path -Parent $Output) ('.'+[IO.Path]::GetFileName($Output)+'.kubo-package.pending.json')}
function Recover-KuboPackagePublication([string]$Output) {
    $ErrorActionPreference='Stop'
    $Output=[IO.Path]::GetFullPath($Output).TrimEnd('\')
    $journalPath=Get-KuboPackageJournalPath $Output
    if(!(Test-Path -LiteralPath $journalPath)) {return $false}
    Assert-KuboPackageAncestors $journalPath
    $held=$null
    try {
        $held=[Kubo.PackagePublicationNative]::Journal($journalPath,$false)
        if($held.Length -lt 1 -or $held.Length -gt 16384) {throw 'Package recovery journal exceeds size bounds'}
        $bytes=New-Object byte[] ([int]$held.Length);$offset=0
        while($offset -lt $bytes.Length) {$n=$held.Read($bytes,$offset,$bytes.Length-$offset);if($n -eq 0) {throw 'Incomplete package journal'};$offset+=$n}
        $text=[Text.UTF8Encoding]::new($false,$true).GetString($bytes)
        if($text.IndexOf([char]0) -ge 0) {throw 'NUL in package journal'}
        $depth=0;$quoted=$false;$escaped=$false
        foreach($ch in $text.ToCharArray()) {
            if($quoted) {if($escaped) {$escaped=$false} elseif($ch -eq '\') {$escaped=$true} elseif($ch -eq '"') {$quoted=$false}}
            elseif($ch -eq '"') {$quoted=$true} elseif($ch -eq '{' -or $ch -eq '[') {if(++$depth -gt 16) {throw 'Package journal nesting exceeds 16'}} elseif($ch -eq '}' -or $ch -eq ']') {$depth--}
        }
        $record=$text | ConvertFrom-Json
        if($record -isnot [pscustomobject]) {throw 'Package journal must be an object'}
        foreach($field in @('format','root','token','stage','backup')) {if($record.$field -isnot [string]) {throw 'Package journal fields must be strings'}}
        if($record.format -cne 'kubo-package-publication-1' -or $record.root -cne [IO.Path]::GetFileName($Output) -or
            $record.token -isnot [string] -or $record.token -cnotmatch '^[0-9a-f]{32}$' -or
            $record.stage -cne ('.kubo-package-stage-'+$record.token) -or $record.backup -cne ('.kubo-package-backup-'+$record.token)) {throw 'Invalid package journal sibling schema'}
        foreach($id in @($record.parentId,$record.stageId)) {if($id -isnot [string] -or $id -cnotmatch '^[0-9a-f]{8}:[0-9a-f]{16}$') {throw 'Invalid package journal identity'}}
        if($record.originalId -isnot [string] -or ($record.originalId -ne '' -and $record.originalId -cnotmatch '^[0-9a-f]{8}:[0-9a-f]{16}$')) {throw 'Invalid package original identity'}
        $parent=Split-Path -Parent $Output;Assert-KuboPackageAncestors $parent
        if([Kubo.PackagePublicationNative]::Identity($parent) -cne $record.parentId) {throw 'Package parent identity changed; all data retained'}
        $stage=Join-Path $parent $record.stage;$backup=Join-Path $parent $record.backup
        $stageId=[Kubo.PackagePublicationNative]::Identity($stage);$backupId=[Kubo.PackagePublicationNative]::Identity($backup);$rootId=[Kubo.PackagePublicationNative]::Identity($Output)
        if(($stageId -ne '' -and $stageId -cne $record.stageId) -or ($backupId -ne '' -and ($record.originalId -eq '' -or $backupId -cne $record.originalId))) {throw 'Package stage/backup identity changed; recovery refused'}
        $published=$false
        if($stageId -eq '' -and $rootId -ceq $record.stageId) {
            if(!(Test-Path -LiteralPath (Join-Path $Output 'package.json') -PathType Leaf) -or !(Test-Path -LiteralPath (Join-Path $Output 'KuboPlayer.exe') -PathType Leaf)) {throw 'Published package is incomplete; inspect manually'}
            $published=$true
        } elseif($rootId -eq '' -and $record.originalId -ne '' -and $backupId -ceq $record.originalId) {
            if(@(Get-ChildItem -LiteralPath $backup -Force).Count -ne 0) {throw 'Original package folder became nonempty; all data retained'}
            [Kubo.PackagePublicationNative]::Move($backup,$Output)
        } elseif($backupId -eq '' -and $rootId -ceq $record.originalId) {
            # The interrupted candidate is retained; it is never resumed/deleted.
        } else {throw 'Package recovery has conflicting output; all data retained'}
        [Kubo.PackagePublicationNative]::Archive($held,($journalPath+'.recovered-'+[guid]::NewGuid().ToString('N')))
        if($published) {Write-Verbose 'Recovered already published package without regeneration';return $true}
        throw "Interrupted package retained at $stage; original output restored/preserved. Inspect the archived journal and retry export."
    } finally {if($held) {$held.Dispose()}}
}
function Start-KuboPackagePublication([string]$Output) {
    $ErrorActionPreference='Stop'
    $Output=[IO.Path]::GetFullPath($Output).TrimEnd('\')
    $parent=Split-Path -Parent $Output
    if(!$parent -or $Output -eq [IO.Path]::GetPathRoot($Output).TrimEnd('\')) {throw 'Do not publish a package at a volume root'}
    Assert-KuboPackageAncestors $Output
    if(Recover-KuboPackagePublication $Output) {return @{Published=$true;Output=$Output}}
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    $held=$null;$stage=''
    try {
        $journalPath=Get-KuboPackageJournalPath $Output
        $held=[Kubo.PackagePublicationNative]::Journal($journalPath,$true)
        $original=[Kubo.PackagePublicationNative]::Identity($Output)
        if($original -ne '' -and @(Get-ChildItem -LiteralPath $Output -Force).Count -ne 0) {throw 'OutputDirectory must be empty; choose a new export directory'}
        $token=[guid]::NewGuid().ToString('N');$stage=Join-Path $parent ('.kubo-package-stage-'+$token);$backup=Join-Path $parent ('.kubo-package-backup-'+$token)
        [Kubo.PackagePublicationNative]::CreateStage($stage)
        $record=[ordered]@{format='kubo-package-publication-1';root=[IO.Path]::GetFileName($Output);token=$token;stage=[IO.Path]::GetFileName($stage);backup=[IO.Path]::GetFileName($backup);parentId=[Kubo.PackagePublicationNative]::Identity($parent);originalId=$original;stageId=[Kubo.PackagePublicationNative]::Identity($stage)}
        $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($record | ConvertTo-Json -Compress))
        $held.Write($bytes,0,$bytes.Length);$held.Flush($true)
        return @{Published=$false;Output=$Output;Stage=$stage;Backup=$backup;Parent=$parent;Record=$record;Journal=$held;Retired=$false}
    } catch {
        if($held) {if(!$stage) {[Kubo.PackagePublicationNative]::Discard($held)};$held.Dispose()};throw
    }
}
function Complete-KuboPackagePublication($Context,[scriptblock]$BeforeCommit=$null,[scriptblock]$AfterCommit=$null) {
    $ErrorActionPreference='Stop'
    $record=$Context.Record;Assert-KuboPackageAncestors $Context.Output
    if([Kubo.PackagePublicationNative]::Identity($Context.Parent) -cne $record.parentId -or
        [Kubo.PackagePublicationNative]::Identity($Context.Stage) -cne $record.stageId -or
        [Kubo.PackagePublicationNative]::Identity($Context.Output) -cne $record.originalId) {throw 'Package identities changed before publication'}
    if($record.originalId -ne '') {
        if(@(Get-ChildItem -LiteralPath $Context.Output -Force).Count -ne 0) {throw 'OutputDirectory became nonempty before publication'}
        [Kubo.PackagePublicationNative]::Move($Context.Output,$Context.Backup);$Context.Retired=$true
        if([Kubo.PackagePublicationNative]::Identity($Context.Backup) -cne $record.originalId -or @(Get-ChildItem -LiteralPath $Context.Backup -Force).Count -ne 0) {throw 'Original output changed during retirement'}
    }
    # Optional callbacks are internal verification seams, not command-line switches.
    if($BeforeCommit) {& $BeforeCommit $Context}
    [Kubo.PackagePublicationNative]::Move($Context.Stage,$Context.Output);$Context.Published=$true
    if($AfterCommit) {& $AfterCommit $Context}
    try {
        if($Context.Retired) {
            if([Kubo.PackagePublicationNative]::Identity($Context.Backup) -cne $record.originalId) {throw 'Original backup identity changed'}
            [IO.Directory]::Delete($Context.Backup,$false)
        }
        [Kubo.PackagePublicationNative]::Discard($Context.Journal)
    } catch {Write-Warning ('Package published; backup/recovery journal retained: '+$_.Exception.Message)}
}
function Close-KuboPackagePublication($Context) {
    $ErrorActionPreference='Stop'
    if($Context -and $Context.Journal) {
        try {
            if(!$Context.Published -and $Context.Retired -and [Kubo.PackagePublicationNative]::Identity($Context.Output) -eq '' -and
                [Kubo.PackagePublicationNative]::Identity($Context.Backup) -ceq $Context.Record.originalId) {
                [Kubo.PackagePublicationNative]::Move($Context.Backup,$Context.Output)
            }
        } finally {$Context.Journal.Dispose()}
    }
}
