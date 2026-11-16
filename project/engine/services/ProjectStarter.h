#pragma once

#include "engine/base/AtomicFile.h"

namespace Engine::Services {

enum class ProjectStarter { Empty, Sprite2D };

inline bool PopulateProjectStarter(const std::filesystem::path& stage, ProjectStarter starter,
    std::string& error)
{
    if (starter == ProjectStarter::Empty) return true;
    // A generated white pixel. The authored Sprite colors supply the palette.
    static constexpr unsigned char pixel[] = {
        0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
        0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
        0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,
        0x10,0x49,0x44,0x41,0x54,0x78,0x01,0x01,0x05,0x00,0xfa,0xff,
        0x00,0xff,0xff,0xff,0xff,0x09,0xfb,0x03,0xfd,0xa3,0xd1,0x49,
        0x0a,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,
        0x82,
    };
    static constexpr char scene[] = R"scene({
      "version": 1,
      "entities": [
        {
          "id": 1, "parent": 0, "name": "Canvas", "active": true, "model": "",
          "transform": {"position":[0,0,0], "rotation":[0,0,0], "scale":[1,1,1]},
          "canvas": {"enabled":true, "referenceSize":[1280,720], "scaleMode":"scaleWithOutput", "sortOrder":0}
        },
        {
          "id": 2, "parent": 1, "name": "Board", "active": true, "model": "",
          "transform": {"position":[640,360,0], "rotation":[0,0,0], "scale":[1,1,1]},
          "sprite": {"enabled":true,"asset":"Assets/white.png","size":[820,440],"pivot":[0.5,0.5],"color":[0.05,0.12,0.23,1],"sortOrder":0}
        },
        {
          "id": 3, "parent": 1, "name": "Player", "active": true, "model": "",
          "transform": {"position":[500,330,0], "rotation":[0,0,0], "scale":[1,1,1]},
          "sprite": {"enabled":true,"asset":"Assets/white.png","size":[120,120],"pivot":[0.5,0.5],"color":[0.18,0.62,0.94,1],"sortOrder":1}
        },
        {
          "id": 4, "parent": 1, "name": "Goal", "active": true, "model": "",
          "transform": {"position":[785,330,0], "rotation":[0,0,0], "scale":[1,1,1]},
          "sprite": {"enabled":true,"asset":"Assets/white.png","size":[96,96],"pivot":[0.5,0.5],"color":[1,0.69,0.25,1],"sortOrder":1}
        },
        {
          "id": 5, "parent": 1, "name": "Platform", "active": true, "model": "",
          "transform": {"position":[640,485,0], "rotation":[0,0,0], "scale":[1,1,1]},
          "sprite": {"enabled":true,"asset":"Assets/white.png","size":[590,28],"pivot":[0.5,0.5],"color":[0.32,0.7,0.64,1],"sortOrder":1}
        }
      ]
    })scene";
    return Base::AtomicFile::Write(stage / L"Assets/white.png",
        std::string(reinterpret_cast<const char*>(pixel),sizeof(pixel)),4096,error,false) &&
        Base::AtomicFile::Write(stage / L"Assets/Scenes/Main.scene",scene,16384,error);
}

} // namespace Engine::Services
