#pragma once


#include <string>
#include <vector>
#include <ren/assets/Handle.h>
#include <ren/assets/NativeAsset.h>
#include <ren/renderer/Texture.h>
#include <ren/renderer/shader/ShaderProgram.h>
#include <ren/renderer/pipelines/PipelineStateObject.h>

namespace ren {

  // A shader program, its pipeline state, and the CPU bytes of its reflected
  // material struct. The bytes are the source of truth for field values:
  // setValue() encodes JSON into them, and getValue() decodes them.
  //
  // A texture field holds a Handle<Texture>. Its bytes are the bindless index of
  // the texture, which resolveTextures() writes before the material is used.
  //
  // A .material file stores the surface shader, the PSO, the value fields that
  // isStoredField() accepts, and the texture fields that isTextureField() accepts.
  // Value fields are scalars, vectors and matrices whose names do not start with
  // '_'; matrix values are arrays of columns. A texture field stores the asset ID
  // of its handle, or null. Use deserializeNativeAsset() and serializeNativeAsset()
  // for the whole file.
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

    // Sets the texture of a texture field. The handle takes an asset ID or a Texture, and an
    // empty handle clears the field. The field's bytes are 0, which the shader treats as no
    // texture, until resolveTextures(). Throws if the field is not a texture field.
    void setTexture(std::string_view name, Handle<Texture> texture);
    // Returns an empty handle if the field has no texture. Throws as setTexture() does.
    Handle<Texture> getTexture(std::string_view name) const;

    // Resolves the handle of each texture field and writes its bindless index into the bytes.
    // A handle that does not resolve writes 0. Call on the main thread before the bytes are used.
    void resolveTextures(void);

    // Render the material to the imgui-based inspector.
    // This is used to inspect a material in the editor.
    virtual void inspect(void);

    // Legacy pass selection until rendering uses shader-backed materials.
    virtual bool isDeferred(void) const { return false; }
    
    ren::PipelineStateObject &getPSO() { return pso; }

    // Returns true if a .material file stores the field: a scalar, vector or matrix
    // whose name does not start with '_'.
    static bool isStoredField(const ShaderReflection::MaterialField &field);

    // Returns true if the field holds a texture: a BindlessTexture whose name does not start
    // with '_'.
    static bool isTextureField(const ShaderReflection::MaterialField &field);

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

    // The texture of each texture field that has one.
    std::map<std::string, Handle<Texture>> textures;
    // The Textures that resolveTextures() wrote into the bytes. They stay alive while the bytes
    // refer to them, even if a reimport replaces the asset.
    std::map<std::string, ref<Texture>> resolvedTextures;
  };
}  // namespace ren