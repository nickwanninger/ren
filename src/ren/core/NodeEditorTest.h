#pragma once
#include <imgui/imgui.h>
#include <imnodes/imnodes.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>

namespace ren {

  class NodeGraphEditor {
   public:
    enum class PinKind { Input, Output };

    struct Pin {
      int id;
      int node_id;
      PinKind kind;
      std::string name;
      // You can extend this with type info for validation
    };

    struct Node {
      int id;
      std::string name;
      ImVec2 position;
      std::vector<int> input_pins;
      std::vector<int> output_pins;
      // Optional: void* user_data for node-specific state
    };

    struct Link {
      int id;
      int start_pin;
      int end_pin;
    };

    NodeGraphEditor() {
      // ImNodes::CreateContext();
      // ImNodes::StyleColorsDark();

      // // Optional: customize style
      // ImNodes::GetStyle().Flags |= ImNodesStyleFlags_GridLines;
    }

    ~NodeGraphEditor() {
      // ImNodes::DestroyContext();
    }

    void display() {
      ImNodes::BeginNodeEditor();

      // Render all nodes
      for (const auto& node : nodes_) {
        ImNodes::BeginNode(node.id);

        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(node.name.c_str());
        ImNodes::EndNodeTitleBar();

        // Input pins
        for (int pin_id : node.input_pins) {
          const Pin& pin = pins_[pin_id];
          ImNodes::BeginInputAttribute(pin.id);
          ImGui::Text("-> %s", pin.name.c_str());
          ImNodes::EndInputAttribute();
        }

        // Output pins
        for (int pin_id : node.output_pins) {
          const Pin& pin = pins_[pin_id];
          ImNodes::BeginOutputAttribute(pin.id);
          ImGui::Indent(40.0f);
          ImGui::Text("%s ->", pin.name.c_str());
          ImNodes::EndOutputAttribute();
        }

        ImNodes::EndNode();
      }

      // Render all links
      for (const auto& link : links_) {
        ImNodes::Link(link.id, link.start_pin, link.end_pin);
      }

      ImNodes::MiniMap();
      ImNodes::EndNodeEditor();

      // Handle link creation
      int start_pin, end_pin;
      if (ImNodes::IsLinkCreated(&start_pin, &end_pin)) {
        if (can_create_link(start_pin, end_pin)) {
          links_.push_back({next_id_++, start_pin, end_pin});
        }
      }

      // Handle link deletion
      int link_id;
      if (ImNodes::IsLinkDestroyed(&link_id)) {
        auto it = std::find_if(links_.begin(), links_.end(),
                               [link_id](const Link& link) { return link.id == link_id; });
        if (it != links_.end()) { links_.erase(it); }
      }

      // Context menu for adding nodes
      // bool open_popup = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      //                         ImNodes::IsEditorHovered() &&
      //                         ImGui::IsMouseClicked(ImGuiMouseButton_Right);

      bool open_popup = ImGui::IsMouseClicked(ImGuiMouseButton_Right); //  && ImNodes::IsEditorHovered();

      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
      if (/*!ImGui::IsAnyItemHovered() &&*/ open_popup) { ImGui::OpenPopup("context_menu"); }

      if (ImGui::BeginPopup("context_menu")) {
        const ImVec2 click_pos = ImGui::GetMousePosOnOpeningCurrentPopup();

        if (ImGui::MenuItem("Add Node")) { add_simple_node("Node", click_pos); }

        // Add more node types here
        if (ImGui::BeginMenu("Add Specific Node")) {
          if (ImGui::MenuItem("Math Node")) { add_math_node(click_pos); }
          if (ImGui::MenuItem("Value Node")) { add_value_node(click_pos); }
          ImGui::EndMenu();
        }

        ImGui::EndPopup();
      }
      ImGui::PopStyleVar();

      // Handle node/link selection and deletion
      handle_deletions();
    }

    // Add a simple node with one input and one output
    int add_simple_node(const std::string& name, const ImVec2& position = ImVec2(0, 0)) {
      Node node;
      node.id = next_id_++;
      node.name = name;
      node.position = position;

      // Add pins
      int input_pin = next_id_++;
      int output_pin = next_id_++;

      pins_[input_pin] = {input_pin, node.id, PinKind::Input, "In"};
      pins_[output_pin] = {output_pin, node.id, PinKind::Output, "Out"};

      node.input_pins.push_back(input_pin);
      node.output_pins.push_back(output_pin);

      nodes_.push_back(node);
      ImNodes::SetNodeScreenSpacePos(node.id, position);

      return node.id;
    }

