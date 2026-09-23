#pragma once

#include <ren/types.h>
#include <ren/renderer/shader/ShaderProgram.h>
#include <slang.h>
#include <slang-com-ptr.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ren {

  /**
   * ShaderCache is the only part of the engine that knows Slang exists.
   *
   * It owns the Slang global session and a single shared ISession (so common
   * modules like `ren/core.slang` are parsed once for the whole engine), maps
   * a shader *name* onto a compiled ShaderProgram, and hands out the same
   * program to every caller that asks for it.
   *
   * Keys are canonicalized, so `demo/cube`, `demo/cube.slang`, `./demo/cube`,
   * and `shaders/demo/cube` are all the same cache entry.
   */
  class ShaderCache {
   public:
    ShaderCache();
    ~ShaderCache();

    // No copy, no move
    ShaderCache(const ShaderCache &) = delete;
    ShaderCache &operator=(const ShaderCache &) = delete;
    ShaderCache(ShaderCache &&) = delete;
    ShaderCache &operator=(ShaderCache &&) = delete;

    // Get (compiling on a miss) the program for `path`. Throws if the shader
    // cannot be compiled, matching the old `make<ShaderProgram>` behavior.
    // Failures are cached too: a broken shader is not recompiled on every call,
    // the recorded error is simply rethrown.
    ref<ShaderProgram> get(std::string_view path);

    // As `get`, but returns nullptr instead of throwing.
    ref<ShaderProgram> tryGet(std::string_view path);

    // Map an arbitrary shader name onto its canonical cache key. Strips a
    // `.slang` extension and a redundant leading `shaders/`, and normalizes
    // separators plus `.` / `..` segments.
    static std::string canonicalize(std::string_view path);

    // Drop every cached program and the Slang session. Callers still holding a
    // ref keep their old program; subsequent gets recompile from scratch.
    void clear(void);

    size_t size(void) const { return entries.size(); }

    // Inspect the cache in imgui.
    void inspect(void);

   private:
    struct Entry {
      ref<ShaderProgram> program;  // null when the compile failed
      std::string error;           // non-empty when the compile failed
      f64 compileMillis = 0.0;
      std::vector<std::string> dependencies;  // source files the compile touched
    };

    Entry &lookup(std::string_view path);

    // Create the global/shared session if needed. Returns true if the session
    // was (re)built, which invalidates everything compiled before it.
    bool ensureSession(void);

    // Run the Slang front end. Throws on any diagnostic.
    CompiledProgram compile(const std::string &canonicalPath);

    // Warn when two keys differ only by case: they are the same file on macOS
    // but two different files on Linux.
    void checkCaseCollision(const std::string &canonicalPath);

    Slang::ComPtr<slang::IGlobalSession> globalSession;
    Slang::ComPtr<slang::ISession> session;
    int sessionOptLevel = -1;

    std::unordered_map<std::string, Entry> entries;
    std::string selected;  // inspector selection
    std::unordered_map<std::string, std::string> lowercaseKeys;
  };


  // The primary way to load a shader. Resolves through the Renderer's cache.
  ref<ShaderProgram> getShader(std::string_view path);

}  // namespace ren
