#include "SceneLoader.h"
#include "Awesome.h"
#include "Scene.h"
#include "IBL.h"
#include "Util.h"
#include <DirectXMath.h>
#include "toml++/toml.h"
#include <random>

using namespace Awesome;
using namespace DirectX;

// https://stackoverflow.com/questions/686353/random-float-number-generation
float randomRange(float low, float high)
{
    return low + static_cast <float> (rand()) / (static_cast <float> (RAND_MAX / (high - low)));
}

float lerp(float a, float b, float s)
{
    return ((1.0f - s) * a) + (s * b);
}

bool SceneLoader::LoadScene(AwesomeGraphics* Awesome, const char* name, const char* path, std::map<std::string, std::tuple<uint32, uint32>>& meshMap)
{
    DebugPrint("Loading Scene: %s\n", path);
    try
    {
        toml::table sceneTable = toml::parse_file(path);
        Scene scene(Awesome);
        scene.SetName(name);
        // Objects
        if (toml::array* objects = sceneTable["Object"].as_array())
        {
            for (toml::node& object : *objects)
            {
                toml::table& oTable = *object.as_table();

                std::string name = oTable["name"].value<std::string>().value();
                std::string meshName = oTable["mesh"].value<std::string>().value();
                toml::array& posData = *oTable["position"].as_array();
                toml::array& rotData = *oTable["rotation"].as_array();
                toml::array& scaleData = *oTable["scale"].as_array();
                bool visible = oTable["visible"].value_or<bool>(true);

                uint32 startIndex = invalidIndex32;
                uint32 endIndex = invalidIndex32;
                if (meshMap.find(meshName) != meshMap.end())
                {
                    startIndex = std::get<0>(meshMap[meshName]);
                    endIndex = std::get<1>(meshMap[meshName]);
                }
                if (startIndex != invalidIndex32)
                {
                    Transform xform = { 
                        {
                            *posData[0].value<float>(),
                            *posData[1].value<float>(),
                            *posData[2].value<float>()
                        },
                        {
                            *rotData[0].value<float>(),
                            *rotData[1].value<float>(),
                            *rotData[2].value<float>()
                        },
                        {
                            *scaleData[0].value<float>(),
                            *scaleData[1].value<float>(),
                            *scaleData[2].value<float>()
                        } };

                    xform.CalcMatrix();

                    SceneObject& obj = scene.AddObject(xform);
                    obj.name = std::string(name);
                    obj.visibile = visible;
                    for (uint32 i = startIndex; i < endIndex + 1; i++)
                        obj.meshes.push_back(i);
                }
            }
            
        }

        // IBLs
        if (toml::array* Materials = sceneTable["IBL"].as_array())
        {
            for (toml::node& material : *Materials)
            {
                toml::table& mTable = *material.as_table();
                auto name = mTable["name"].value<std::string>().value();
                uint32 ibl = Awesome->GetIBLSystem()->GetIBLByName(name);
                if (ibl != invalidIndex32)
                {
                    scene.SetIBL(ibl);
                }

            }
        }

        // Camera
        {
            // TODO: Move this to the scene file
            {
                Awesome::Camera* cam = scene.GetCamera();

                uint16 width = 1920;
                uint16 height = 1080;
                cam->width = width;
                cam->height = height;
                cam->aspectRatio = (float)width / (float)height;
                cam->horizontalFOV = 90.0f;
                cam->verticalFOV = cam->CalcVerticalFOV();
                cam->nearClip = 0.1f;
                cam->farClip = 5000.0f;
                cam->type = Awesome::CameraType::Perspevtive;
                cam->transform.position = {0.0f, 4.50f, 9.0f};
                cam->transform.rotation = { };//0.0f, -90.0f, 22.5f };
                cam->target = Awesome::Transform::Zero;
                cam->distance = 10.f;
                cam->StartFrame();
                cam->EndFrame();

            }
        }

        //Lights
        {
            scene.GetSunLight()->UpdateMatrix();

#if 0
            // Let's just a add a bunch of lights to test with
            std::default_random_engine gen;

            // values near the mean are the most likely
            // standard deviation affects the dispersion of generated values from the mean
            std::normal_distribution d{ 5.0, 2.0 };

            // draw a sample from the normal distribution and round it to an integer
            auto random_int = [&d, &gen] { return std::round(d(gen)); };

            std::map<int, int> hist{};
            for (int n = 0; n != 1000; ++n)
            {
                int val = random_int();
                val = std::clamp(val, 0, 10);
                val = val - 5;

                if (val < 0)
                    val = 11 + val;
                ++hist[val];

            }

            for (auto [x, y] : hist)
                DebugPrint("%d %s\n", x, std::string(y/25, '*'));

#endif
#if 1
            // Let's just a add a bunch of lights to test with
            std::default_random_engine generator;

            std::normal_distribution<float> posDist(5.0f, 2.5f);
            std::normal_distribution<float> intensityDist(2.75f, 1.5f);
            std::normal_distribution<float> colorDist(0.5f, 0.25f);
            std::normal_distribution<float> speedDist(1.5f, 0.05f);

            uint32 total = static_cast<uint32>(std::pow(2, 12));
            DebugPrint("Total Lights for scene %d\n", total);
            for (uint32 l = 0; l < total; l++)
            {
                float posXBase = std::clamp(posDist(generator), 0.0f, 10.0f) - 5.0f;
                float posYBase = std::clamp(posDist(generator), 0.0f, 10.0f) - 5.0f;
                float posZBase = std::clamp(posDist(generator), 0.0f, 10.0f) - 5.0f;

                posXBase = posXBase < 0 ? 11.f + posXBase : posXBase;
                posYBase = posYBase < 0 ? 11.f + posYBase : posYBase;
                posZBase = posZBase < 0 ? 11.f + posZBase : posZBase;

                float intensityBase = std::clamp(intensityDist(generator), 0.5f, 5.0f);
                float redBase = std::clamp(colorDist(generator), 0.0f, 1.0f);
                float greenBase = std::clamp(colorDist(generator), 0.0f, 1.0f);
                float blueBase = std::clamp(colorDist(generator), 0.0f, 1.0f);

                Light light = {};
                light.type = LightType::Point;
                light.position.x = lerp(-45.0f, 45.0f, posXBase / 10.0f);
                light.position.y = lerp(1.0f, 81.0f, posYBase / 10.0f);
                light.position.z = lerp(-30.0f, 30.0f, posZBase / 10.0f);
                light.intensity = intensityBase;
                light.color = { redBase, greenBase, blueBase };
                light.speed = abs(speedDist(generator));
                
                scene.AddLight(light);
            }
#endif
        }

        Awesome->AddScene(scene);
    }
    catch (const toml::parse_error& error)
    {
        DebugPrint("What: %s\nDescription: %s\n", error.what(), error.description().data());
        return false;
    }
    return true;
}
