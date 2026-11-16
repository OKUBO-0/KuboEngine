#include "../engine/assets/AssetImportJob.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>
namespace {
namespace fs = std::filesystem;
using namespace Engine::Assets;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const fs::path& path, const std::string& bytes) { std::ofstream file(path, std::ios::binary); file << bytes; Require(bool(file), "Fixture write failed"); }
AssetImportJob::Result Wait(AssetImportJob& job) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    AssetImportJob::Result result;
    while (!job.Poll(result)) { Require(std::chrono::steady_clock::now() < limit, "Dependency worker timeout"); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    return result;
}
}
int main() {
    const auto fixture = fs::temp_directory_path() / (L"kubo-dependencies-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    try {
        const auto project = fixture / L"日本語 project", root = project / "Assets";
        fs::create_directories(root / "Models"); fs::create_directories(root / "Other");
        const auto obj = root / "Models/model.obj", mtl = root / L"Models/材質.mtl", image = root / L"Models/色 #1.png";
        Write(obj, "# ignored mtllib wrong.mtl\nmtllib 材質.mtl 材質.mtl # deduplicate\nv 0 0 0\n");
        Write(mtl, "newmtl Sample\nmap_Kd \"色 #1.png\"\n"); Write(image, "image");
        Write(root / L"Models/頂点 bin.bin", "vertices");
        const auto gltf = root / "Models/model.gltf";
        Write(gltf, "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"頂点%20bin.bin\"},{\"uri\":\"data:application/octet-stream;base64,AAAA\"}],\"images\":[{\"uri\":\"missing.png\"},{\"bufferView\":0}]}");
        AssetRegistry registry; std::string error;
        Require(registry.Scan(root, error), error.c_str());
        const auto objGuid = registry.FindByPath(obj)->guid, mtlGuid = registry.FindByPath(mtl)->guid;
        const auto imageGuid = registry.FindByPath(image)->guid, gltfGuid = registry.FindByPath(gltf)->guid;
        auto graph = InspectAssetDependencies(registry);
        Require(graph.issues.empty() && graph.edges.size() == 4, "Dependency parsing/deduplication failed");
        Require(graph.DependenciesOf(objGuid).size() == 1 && graph.DependentsOf(mtlGuid).size() == 1 && graph.DependentsOf(imageGuid).size() == 1, "OBJ/MTL graph or reverse edges failed");
        const auto gltfEdges = graph.DependenciesOf(gltfGuid);
        Require(gltfEdges.size() == 2 && !gltfEdges[0]->missing && gltfEdges[1]->missing && gltfEdges[0]->targetRelative == "Models/頂点 bin.bin", "glTF URI, embedded data or missing reference failed");
        Require(!graph.MoveRisk(registry, objGuid, "Other/model.obj").empty() && graph.MoveRisk(registry, objGuid, "Models/renamed.obj").empty(), "Folder move risk or same-folder owner rename incorrect");
        Require(!graph.MoveRisk(registry, imageGuid, "Models/new.png").empty(), "Referenced dependency rename permitted");
        bool cancelled = false;
        try { InspectAssetDependencies(registry, [] { return true; }); } catch (const std::exception&) { cancelled = true; }
        Require(cancelled, "Dependency inspection ignored cancellation");
        AssetImportJob job;
        Require(job.Start({project, root, {"Models/model.obj", "Other/model.obj", objGuid}}), "Risky move job start failed");
        auto result = Wait(job);
        Require(!result.success && !result.moveCommitted && fs::exists(obj) && !fs::exists(root / "Other/model.obj") && !fs::exists(root / ".kubo/asset-move.pending.json"), "Risky owner move mutated files");
        Require(job.Start({project, root, {"Models/色 #1.png", "Models/new.png", imageGuid}}), "Incoming-risk move start failed");
        result = Wait(job); Require(!result.success && !result.moveCommitted && fs::exists(image), "Referenced texture moved without rewriting dependents");
        Require(job.Start({project, root, {"Models/model.obj", "Models/renamed.obj", objGuid}}), "Same-folder rename start failed");
        result = Wait(job); Require(result.success && result.moveCommitted, result.error.c_str());
        AssetDependencies published;
        Require(!result.ApplyTo(registry, {project, root / "Wrong"}, &published) && published.edges.empty(), "Stale graph completion published");
        Require(result.ApplyTo(registry, {project, root}, &published) && published.DependenciesOf(objGuid).size() == 1 && registry.ResolveGuid(objGuid) == root / "Models/renamed.obj", "Registry/graph publication failed");
        for (const auto& invalid : {"{", "{\"buffers\":[{\"uri\":\"../../escape.bin\"}]}", "{\"buffers\":[{\"uri\":\"https://example.com/data.bin\"}]}", "{\"images\":[{\"uri\":\"%00bad.png\"}]}", "{\"extensionsUsed\":[\"VENDOR_unknown\"]}"}) {
            Write(gltf, invalid); graph = InspectAssetDependencies(registry);
            Require(!graph.issues.empty() && !graph.MoveRisk(registry, objGuid, "Models/again.obj").empty(), "Unsupported/corrupt dependencies silently permitted move");
        }
        Write(gltf, std::string(1000, '[') + "0" + std::string(1000, ']'));
        graph = InspectAssetDependencies(registry);
        Require(!graph.issues.empty() && graph.issues.back().message.find("structure limit") != std::string::npos, "Deep glTF reached recursive parser");
        Write(gltf, std::string(8 * 1024 * 1024 + 1, ' '));
        graph = InspectAssetDependencies(registry);
        Require(!graph.issues.empty(), "Oversized source silently passed dependency inspection");
        Write(gltf, "{}"); Write(mtl, "map_Kd -s 1 1 1 image.png\n");
        graph = InspectAssetDependencies(registry); Require(!graph.issues.empty(), "Unsupported MTL options silently passed");
        Require(job.Start({project, root}), "Diagnostic refresh start failed"); result = Wait(job);
        Require(result.success && !result.dependencies.issues.empty(), "Dependency diagnostics failed registry publication or were lost");
        fs::remove_all(fixture);
        std::cout << "PASS: OBJ/MTL/glTF dependency graph, UTF-8 URIs, missing/reverse edges, move guards, diagnostics and worker publication\n";
        return 0;
    } catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; std::error_code ec; fs::remove_all(fixture, ec); return 1; }
}
