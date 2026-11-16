#include "../engine/services/GitWorkspace.h"
#include "../engine/services/UpdateService.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary);
    output << text;
    if (!output) throw std::runtime_error("Cannot write test fixture");
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

int main()
{
    const auto fixture = std::filesystem::temp_directory_path() /
        (L"kubo-update-check-日本語-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        std::filesystem::create_directories(fixture);
        Require(Engine::Services::GitWorkspace::IsAvailable(), "Git CLI was not detected");
        const auto git = Engine::Services::GitWorkspace::ReadStatus(std::filesystem::current_path());
        Require(git.repository ? git.error.empty() : !git.error.empty(), "Workspace Git result has no explicit repository/error state");

        const auto path = fixture / "manifest.json";
        Write(path, R"({"version":"0.2.0","url":"https://example.invalid/kubo.zip","sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"})");
        const auto update = Engine::Services::UpdateService::ReadManifest(path, "0.1.0");
        Require(update.valid && update.updateAvailable && update.url.find("https://") == 0, "Update manifest was not parsed");
        Require(Engine::Services::UpdateService::CompareVersions("1.0.0", "1.0") == 0, "Version comparison failed");
        for (const auto* version : {"1.2junk", "-1.2", "1..2", "1.2.", "+1.0", " 1.0", "2147483648.0", ""}) {
            Write(path, std::string("{\"version\":\"") + version + "\"}");
            const auto invalid = Engine::Services::UpdateService::ReadManifest(path, "0.1.0");
            Require(!invalid.valid && !invalid.updateAvailable && !invalid.error.empty(), "Malformed version accepted");
        }
        Write(path, R"({"version":"1.0","sha256":"abc"})");
        Require(!Engine::Services::UpdateService::ReadManifest(path, "0.1").valid, "Malformed manifest digest accepted");
        Write(path, std::string(1024 * 1024 + 1, ' '));
        Require(!Engine::Services::UpdateService::ReadManifest(path, "0.1").valid, "Oversized manifest accepted");
        const auto source = fixture / L"コピー元.bin";
        const auto staged = fixture / L"更新先.bin";
        Write(source, "abc");
        std::string error;
        std::string hash;
        Require(Engine::Services::UpdateService::ComputeSha256(source, hash, error), error.c_str());
        Require(hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known vector failed");
        auto uppercase = hash;
        std::transform(uppercase.begin(), uppercase.end(), uppercase.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        Write(staged, "existing staged file");
        const auto legacyTemporary = fixture / L"更新先.bin.pending.tmp";
        Write(legacyTemporary, "unrelated temporary file");
        Require(!Engine::Services::UpdateService::StageVerifiedArtifact(source, staged, std::string(64, '0'), error), "Mismatched hash accepted");
        Require(Read(staged) == "existing staged file", "Hash mismatch changed existing destination");
        Require(!Engine::Services::UpdateService::StageVerifiedArtifact(source, staged, "abc", error), "Short digest accepted");
        HANDLE writer = CreateFileW(source.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(writer != INVALID_HANDLE_VALUE, "Cannot lock test source");
        const auto sourceLocked = Engine::Services::UpdateService::StageVerifiedArtifact(source, staged, hash, error);
        CloseHandle(writer);
        Require(!sourceLocked && Read(staged) == "existing staged file", "Writable source was staged");
        HANDLE destinationLock = CreateFileW(staged.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(destinationLock != INVALID_HANDLE_VALUE, "Cannot lock test destination");
        const auto targetLocked = Engine::Services::UpdateService::StageVerifiedArtifact(source, staged, hash, error);
        CloseHandle(destinationLock);
        Require(!targetLocked && Read(staged) == "existing staged file", "Locked-destination failure changed existing data");
        Require(Engine::Services::UpdateService::StageVerifiedArtifact(source, staged, uppercase, error), error.c_str());
        Require(Read(staged) == "abc", "Verified staged bytes differ");
        Require(Read(legacyTemporary) == "unrelated temporary file", "Staging touched unrelated temporary file");
        size_t files = 0;
        for (const auto& entry : std::filesystem::directory_iterator(fixture)) ++files;
        Require(files == 4, "Failed staging leaked temporary files");
        Write(source, "");
        Require(Engine::Services::UpdateService::ComputeSha256(source, hash, error) &&
            hash == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Empty-file SHA-256 failed");
        std::filesystem::remove_all(fixture);
        std::cout << "PASS: Git status, strict versions/digests, known SHA-256 vectors and verified staging failure isolation\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        std::filesystem::remove_all(fixture);
        return 1;
    }
}
