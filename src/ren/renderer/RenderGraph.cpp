#include <ren/renderer/RenderGraph.h>

#include <imgui/imgui.h>
#include <ImGuizmo/ImGuizmo.h>
#include <ImGuizmo/GraphEditor.h>
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

    // fmt::println("Added GraphNode '{}' with ID: {}", name, (u64)uuid);

    return node;
  }

  void RenderGraph::dump() {
    fmt::println("// https://dreampuf.github.io/GraphvizOnline/?engine=dot");
    fmt::println("digraph RenderGraph {{");
    // fmt::println("  rankdir=LR;");
    fmt::println("  node [shape=box];");

    for (const auto &[uuid, node] : nodes) {
      fmt::println("  // {} {}", (u64)uuid, node->desc.name);
    }

    for (const auto &[uuid, node] : nodes) {
      fmt::println("  subgraph cluster_{} {{ // {}", node->desc.name, (u64)uuid);
      fmt::println("     style=filled; color=lightgrey; label=\"{}\"", node->desc.name);
      fmt::println("     rp_{} [label=\"Pass\"];", (u64)uuid);

      // draw the inputs
      for (const auto &input : node->inputs) {
        // the inputs are nodes which point to this node.
        fmt::println("     input_{}_{} [label=\"{}\"];", input.resourceName, (u64)uuid,
                     input.resourceName);
        fmt::println("     input_{}_{} -> rp_{};", input.resourceName, (u64)uuid, (u64)uuid);
      }


      for (const auto &output : node->outputs) {
        // the outputs are nodes which point to this node.
        fmt::println("     output_{} [label=\"{}\"];", output.resourceName, output.resourceName);
        fmt::println("     rp_{} -> output_{};", (u64)uuid, output.resourceName);
      }
      fmt::println("  }}");
    }

    // Now draw the dependencies between nodes.

    for (auto &[name, uuids] : resourceDependants) {
      // fmt::println("  // Resource: {}", name);
      for (const auto &uuid : uuids) {
        // fmt::println("  {} -> rp_{};", name, (u64)uuid);
        fmt::println("  output_{} -> input_{}_{} [label=FOOO];", name, name, (u64)uuid);
      }
    }

    fmt::println("}}");
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
      auto node = readyNodes.front();
      REN_PROFILE_SCOPE(node->desc.name.data());
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



  void RenderGraph::renderImGui() {
    // draw the render graph using imgui and imguizmo
    ImGui::Begin("Render Graph");

    ImGui::Text("Render Graph Nodes: %zu", nodes.size());
    ImGui::Text("Resource Dependants: %zu", resourceDependants.size());
    ImGui::Text("Resource Producers: %zu", resourceProducers.size());
    ImGui::Separator();
    ImGui::Text("Nodes:");

    for (const auto &[uuid, node] : nodes) {
      ImGui::PushID((u64)uuid);
      ImGui::Text("Node: %s (ID: %llu)", node->desc.name.c_str(), (u64)uuid);
      ImGui::Text("Inputs:");
      for (const auto &input : node->inputs) {
        ImGui::Text("  - %s (Access: %d)", input.resourceName.c_str(), (int)input.access);
      }
      ImGui::Text("Outputs:");
      for (const auto &output : node->outputs) {
        ImGui::Text("  - %s (Access: %d)", output.resourceName.c_str(), (int)output.access);
      }
      ImGui::PopID();
    }



    ImGui::End();
  }

}  // namespace ren