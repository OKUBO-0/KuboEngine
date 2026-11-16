#include "../engine/base/ProjectContext.h"
#include "../engine/scene/entity/EntityWorld.h"
#include <Windows.h>
#include <iostream>
#include <stdexcept>
#include <fstream>

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
        ("kubo-project-creation-" + std::to_string(GetCurrentProcessId()));
    try {
        Engine::Base::ProjectContext project;
        std::string error;
        if (!project.Create(root, "Sample", error)) throw std::runtime_error(error);
        Engine::Scene::EntityWorld world;
        if (!world.Load(project.ResolvePath(project.GetSettings().startupScene), error)) throw std::runtime_error(error);
        if (!world.GetEntities().empty()) throw std::runtime_error("Initial scene must be empty");
        if (project.Create(root, "Sample", error)) throw std::runtime_error("Existing project was overwritten");
        if (project.Create(root, "Another", error)) throw std::runtime_error("A second project was created in the same root");
        if (std::filesystem::exists(root / "Another.kubo")) throw std::runtime_error("Rejected creation left a second manifest");
        Engine::Base::ProjectContext reopened;
        if (!reopened.Open(root / "Sample.kubo", error)) throw std::runtime_error(error);
        if (reopened.GetSettings().startupScene != "Assets/Scenes/Main.scene") throw std::runtime_error("Startup scene was not persisted");
        const auto uppercase=root/"Sample.KUBO";std::filesystem::rename(root/"Sample.kubo",uppercase);
        if(!reopened.Open(uppercase,error) || !reopened.Open(root,error)) throw std::runtime_error("Case-insensitive project extension failed");
        {std::ofstream extra(root/"Other.kubo");extra<<"{}";}
        if(reopened.Open(root,error) || error.find("Multiple")==std::string::npos) throw std::runtime_error("Ambiguous manifest directory was silently selected");
        if(!reopened.Open(uppercase,error)) throw std::runtime_error("Explicit manifest in ambiguous directory was rejected");
        for(const auto& invalid:{std::string("CON"),std::string("nul.project"),std::string("COM1"),std::string("bad?name"),std::string("trailing."),std::string("space "),std::string("a\0b",3),std::string("\xff",1)}) {
            const auto invalidRoot=root/"Rejected";
            if(project.Create(invalidRoot,invalid,error) || std::filesystem::exists(invalidRoot)) throw std::runtime_error("Invalid project name mutated the filesystem");
        }
        const auto retainedRoot=root/"Retained";std::filesystem::create_directories(retainedRoot/"Assets/Scenes");
        const std::string retained="{\"version\":1,\"entities\":[],\"retained\":true}";
        {std::ofstream original(retainedRoot/"Assets/Scenes/Main.scene");original<<retained;}
        if(!project.Create(retainedRoot,"Retained",error)) throw std::runtime_error(error);
        std::ifstream original(retainedRoot/"Assets/Scenes/Main.scene");const std::string actual((std::istreambuf_iterator<char>(original)),{});
        original.close();
        if(actual!=retained) throw std::runtime_error("Project creation overwrote existing startup content");
        std::filesystem::remove_all(root);
        std::cout << "PASS: project creation, initial EntityWorld, reopen and overwrite rejection\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
}