    // Example: math node with two inputs, one output
    int add_math_node(const ImVec2& position = ImVec2(0, 0)) {
      Node node;
      node.id = next_id_++;
      node.name = "Math";
      node.position = position;

      int in1 = next_id_++;
      int in2 = next_id_++;
      int out = next_id_++;

      pins_[in1] = {in1, node.id, PinKind::Input, "A"};
      pins_[in2] = {in2, node.id, PinKind::Input, "B"};
      pins_[out] = {out, node.id, PinKind::Output, "Result"};

      node.input_pins = {in1, in2};
      node.output_pins = {out};

      nodes_.push_back(node);
      ImNodes::SetNodeScreenSpacePos(node.id, position);

      return node.id;
    }

    int add_value_node(const ImVec2& position = ImVec2(0, 0)) {
      Node node;
      node.id = next_id_++;
      node.name = "Value";
      node.position = position;

      int out = next_id_++;
      pins_[out] = {out, node.id, PinKind::Output, "Value"};
      node.output_pins = {out};

      nodes_.push_back(node);
      ImNodes::SetNodeScreenSpacePos(node.id, position);

      return node.id;
    }

    const std::vector<Node>& nodes() const { return nodes_; }
    const std::vector<Link>& links() const { return links_; }
    const std::unordered_map<int, Pin>& pins() const { return pins_; }

   private:
    std::vector<Node> nodes_;
    std::vector<Link> links_;
    std::unordered_map<int, Pin> pins_;
    int next_id_ = 1;

    bool can_create_link(int start_pin, int end_pin) {
      // Ensure we're connecting output -> input
      const auto start_it = pins_.find(start_pin);
      const auto end_it = pins_.find(end_pin);

      if (start_it == pins_.end() || end_it == pins_.end()) return false;

      const Pin& start = start_it->second;
      const Pin& end = end_it->second;

      // Can't connect node to itself
      if (start.node_id == end.node_id) return false;

      // Ensure correct direction: output -> input
      if (start.kind == PinKind::Output && end.kind == PinKind::Input) return true;
      if (start.kind == PinKind::Input && end.kind == PinKind::Output)
        return true;  // imnodes will swap them

      return false;
    }

    void handle_deletions() {
      // Delete selected nodes
      const int num_selected_nodes = ImNodes::NumSelectedNodes();
      if (num_selected_nodes > 0 && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        std::vector<int> selected_nodes(num_selected_nodes);
        ImNodes::GetSelectedNodes(selected_nodes.data());

        for (int node_id : selected_nodes) {
          // Remove links connected to this node's pins
          auto node_it = std::find_if(nodes_.begin(), nodes_.end(),
                                      [node_id](const Node& n) { return n.id == node_id; });

          if (node_it != nodes_.end()) {
            // Collect all pins of this node
            std::vector<int> node_pins;
            node_pins.insert(node_pins.end(), node_it->input_pins.begin(),
                             node_it->input_pins.end());
            node_pins.insert(node_pins.end(), node_it->output_pins.begin(),
                             node_it->output_pins.end());

            // Remove links connected to any of these pins
            links_.erase(std::remove_if(links_.begin(), links_.end(),
                                        [&node_pins](const Link& link) {
                                          return std::find(node_pins.begin(), node_pins.end(),
                                                           link.start_pin) != node_pins.end() ||
                                                 std::find(node_pins.begin(), node_pins.end(),
                                                           link.end_pin) != node_pins.end();
                                        }),
                         links_.end());

            // Remove pins from map
            for (int pin_id : node_pins) {
              pins_.erase(pin_id);
            }

            // Remove node
            nodes_.erase(node_it);
          }
        }

        ImNodes::ClearNodeSelection();
      }

      // Delete selected links
      const int num_selected_links = ImNodes::NumSelectedLinks();
      if (num_selected_links > 0 && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        std::vector<int> selected_links(num_selected_links);
        ImNodes::GetSelectedLinks(selected_links.data());

        for (int link_id : selected_links) {
          auto it = std::find_if(links_.begin(), links_.end(),
                                 [link_id](const Link& link) { return link.id == link_id; });
          if (it != links_.end()) { links_.erase(it); }
        }

        ImNodes::ClearLinkSelection();
      }
    }
  };

}  // namespace ren