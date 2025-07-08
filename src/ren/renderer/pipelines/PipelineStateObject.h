#pragma once

#include <vector>

#include <ren/renderer/Shader.h>
#include <ren/renderer/ShaderProgram.h>
#include <ren/misc/json_serialize.h>

namespace ren {


  enum class Topology : u8 {
    // For now, we will just use a few common topologies.
    TriangleList,
    LineList,
  };

  JSON_SERIALIZE_ENUM(Topology, {{Topology::TriangleList, "Triangle list"},
                                          {Topology::LineList, "Line list"}});

  enum class FillMode : u8 {
    Solid,      // Filled polygons
    Wireframe,  // Wireframe polygons
  };
  JSON_SERIALIZE_ENUM(FillMode, {{FillMode::Solid, "Solid"}, {FillMode::Wireframe, "Wireframe"}});

  enum class CullMode : u8 {
    None,
    Back,
    Front,
  };
  JSON_SERIALIZE_ENUM(CullMode, {{CullMode::None, "None"},
                                 {CullMode::Back, "Back"},
                                 {CullMode::Front, "Front"}});

  enum class BlendMode : u8 {
    None,            // No blending
    Alpha,           // Alpha blending
    Additive,        // Additive blending
    Subtractive,     // Subtractive blending
    Multiplicative,  // Multiplicative blending
  };
  JSON_SERIALIZE_ENUM(BlendMode, {{BlendMode::None, "None"},
                                  {BlendMode::Alpha, "Alpha"},
                                  {BlendMode::Additive, "Additive"},
                                  {BlendMode::Subtractive, "Subtractive"},
                                  {BlendMode::Multiplicative, "Multiplicative"}});

  // A pipeline state object (PSO) encapsulates the state needed to create a
  // graphics pipeline in the renderer.  The goal of this object is to abstract
  // away the details of the vulkan api enough that the user can simply construct
  // a PSO with desired shaders and state, and pass *just it* to the renderer which
  // will handle the rest through the PipelineCache.
  struct PipelineStateObject : public HasUUID {
    // The debug name of this pipeline state object.
    std::string debugName;
    // Perhaps the most important part of a pipeline state object is the shader
    // program that defines the bindings, layout, and functionality of the PSO.
    ref<ShaderProgram> program;

    // The topology of the geometry to be rendered.
    Topology topology = Topology::TriangleList;

    // If this pipeline should perform depth testing.
    bool depthTest = true;
    // If this pipeline should perform depth writing.
    bool depthWrite = true;
    // If depth clipping is enabled.
    bool depthClip = true;

    // The fill mode for the pipeline.
    FillMode fillMode = FillMode::Solid;
    // How to cull polygons. Default to back-facing.
    CullMode cullMode = CullMode::Back;
    // If the front face of polygons are counter-clockwise.
    bool frontCCW = true;

    // Depth bias settings
    float depthBias = 0.0f;
    float depthBiasClamp = 0.0f;
    float depthSlopeFactor = 0.0f;

    // Color blending mode. Currently does nothing.
    BlendMode blendMode = BlendMode::None;


    // TODO: abstract me!
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;


    // TODO: include more state here, such as:
    // - Binding descriptions
    // - Attribute descriptions

    void renderInspector();
    friend void to_json(json& j, const PipelineStateObject& pso);
    friend void from_json(const json& j, PipelineStateObject& pso);
  };


}  // namespace ren