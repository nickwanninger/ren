#include <ren/assets/MegaMeshBuffer.h>
#include <imgui/imgui.h>

namespace ren {

#define MB (1024 * 1024)
  MegaMeshBuffer::MegaMeshBuffer()
      : vertexBuffer((256 * MB) / sizeof(Vertex))  // start with 256MB of vertices
      , indexBuffer((16 * MB) / sizeof(u32))       // start with 16MB of indices

  {
    REN_PROFILE_FUNCTION();
  }

  MegaMeshBuffer::~MegaMeshBuffer() { REN_PROFILE_FUNCTION(); }



  MegaMeshHandle MegaMeshBuffer::allocate(std::span<const Vertex> vertices,
                                          std::span<const u32> indices) {
    REN_PROFILE_FUNCTION();

    MegaMeshHandle handle = nextMeshHandle++;
    MegaMeshEntry entry;
    entry.meshID = handle;



    entry.vertexOffset = vertexBuffer.allocate(vertices.size());
    entry.indexOffset = indexBuffer.allocate(indices.size());


    vertexBuffer.copyFromHost(vertices.data(), vertices.size(), entry.vertexOffset);
    indexBuffer.copyFromHost(indices.data(), indices.size(), entry.indexOffset);

    entry.vertexCount = static_cast<u32>(vertices.size());
    entry.indexCount = static_cast<u32>(indices.size());

    entries[handle] = entry;

    return handle;
  }


  void MegaMeshBuffer::dumpEntries(void) {
    ImGui::Begin("MegaMeshBuffer Entries");
    ImGui::Text("Total Meshes: %d", (int)entries.size());

    float vertexCommitted = vertexBuffer.committed() * sizeof(Vertex) / (1024.0f * 1024.0f);
    float indexCommitted = indexBuffer.committed() * sizeof(u32) / (1024.0f * 1024.0f);
    ImGui::Text("Total Vertex Buffer: %.2f MB (%.2f MB Committed)", vertexBuffer.getByteCount() / (1024.0f * 1024.0f), vertexCommitted);
    ImGui::Text("Total Index Buffer: %.2f MB (%.2f MB Committed)", indexBuffer.getByteCount() / (1024.0f * 1024.0f), indexCommitted);
    ImGui::Separator();
    ImGui::Text("Entries:");
    for (const auto &[handle, entry] : entries) {
      ImGui::Text("Mesh %3d: Verts %6d @ %6d, Idx %6d @ %6d", handle, entry.vertexCount,
                  entry.vertexOffset, entry.indexCount, entry.indexOffset);
    }
    ImGui::End();
  }
}  // namespace ren