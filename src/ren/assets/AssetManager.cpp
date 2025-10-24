#include <ren/assets/AssetManager.h>

#include <ren/renderer/Texture.h>
#include <ren/renderer/Shader.h>
#include <ren/core/Application.h>
#include <imgui/imgui.h>
#include <ren/misc/hash.h>

namespace ren {

  AssetManager &getAssetManager() { return ren::world().get_mut<AssetManager>(); }

  AssetManager::AssetManager() {}

  AssetManager::~AssetManager() { fmt::print("AssetManager destroyed.\n"); }


  void AssetManager::addFilesystemSource(const std::string_view &path) {
    this->source.addFilesystem(path);
  }
  void AssetManager::addEmbeddedSource(void) { this->source.addEmbeddedSource(); }


  AssetID AssetManager::getID(const std::string_view &name) {
    u64 state = 0;
    // For now, simply hash. Later on we might need to do this centrally.
    // It's unlikely to get a collision, but you never know.
    ren::hashStd(state, name);
    return state;
  }


  ref<Asset> AssetManager::load(const std::string_view &path, AssetType type) {
    REN_PROFILE_SCOPE("Get Asset");
    // Check if the asset is already loaded
    for (const auto &pair : assetRegistry) {
      if (pair.second.path == path) {
        fmt::print("Asset {} is already imported with ID {}\n", path, (u64)pair.first);
        return getAsset(pair.second.id);
      }
    }


    if (type == AssetType::Unknown) {
      auto extension = [&](const char *ext, AssetType t) {
        // I don't have string_view::ends_with, so we do it manually.
        if (path.size() >= strlen(ext) &&
            path.compare(path.size() - strlen(ext), strlen(ext), ext) == 0) {
          type = t;
          fmt::print("Detected asset type {} for path {}\n", static_cast<int>(t), path);
        }
      };

      // Texture formats we support
      extension(".png", AssetType::Texture);
      extension(".jpg", AssetType::Texture);
      extension(".jpeg", AssetType::Texture);

      // Mesh formats we support
      extension(".glb", AssetType::Mesh);
      extension(".gltf", AssetType::Mesh);
      extension(".obj", AssetType::Mesh);
      extension(".fbx", AssetType::Mesh);

      // Shader formats we support
      extension(".spv", AssetType::Shader);
      extension(".vert", AssetType::Shader);
      extension(".frag", AssetType::Shader);
      extension(".comp", AssetType::Shader);
      extension(".geom", AssetType::Shader);


      // Scripts
      extension(".lua", AssetType::Script);
      extension(".fnl", AssetType::Script);
    }

    // Create a new AssetInfo
    AssetInfo info;
    info.id = UUID();  // Generate a new UUID for the asset
    info.path = path;
    info.type = type;
    assetRegistry[info.id] = info;  // Store the asset info in the map
    // sync();                         // sync the asset state!

    return load(info);
  }



  ref<Asset> AssetManager::getAsset(ren::AssetID assetID) {
    REN_PROFILE_SCOPE("Get Asset");
    // First, lookup the asset in the cached asset map.
    if (auto it = loadedAssets.find(assetID); it != loadedAssets.end()) {
      return it->second;  // Return the cached asset if found
    }

    // If it isn't loaded, load the asset if we can.
    if (auto it = assetRegistry.find(assetID); it != assetRegistry.end()) {
      // if the cached asset is not loaded, load it.
      return load(it->second);
    } else {
      fmt::print("Asset with ID {} is not found in the asset registry\n", (u64)assetID);
    }
    return nullptr;  // Asset not found
  }




  ref<Asset> AssetManager::load(const AssetInfo &info) {
    REN_PROFILE_SCOPE("Load Asset");
    std::string path = info.path;
    ref<Asset> asset = nullptr;

    // Load!
    // TODO: asset loader abstraction
    switch (info.type) {
      case AssetType::Texture: {
        asset = ren::Texture::load(path);
        break;
      }
      case AssetType::Mesh: {
        fmt::print("IMPORTING MESH FROM {} IS NOT IMPLEMENTED\n", path);
        break;
      }
      case AssetType::Shader: {
        fmt::print("Importing shader asset from {}\n", path);
        // Load the shader from the file
        auto stage = Shader::getStageFromFilename(path);
        auto shader = makeRef<Shader>(path, stage);
        if (shader) {
          // Register the shader asset
          shader->setAssetID(info.id);
          loadedAssets[info.id] = shader;  // Cache the loaded shader
          fmt::print("Shader asset {} imported successfully.\n", (u64)info.id);
          return shader;  // Return the loaded shader
        }
        asset = shader;
      }
      case AssetType::Material: {
        fmt::print("IMPORTING MATERIAL FROM {} IS NOT IMPLEMENTED\n", path);
        break;
      }


      case AssetType::Unknown:
      default: {
        fmt::print("Unknown asset type for path {}\n", path);
        break;
      }
    }

    if (asset) {
      asset->setAssetID(info.id);     // Set the asset ID for the asset
      loadedAssets[info.id] = asset;  // Cache the loaded asset
    } else {
      fmt::print("Failed to import asset from path {}\n", path);
    }


    return asset;
  }


