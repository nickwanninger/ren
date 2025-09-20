#pragma once

#include <ren/types.h>
#include <ren/renderer/Buffer.h>
#include <ren/assets/Vertex.h>

namespace ren {

  using MegaMeshHandle = u32;

  struct MegaMeshEntry {
    MegaMeshHandle meshID = 0;
    u32 vertexOffset = 0;  // TODO: not sure if u32 is enough.
    u32 indexOffset = 0;
    u32 vertexCount = 0;
    u32 indexCount = 0;
  };


  // This class implements a "Mega Mesh Buffer", which is a large buffer that
  // holds all mesh data to accelerate rendering through indirect draws.
  class MegaMeshBuffer {
   public:
    MegaMeshBuffer();
    ~MegaMeshBuffer();

    MegaMeshHandle allocate(std::span<const Vertex> vertices, std::span<const u32> indices);

    void dumpEntries(void);

    inline void bind(VkCommandBuffer cmd) {
      ren::bind(cmd, vertexBuffer);
      ren::bind(cmd, indexBuffer);
    }

    const auto &getEntry(MegaMeshHandle handle) const { return entries.at(handle); }

    auto &getIndexBuffer(void) { return indexBuffer; }
    auto &getVertexBuffer(void) { return vertexBuffer; }

   private:
    MegaMeshHandle nextMeshHandle = 1;
    std::unordered_map<MegaMeshHandle, MegaMeshEntry> entries;

    ren::ArenaBuffer<Vertex, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT> vertexBuffer;
    ren::ArenaBuffer<u32, VK_BUFFER_USAGE_INDEX_BUFFER_BIT> indexBuffer;
  };
}  // namespace ren