#include "../engine/services/ExportJob.h"
#include <iostream>
#include <optional>
#include <vector>

void Require(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
std::optional<std::wstring> ReadEnvironment(const wchar_t* name) {
    SetLastError(ERROR_SUCCESS);const auto length=GetEnvironmentVariableW(name,nullptr,0);
    if(!length) {if(GetLastError()==ERROR_ENVVAR_NOT_FOUND) return std::nullopt;return std::wstring{};}
    std::wstring value(length,L'\0');const auto written=GetEnvironmentVariableW(name,value.data(),length);
    Require(written<length,"Environment read changed size");value.resize(written);return value;
}
struct ScopedEnvironment {
    std::wstring name;std::optional<std::wstring> previous;
    ScopedEnvironment(const wchar_t* key,const wchar_t* value):name(key),previous(ReadEnvironment(key)) {
        Require(SetEnvironmentVariableW(key,value)!=FALSE,"Cannot set owned test environment");
    }
    ~ScopedEnvironment() {SetEnvironmentVariableW(name.c_str(),previous ? previous->c_str() : nullptr);}
};
void Write(const std::filesystem::path& path,const char* text) {
    std::filesystem::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);file<<text;
    Require(bool(file),"Cannot write export environment fixture");
}
void Wait(Engine::Services::ExportJob& job) {
    for(int i=0;i<300 && job.GetState()==Engine::Services::ExportJob::State::Running;++i) {Sleep(100);job.Poll();}
    Require(job.GetState()!=Engine::Services::ExportJob::State::Running,"Environment export exceeded 30 seconds");
    if(job.GetState()!=Engine::Services::ExportJob::State::Succeeded) throw std::runtime_error(job.ReadLog());
    Require(job.ReadLog().find("PASS_EXPORT_ENVIRONMENT")!=std::string::npos,"Environment assertions did not execute");
}
int main() {
    using Engine::Services::ExportJob;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-export-environment-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    ExportJob job;
    try {
        const auto project=fixture/"Project/Game.kubo",runtime=fixture/"Engine/KuboPlayer.exe";
        const auto sdk=fixture/"SDK",consumer=fixture/"Native/Game.vcxproj";
        Write(project,"{}");Write(runtime,"fixture");Write(sdk/"sdk-package.json","{}");Write(consumer,"fixture");
        const auto module=fixture/"Incompatible/Microsoft.PowerShell.Utility";
        Write(module/"Microsoft.PowerShell.Utility.psd1","@{ RootModule='Utility.psm1'; ModuleVersion='99.0.0'; GUID='5dd371bf-d613-4be9-86ad-188176c416fa'; FunctionsToExport=@('Get-FileHash'); CmdletsToExport=@(); AliasesToExport=@() }");
        Write(module/"Utility.psm1","function Get-FileHash { throw 'Inherited incompatible module was loaded' }\nExport-ModuleMember -Function Get-FileHash\n");
        const char* script=R"PS(param($ProjectFile,$OutputDirectory,$Configuration,$RuntimeExecutable,$NativeProject,$SdkDirectory)
$ErrorActionPreference='Stop'
$hash=Get-FileHash -LiteralPath $ProjectFile -Algorithm SHA256
if($hash.Hash.Length -ne 64) {throw 'Built-in hash did not execute'}
$command=Get-Command Get-FileHash
if(!$command.Module.Path.StartsWith((Join-Path $PSHOME 'Modules'),[StringComparison]::OrdinalIgnoreCase)) {throw 'Wrong host module loaded'}
$expected=(-join ([char[]]@(0x65e5,0x672c,0x8a9e,0x3d,0x5024)))+';"&''$"'
if($env:KuboExportProbe -ne $expected) {throw 'Unicode/quoted environment changed'}
if($env:Path -ne $env:KuboExportExpectedPath) {throw 'Unrelated PATH changed'}
if($env:KuboExportEmpty -ne '') {throw 'Empty environment changed'}
$record=@{sha256=$hash.Hash;configuration=$Configuration} | ConvertTo-Json -Compress
if(($record | ConvertFrom-Json).sha256 -ne $hash.Hash) {throw 'Built-in JSON conversion failed'}
Write-Output 'PASS_EXPORT_ENVIRONMENT'
)PS";
        const auto packager=fixture/"Engine/export.ps1";Write(packager,script);Write(sdk/"tools/build_native_game.ps1",script);
        const auto path=ReadEnvironment(L"PATH");Require(path.has_value(),"PATH unavailable for preservation test");
        ScopedEnvironment probe(L"KuboExportProbe",L"日本語=値;\"&'$\"");
        ScopedEnvironment expectedPath(L"KuboExportExpectedPath",path->c_str());
        ScopedEnvironment empty(L"KuboExportEmpty",L"");
        wchar_t system[32768]{};Require(GetSystemDirectoryW(system,32768)>0,"System directory unavailable");
        const auto poisoned=(fixture/"Incompatible").wstring()+L";"+(std::filesystem::path(system)/"WindowsPowerShell/v1.0/Modules").wstring();
        for(const wchar_t* modulePath : {poisoned.c_str(),L"",static_cast<const wchar_t*>(nullptr)}) {
            ScopedEnvironment moduleOverride(L"PSModulePath",modulePath);
            const auto before=ReadEnvironment(L"PSModulePath");std::string error;
            Require(job.Start(project,fixture/"Prebuilt",packager,runtime,error),error.c_str());Wait(job);
            Require(ReadEnvironment(L"PSModulePath")==before && ReadEnvironment(L"PATH")==path,"Prebuilt export changed parent environment");
            Require(job.StartNative(project,fixture/"NativeExport",sdk,consumer,false,error),error.c_str());Wait(job);
            Require(ReadEnvironment(L"PSModulePath")==before && ReadEnvironment(L"PATH")==path,"Native export changed parent environment");
        }
        std::filesystem::remove_all(fixture);
        std::cout<<"PASS: actual native/prebuilt Windows PowerShell host modules, incompatible/empty/absent module paths, Unicode/PATH inheritance and parent isolation\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        if(job.GetState()==ExportJob::State::Running) {std::string reason;if(job.Cancel(reason)) {for(int i=0;i<300 && job.GetState()==ExportJob::State::Running;++i) {Sleep(100);job.Poll();}}}
        std::cerr<<"Retained owned environment fixture for inspection\n";return 1;
    }
}
