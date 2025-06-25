#include <ren/renderer/RenderGraph.h>
#include <unordered_map>

namespace ren {




  void GraphNode::addInput(const std::string &resourceName, GraphResourceFlags access) {
    // Add this node to the list of dependants for this resource.
    auto &dependants = desc.graph.resourceDependants[resourceName];
    dependants.push_back(getNodeID());

    GraphResourceUsage usage{resourceName, access, 0, 0};
    inputs.push_back(usage);
  }

  void GraphNode::addOutput(const std::string &resourceName, GraphResourceFlags access) {
    // if (desc.graph.resourceProducers.contains(resourceName)) {
    //   fmt::print("Error: Resource '{}' already has a producer.\n", resourceName);
    //   return;
    // }


    // Add this node as the producer for this resource.
    desc.graph.resourceProducers[resourceName] = getNodeID();

    // Add the output to the list of outputs for this node.
    GraphResourceUsage usage{resourceName, access, 0, 0};
    outputs.push_back(usage);
  }




  ref<GraphNode> RenderGraph::addNode(const std::string &name) {
    auto uuid = UUID();
    GraphNodeDesc desc{uuid, name, *this};
    auto node = makeRef<GraphNode>(desc);
    nodes[uuid] = node;

    fmt::println("Added GraphNode '{}' with ID: {}", name, (u64)uuid);

    return node;
  }

  void RenderGraph::dump() {
    fmt::println("RenderGraph Dump:");
    for (const auto &[uuid, node] : nodes) {
      fmt::println("Node ID: {}, Name: {}", (u64)uuid, node->desc.name);
      fmt::println("  Inputs:");
      for (const auto &input : node->inputs) {
        fmt::println("    - {} (Access: {})", input.resourceName, (u32)input.access);
      }
      fmt::println("  Outputs:");
      for (const auto &output : node->outputs) {
        fmt::println("    - {} (Access: {})", output.resourceName, (u32)output.access);
      }
    }

    fmt::println("Resource Dependants:");
    for (const auto &[resourceName, dependants] : resourceDependants) {
      fmt::println("Resource '{}':", resourceName);
      for (const auto &depUUID : dependants) {
        fmt::println("  - Node ID: {}", (u64)depUUID);
      }
    }

    fmt::println("Resource Producers:");
    for (const auto &[resourceName, producerUUID] : resourceProducers) {
      fmt::println("Resource '{}': Node ID: {}", resourceName, (u64)producerUUID);
    }
  }

  void RenderGraph::run() {
    REN_PROFILE_FUNCTION();
    // The ready nodes queue is populated with nodes that have no outstanding dependencies.
    std::queue<ref<GraphNode>> readyNodes;
    // Initialize the dependency count for each node.
    for (const auto &[uuid, node] : nodes) {
      node->outstandingDependencies = node->inputs.size();
      node->ran = false;

      // If the node has no dependencies, add it to the ready queue.
      if (node->outstandingDependencies == 0) readyNodes.push(node);
    }

    auto outputDone = [&](std::string resourceName) {
      REN_PROFILE_SCOPE("Propegate Outputs");
      // This function will decrement the dependency
      // count for all nodes that depend on this resource.
      for (const auto &nodeUUID : resourceDependants[resourceName]) {
        auto &node = nodes[nodeUUID];
        node->outstandingDependencies--;

        // If the node has no outstanding dependencies, add it to the ready queue.
        if (node->outstandingDependencies == 0 && !node->ran) { readyNodes.push(node); }
      }
    };

    // Go through the deps map and find nodes with no dependencies.
    while (!readyNodes.empty()) {
      REN_PROFILE_SCOPE("Process Ready Node");
      auto node = readyNodes.front();
      readyNodes.pop();

      // If the node has already run, skip it.
      if (node->ran) continue;

      // Mark the node as ran.
      node->ran = true;

      fmt::println("Running node: {} (ID: {})", node->desc.name, (u64)node->getNodeID());

      // Execute the node's logic here.
      // This is where you would call the node's execute function or similar.
      // For now, we just simulate it by printing the name.
      // In a real implementation, you would have a method to execute the node's logic.

      // After running the node, output done for each of its outputs.
      for (const auto &output : node->outputs) {
        outputDone(output.resourceName);
      }
    }
  }

}  // namespace ren