  // void AssetManager::inspect(void) {
  //   REN_PROFILE_SCOPE("AssetManager Inspect");
  //   ImGui::Text("Asset Manager");
  //   ImGui::Separator();
  //   if (ImGui::Button("Sync Assets")) {
  //     sync();
  //     fmt::print("Assets synced to disk.\n");
  //   }
  //   ImGui::Text("Assets Loaded: %zu", loadedAssets.size());
  //   ImGui::Text("Assets Registered: %zu", assetRegistry.size());
  //   // List all assets
  //   for (const auto &pair : assetRegistry) {
  //     const auto &assetInfo = pair.second;
  //     ImGui::Text("Asset ID: %llu, Path: %s, Type: %s", (u64)assetInfo.id,
  //                 assetInfo.path.string().c_str(), json(assetInfo.type).dump().c_str());
  //   }
  // }

  bool loadAssetBytes(const std::string_view &path, std::vector<u8> &out) {
    return getAssetManager().load(path, out);
  }



  // Utility: module name "a.b.c" -> "a/b/c"
  inline std::string dot_to_slash(std::string s) {
    std::replace(s.begin(), s.end(), '.', '/');
    return s;
  }

  // A tiny path expander similar to package.path semantics.
  // We’ll try <name>.lua, <name>/init.lua, and allow a configurable prefix like "scripts/".
  inline void build_candidate_keys(const std::string &mod, const std::string &prefix,
                                   std::vector<std::string> &out) {
    const std::string base = dot_to_slash(mod);
    out.push_back(prefix + base + ".lua");
    out.push_back(prefix + base + "/init.lua");  // init
    out.push_back(prefix + base + ".fnl");
    out.push_back(prefix + base + "/init.fnl");  // init
  }


  static int loadFennelScript(lua_State *L, std::vector<u8> &scriptData, const char *filename) {
    // Load the fennel compiler if not already loaded
    // Use plain Lua C API to require fennel and call compileString.
    // Stack: [...] before; we will clean up before continuing.
    lua_getglobal(L, "require");            // push require
    lua_pushstring(L, "fennel");            // arg
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {  // call require("fennel")
      const char *err = lua_tostring(L, -1);
      fmt::println("Failed to require fennel: {}", err ? err : "unknown error");
      abort();
    }

    // Now fennel module is on top of the stack.
    if (!lua_istable(L, -1)) {
      fmt::println("fennel.require did not return a table");
      abort();
    }

    // Get fennel.compileString
    lua_getfield(L, -1, "compileString");  // pushes function
    if (!lua_isfunction(L, -1)) {
      fmt::println("fennel.compileString not found or not a function");
      abort();
    }

    // Push arguments: source string and options table { filename = candidate }
    lua_pushlstring(L, (const char *)scriptData.data(), scriptData.size());
    lua_newtable(L);
    lua_pushstring(L, "filename");
    lua_pushstring(L, filename);
    lua_settable(L, -3);  // options["filename"] = candidate

    // Call compileString(source, options)
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
      const char *err = lua_tostring(L, -1);
      fmt::println("fennel.compileString error: {}", err ? err : "unknown error");
      return lua_error(L);
      // lua_pop(L, 1);  // pop error
      // lua_pop(L, 1);  // pop module
      // return 0;
    }

    // Result should be a string containing compiled Lua
    size_t compiled_len = 0;
    const char *compiled_cstr = lua_tolstring(L, -1, &compiled_len);
    if (!compiled_cstr) {
      fmt::println("fennel.compileString did not return a string");
      lua_pop(L, 1);  // pop result
      return 0;
    }

    std::string compiledCode(compiled_cstr, compiled_len);
    lua_pop(L, 1);  // pop compiled result
    lua_pop(L, 1);  // pop fennel module

    if (luaL_loadbuffer(L, compiledCode.c_str(), compiledCode.size(), filename) == LUA_OK) {
      return 1;  // return the loaded chunk
    } else {
      return lua_error(L);
    }
  }

  void AssetManager::configureLua(sol::state &lua) {
    // Override the normal filesystem loader with our custom one.
    lua["package"]["loaders"][2] = +[](lua_State *L) -> int {
      const char *modname = luaL_checkstring(L, 1);
      auto &am = ren::Application::get().getAssetManager();
      std::vector<std::string> candidates;
      // Load everything from the scripts/ directory.
      build_candidate_keys(modname, "scripts/", candidates);
      // build_candidate_keys(modname, "", candidates);
      std::vector<u8> scriptData;
      for (auto &candidate : candidates) {
        scriptData.clear();
        if (am.load(candidate, scriptData)) {
          // if the script is found, and it ends in .lua, load as lua.
          if (candidate.ends_with(".lua")) {
            if (luaL_loadbuffer(L, (const char *)scriptData.data(), scriptData.size(),
                                candidate.c_str()) == LUA_OK) {
              return 1;  // return the loaded chunk
            } else {
              // Loading failed; return the error message
              return lua_error(L);
            }
          }

          // if it ends in .fnl, load as fennel.
          if (candidate.ends_with(".fnl")) {
            return loadFennelScript(L, scriptData, candidate.c_str());
          }
        }
      }

      fmt::println("[!!] Failed to load lua module '{}'\n", modname);
      // On failure, return nil and an error string.
      lua_pushnil(L);
      lua_pushfstring(L, "Module '%s' not found", modname);
      return 2;
    };
    // remove loaders 3 and 4 (C loader and all else fails)
    lua["package"]["loaders"][3] = nullptr;
    lua["package"]["loaders"][4] = nullptr;
  }

}  // namespace ren
