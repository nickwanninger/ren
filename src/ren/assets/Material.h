#pragma once


#include <string>
#include <vector>
#include <ren/assets/NativeAsset.h>
#include <ren/renderer/Texture.h>
#include <ren/renderer/shader/ShaderProgram.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>

namespace ren {

  // A shader program, its pipeline state, and the CPU bytes of its reflected
  // material struct. The bytes are the source of truth for field values:
  // setValue() encodes JSON into them, and getValue() decodes them.
  //
  // A .material file stores the surface shader, the PSO, and the fields that
  // isStoredField() accepts: scalars, vectors and matrices whose names do not
  // start with '_'. Matrix values are arrays of columns. Use
  // deserializeNativeAsset() and serializeNativeAsset() for the whole file.
  class Material : public NativeAsset {
   public:
    static constexpr AssetType TYPE = AssetType::Material;
    // The version of the .material content.
    static constexpr u32 version = 1;

    Material() = default;
    explicit Material(ref<ShaderProgram> program);
    // Links the surface module with the deferred pass. Only a material made this way can be
    // saved, because the file stores the surface module, not the program.
    explicit Material(std::string_view surfaceShader);
    virtual ~Material() = default;

    // Makes a material from the content of a .material file. The surface shader links with the
    // deferred pass. A file field that the shader does not have, or does not store, gives a
    // warning. Throws if the content is malformed or a field value does not fit its field.
    static ref<Material> fromContent(AssetID id, const json &content);

    AssetType getType(void) const override { return TYPE; }

    // Writes the surface shader, PSO and stored fields.
    // Throws if the material did not come from a surface shader.
    json serializeContent(void) const override;

    // Materials cannot be moved or copied
    Material(const Material &) = delete;
    Material &operator=(const Material &) = delete;
    Material(Material &&) = delete;
    Material &operator=(Material &&) = delete;


    void setName(const std::string_view &name) { this->name = name; }
    const std::string &getName() const { return this->name; }


    // The shader defines the material's fields and their byte layout.
    const ref<ShaderProgram> &getProgram() const { return pso.program; }
    const ShaderReflection::MaterialSchema *getSchema() const;
    const std::vector<u8> &getCpuMaterialData() const { return cpuMaterialData; }

    // Encodes `value` into the field with the JSON value format. Throws if the field
    // does not exist, is not stored, or `value` does not fit it. A failed write changes no bytes.
    void setValue(std::string_view name, const json &value);
    // Decodes the field into the JSON value format. Throws as setValue() does.
    json getValue(std::string_view name) const;

    // Named writes use the reflected byte layout.
    void setFloat(std::string_view name, float value);
    void setFloat2(std::string_view name, const glm::vec2 &value);
    void setFloat3(std::string_view name, const glm::vec3 &value);
    void setFloat4(std::string_view name, const glm::vec4 &value);
    void setColor(std::string_view name, const glm::vec4 &color) { setFloat4(name, color); }
    void setTexture(std::string_view name, const ref<Texture> &texture);

    // Render the material to the imgui-based inspector.
    // This is used to inspect a material in the editor.
    virtual void inspect(void);

    // Legacy pass selection until rendering uses shader-backed materials.
    virtual bool isDeferred(void) const { return false; }
    
    ren::PipelineStateObject &getPSO() { return pso; }

    // Returns true if a .material file stores the field: a scalar, vector or matrix
    // whose name does not start with '_'.
    static bool isStoredField(const ShaderReflection::MaterialField &field);

    // Encode and decode one field of the JSON value format. `out` and `data` point to the
    // field's bytes, field.byteSize long. Throws if the field is not stored, its reflected
    // layout does not fit its shape, or `value` does not fit the field.
    static void encodeValue(const ShaderReflection::MaterialField &field, const json &value, u8 *out);
    static json decodeValue(const ShaderReflection::MaterialField &field, const u8 *data);

   private:
    const ShaderReflection::MaterialField &field(std::string_view name) const;
    void writeField(const ShaderReflection::MaterialField &field, ren::ByteSpan value);

    std::string name = "Unknown Material";
    // The surface module that fromContent() linked. Empty for a material made from a full program.
    std::string surfaceShader;

    ren::PipelineStateObject pso;
    // Zero-filled storage with the size of the reflected MaterialT.
    std::vector<u8> cpuMaterialData;

    // to hold refcounts of bound textures.
    std::map<std::string, ref<Texture>> boundTextures;
  };
}  // namespace ren