#pragma once


#include <string>
#include <vector>
#include <ren/assets/Asset.h>
#include <ren/renderer/shader/ShaderProgram.h>

namespace ren {

  class Material : public ren::MaterialAsset {
   public:
    // The default constructor remains for PBRMaterial until the importer moves
    // to shader-backed materials.
    Material() = default;
    explicit Material(ref<ShaderProgram> program);
    explicit Material(std::string_view shader);
    virtual ~Material() = default;

    // Materials cannot be moved or copied
    Material(const Material &) = delete;
    Material &operator=(const Material &) = delete;
    Material(Material &&) = delete;
    Material &operator=(Material &&) = delete;


    void setName(const std::string_view &name) { this->name = name; }
    const std::string &getName() const { return this->name; }


    // The shader defines the material's fields and their byte layout.
    const ref<ShaderProgram> &getProgram() const { return program; }
    const ShaderReflection::MaterialSchema *getSchema() const;
    const std::vector<u8> &getCpuMaterialData() const { return cpuMaterialData; }

    // Named writes use the reflected byte layout. Texture writes follow later.
    void setFloat(std::string_view name, float value);
    void setColor(std::string_view name, const glm::vec4 &color);

    // Render the material to the imgui-based inspector.
    // This is used to inspect a material in the editor.
    virtual void inspect(void);

    // Legacy pass selection until rendering uses shader-backed materials.
    virtual bool isDeferred(void) const { return false; }


   private:
    const ShaderReflection::MaterialField &field(std::string_view name) const;
    void writeField(const ShaderReflection::MaterialField &field, ren::ByteSpan value);

    std::string name = "Unknown Material";


    ref<ShaderProgram> program;
    // Zero-filled storage with the size of the reflected MaterialT.
    std::vector<u8> cpuMaterialData;
  };
}  // namespace ren