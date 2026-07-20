#pragma once
#include <map>
#include <string>
#include "types.h"

namespace Awesome {

    class AwesomeGraphics;
    class SceneLoader {
    public:
        static bool LoadScene(AwesomeGraphics* Awesome, const char* name, const char* path, std::map<std::string, std::tuple<uint32, uint32>>& meshMap);
    };

};
