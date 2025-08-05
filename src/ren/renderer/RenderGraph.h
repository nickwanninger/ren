#pragma once

#include <string>
#include <vector>
#include <map>
#include <ren/types.h>
#include <unordered_set>
#include <ren/core/UUID.h>

namespace ren {

  class RenderGraph;

  enum class GraphResourceFlags {
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    ReadWrite = Read | Write
  };

  struct GraphResourceUsage {
    std::string resourceName;
    GraphResourceFlags access;
    u32 pipelineStage;
    u32 accessMask;
  };

  struct GraphNodeDesc {
    UUID node_id;
    const std::string name;
    RenderGraph &graph;
  };



  class GraphResource {
   public:
    enum class Type { Buffer, Texture };


    explicit GraphResource(Type type, u32 index)
        : resourceType(type)
        , index(index) {}


    u32 getIndex() const { return index; }
    Type getType() const { return resourceType; }

    const std::unordered_set<u32> &getWrittenIn() const { return writtenIn; }
    const std::unordered_set<u32> &getReadIn() const { return readIn; }

    void setName(const std::string_view &name) { this->name = name; }
    const std::string &getName() const { return name; }

   private:
    Type resourceType;
    u32 index;

    // Which passes is a resource read/written in?
    std::unordered_set<u32> writtenIn;
    std::unordered_set<u32> readIn;

    std::string name;
  };


  class GraphTextureResource : public GraphResource {
   public:
    explicit GraphTextureResource(u32 index)
        : GraphResource(Type::Texture, index) {}
  };


  // ...


  class GraphNode {
   public:
    GraphNode(GraphNodeDesc &desc)
        : desc(desc) {}

    // Resource dependency management
    void addInput(const std::string &resourceName,
                  GraphResourceFlags access = GraphResourceFlags::Read);
    void addOutput(const std::string &resourceName,
                   GraphResourceFlags access = GraphResourceFlags::Write);


    UUID getNodeID(void) const { return desc.node_id; }
    RenderGraph &getGraph() const { return desc.graph; }



   protected:
    friend class RenderGraph;
    std::vector<GraphResourceUsage> inputs;
    std::vector<GraphResourceUsage> outputs;
    GraphNodeDesc desc;

    // This is updated by the RenderGraph when it is scheduling the graph.
    // These things are private to the RenderGraph and should not be modified.
    u32 outstandingDependencies = 0;
    bool ran = false;
  };

  // This render graph is a simple topological sort with dynamic scheduling.
  // It allows you to define Passes, attach input/output resources,
  // and then execute them in the correct order as those dependencies are met.
  // Resources are globally
  class RenderGraph {
   public:
    void run();

    ref<GraphNode> addNode(const std::string &name);

    void dump();
    bool locked = false;  // Once the graph is locked, no more nodes can be added.

    std::map<UUID, ref<GraphNode>> nodes;

    // Given a resource name, return the list of nodes which depend on it.
    std::map<std::string, std::vector<UUID>> resourceDependants;
    std::map<std::string, UUID> resourceProducers;

    void renderImGui();
  };
}  // namespace ren