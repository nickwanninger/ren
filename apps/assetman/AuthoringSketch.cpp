#include "AuthoringSketch.h"
#include <ren/edit/EditContext.h>
#include <ren/edit/ImGuiEdit.h>
#include <ren/obj/Types.h>

#include <ren/types.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <ren/core/ui/EditorUI.h>
#include <algorithm>
#include <any>
#include <charconv>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <flecs/flecs.h>


// This sketch keeps the authoring model local to assetman. Type descriptors define
// the schema; Value objects own editable data. The inspector and JSON reader use
// the same value tree. Baking constructs a separate C++ object from that tree.
// Define a type first, read its initial values, then edit, serialize, or bake them.
namespace authoring_sketch {
  namespace {

    using namespace ren::obj;
    using namespace ren::edit;



    //=========================
    // Sketch value types
    //=========================

    // Keeps texture identity as an atomic authoring value. Reading or baking this
    // reference copies the ID; it does not resolve an asset or load a resource.
    struct TextureReference {
      std::string id = "null";
    };


    // Keeps scene identity separate from texture identity, despite the shared ID
    // format. The sketch does not resolve the ID to an ECS entity.
    struct EntityReference {
      std::string id = "null";
    };


    // Shares the reference format without merging their distinct descriptor types.
    // The editor permits incomplete text; JSON loading and baking validate its ID.
    template <typename T>
    struct ReferenceType : PrimitiveType<T> {
      explicit ReferenceType(const char* name)
          : PrimitiveType<T>(name) {}


      json encodeValue(const T& value) const final { return value.id; }


      T decodeValue(const json& document) const final {
        std::string id = document.get<std::string>();
        if (id != "null" && (id.size() != 16 || id.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)) {
          throw std::runtime_error("Expected null or a 16-digit hexadecimal ID");
        }
        return T{std::move(id)};
      }


      bool inspectPrimitive(ren::eui::Ui& ui, const char* label, T& value) const final { return ui.inspect(label, value.id); }
    };


    struct Color {
      float r, g, b, a;
      ren_auto_describe(Color, r, g, b, a);
    };

    struct ColorEditor final : Editor {
      void draw(ren::eui::Ui& ui, EditContext& context) const override {
        auto candidate = context.read();
        float rgba[] = {
            candidate.at("r").get<float>(),
            candidate.at("g").get<float>(),
            candidate.at("b").get<float>(),
            candidate.at("a").get<float>(),
        };

        // ColorEdit4 draws several items and a picker popup; edit() treats them as one control.
        context.edit([&] { return ui.imgui([&] { return ImGui::ColorEdit4("##value", rgba, ImGuiColorEditFlags_Float); }); },
                     [&] {
                       candidate["r"] = rgba[0];
                       candidate["g"] = rgba[1];
                       candidate["b"] = rgba[2];
                       candidate["a"] = rgba[3];
                       return candidate;
                     });
      }
    };

  }  // namespace
}  // namespace authoring_sketch


// Specializations must be declared in the namespace of the primary template.
namespace ren::obj {

  // Selects the shared reference behavior with a distinct texture descriptor.
  template <>
  struct TypeDefinition<authoring_sketch::TextureReference> final : authoring_sketch::ReferenceType<authoring_sketch::TextureReference> {
    TypeDefinition()
        : ReferenceType("TextureReference") {}
  };


  // Selects the shared reference behavior with a distinct entity descriptor.
  template <>
  struct TypeDefinition<authoring_sketch::EntityReference> final : authoring_sketch::ReferenceType<authoring_sketch::EntityReference> {
    TypeDefinition()
        : ReferenceType("EntityReference") {}
  };


  template <>
  struct TypeDefinition<authoring_sketch::Color> final : StructType<authoring_sketch::Color> {
    TypeDefinition()
        : StructType<authoring_sketch::Color>("Color") {
      TypeBuilder<authoring_sketch::Color> builder{*this};
      authoring_sketch::Color::describe(builder);
    }


    const ren::edit::Editor* getEditor() const override {
      static const authoring_sketch::ColorEditor editor;
      return &editor;
    }
  };

}  // namespace ren::obj


namespace authoring_sketch {
  namespace {

    using namespace ren::obj;
    using namespace ren::edit;




    // Demonstrates nested fields with different defaults for the same vector type.
    // No custom editor is present, so the inspector draws its fields recursively.
    struct Transform {
      glm::vec3 translation{0.0f};
      glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
      glm::vec3 scale{1.0f};


      ren_auto_describe(Transform, translation, rotation, scale);
    };


    // Provides the native target and initial defaults for the prototype. Only
    // declared members enter the authoring tree; registration with ECS is separate.
    struct ExampleComponent {
      glm::vec2 A;
      glm::ivec2 B;

      glm::vec3 C;
      glm::ivec3 D;

      glm::vec4 E;
      glm::uvec4 F;

      std::string yourFriendsName4;
      Color favoriteColor;

      Transform TheTranslation;
      // std::vector<float> weights{0.25f, 0.75f};
      // std::vector<Transform> transforms{Transform{}};

      ren_auto_describe(ExampleComponent, A, B, C, D, E, F, yourFriendsName4, favoriteColor, TheTranslation);
    };




    // Adapts the prototype's single component to the edit contract. Every write
    // resolves from the root, so replacing a subtree cannot invalidate history.
    // Batches validate on a copy before replacing the live authoring tree.
    struct SketchDocument final : PropertyDocument {
      Value& root;

      explicit SketchDocument(Value& root)
          : root(root) {}


      json read(const EditAddress& address) const override { return resolve(root, address).serialize(); }


      void apply(const std::vector<PropertyWrite>& writes) override {
        if (writes.empty()) {
          return;
        }

        auto candidate = root;
        for (const auto& write : writes) {
          auto& target = resolve(candidate, write.address);
          target = target.deserialize(write.value, write.address.display());
        }
        root = std::move(candidate);
      }


      // The empty address is the whole component. Each segment names a field or
      // a sequence index below it.
      Value& resolve(Value& value, const EditAddress& address) const {
        Value* current = &value;
        for (const auto& segment : address.parts()) {
          try {
            current = &current->findChild(segment);
          } catch (const std::exception&) {
            throw std::runtime_error("Unknown field path: " + address.display());
          }
        }
        return *current;
      }
    };




    //=========================
    // Inspector and tab state
    //=========================

    // All rows share the same columns. Tree indentation affects labels only;
    // nested fields retain the same control width and horizontal position.
    // Addresses survive document replacement during editor callbacks.
    void inspectValueRows(ren::eui::Ui& ui, const char* label, const Type& type, PropertyEditor& properties, EditorStateStore& states,
                          const EditAddress& address, bool customEditors, bool* removeElement = nullptr) {
      ImGui::PushID(address.key().c_str());
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const auto* editor = type.getEditor();
      const bool sequence = type.kind == TypeKind::Sequence;
      const bool control = !sequence && editor && (customEditors || type.kind == TypeKind::Primitive);
      bool expanded = false;
      if (control) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
      } else {
        expanded = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_SpanAvailWidth);
      }

      ImGui::TableSetColumnIndex(1);
      bool add = false;
      size_t count = 0;
      if (control) {
        ImGui::SetNextItemWidth(-1.0f);
        EditContext context(properties, states, address, ImGui::GetID("##value"));
        editor->draw(ui, context);
        if (!context.getError().empty()) {
          ImGui::TextWrapped("%s", context.getError().c_str());
        }
      } else if (sequence) {
        count = properties.read(address).size();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Size: %zu", count);
        ImGui::SameLine();
        add = ImGui::Button("+", ImVec2(ImGui::GetFrameHeight(), 0.0f));
        ImGui::SetItemTooltip("Add element");
      }

      ImGui::TableSetColumnIndex(2);
      if (removeElement) {
        *removeElement = ImGui::Button("-", ImVec2(ImGui::GetFrameHeight(), 0.0f));
        ImGui::SetItemTooltip("Remove %s", label);
      }

      if (sequence) {
        const auto& sequenceType = static_cast<const SequenceType&>(type);
        std::optional<size_t> remove;
        if (expanded) {
          for (size_t i = 0; i < count; ++i) {
            const auto index = std::to_string(i);
            const auto elementLabel = "Element " + index;
            bool removeThis = false;
            inspectValueRows(ui, elementLabel.c_str(), sequenceType.elementType, properties, states, address / index, customEditors, &removeThis);
            if (removeThis) {
              remove = i;
            }
          }
        }

        // Finish element controls before changing indices. The parent session
        // commits any pending element edit before it replaces the full array.
        EditContext context(properties, states, address, ImGui::GetID("##sequence"));
        if (add || remove) {
          auto candidate = properties.read(address);
          if (remove) {
            candidate.erase(*remove);
          } else {
            candidate.push_back(sequenceType.makeElement().serialize());
          }
          context.submit(candidate, {.changed = true, .finished = true});
        }
        if (!context.getError().empty()) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(1);
          ImGui::TextWrapped("%s", context.getError().c_str());
        }
      } else if (expanded) {
        for (const auto& field : type.fields) {
          inspectValueRows(ui, field->friendlyName.c_str(), *field->type, properties, states, address / field->name, customEditors);
        }
      }

      if (expanded) {
        ImGui::TableSetColumnIndex(0);
        ImGui::TreePop();
      }
      ImGui::PopID();
    }


    // A single table keeps controls aligned across foldouts. Compact padding
    // preserves the normal control height while reducing gaps between rows.
    void inspectValue(ren::eui::Ui& ui, const char* label, const Type& type, PropertyEditor& properties, EditorStateStore& states,
                      const EditAddress& address, bool customEditors) {
      ImGui::PushID(address.key().c_str());
      ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 2.0f));
      ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, ImGui::GetFontSize());
      const auto flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_NoBordersInBody;
      if (ImGui::BeginTable("##inspector", 3, flags)) {
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentEnable, 0.38f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_IndentDisable, 0.62f);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_IndentDisable, ImGui::GetFrameHeight());
        inspectValueRows(ui, label, type, properties, states, address, customEditors);
        ImGui::EndTable();
      }
      ImGui::PopStyleVar(4);
      ImGui::PopID();
    }


    // Displays descriptor paths without consulting values. A custom structure
    // editor therefore cannot hide its primitive children from the schema view.
    void drawSchema(const Type& type, const std::string& path) {
      const char* kind = type.kind == TypeKind::Primitive ? "primitive" : type.kind == TypeKind::Sequence ? "sequence" : "structure";
      ImGui::Text("%s: %s (%s)", path.c_str(), type.name.c_str(), kind);
      if (type.kind == TypeKind::Sequence) {
        drawSchema(static_cast<const SequenceType&>(type).elementType, path + "[]");
      }
      for (const auto& field : type.fields) {
        drawSchema(*field->type, path + "." + field->name);
      }
    }


    //=========================
    // Native float
    //=========================

    // A second document with its own history. Its data is plain C++ values,
    // with no obj::Value, PropertyDocument or JSON.
    struct NativeFloat {
      float value = 1.0f;
      glm::vec3 position{0.0f};
      float opacity = 1.0f;
      glm::vec3 tint{1.0f};
      bool visible = true;
      EditHistory history;
    };


    // Draws the whole edit loop for one value: read, draw, report flags, apply.
    void drawNativeFloat(ren::eui::Ui& ui, NativeFloat& native) {
      ImGui::TextWrapped("Drives EditHistory directly. Drag a value or Ctrl+click and type, and press Escape during an edit to cancel it.");

      ImGui::BeginDisabled(!native.history.canUndo());
      if (ImGui::Button("Undo")) {
        native.history.undo();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::BeginDisabled(!native.history.canRedo());
      if (ImGui::Button("Redo")) {
        native.history.redo();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();

      // A one-step change: execute() runs redo() now and records the command.
      if (ImGui::Button("Reset to 1")) {
        const float before = native.value;
        native.history.execute(
            ren::makeBox<LambdaAction>("Reset value", [&native] { native.value = 1.0f; }, [&native, before] { native.value = before; }));
      }

      // editValue() passes a copy to the lambda and reads the ImGui state after
      // it. An edit over many frames becomes one entry; Escape restores the value.
      ui.valueTable("Native values", [&](ren::eui::Ui& ui) {
        editValue(ui, native.history, "Value", native.value, [](ren::eui::Ui& ui, float& value) { return ui.drag("Value", value, {.step = 0.01f}); });

        // Without a lambda, editValue() draws the default Ui::inspect widget. For a
        // vector, that is one drag field per axis, and the axes act as one control:
        // an edit on any axis records the whole vector.
        editValue(ui, native.history, "Position", native.position);

        // Sliders keep values between their bounds; a vector uses the same bounds per component.
        editValue(ui, native.history, "Opacity", native.opacity,
                  [](ren::eui::Ui& ui, float& value) { return ui.slide("Opacity", value, {.min = 0.0f, .max = 1.0f}); });
        editValue(ui, native.history, "Visible", native.visible);
        editValue(ui, native.history, "Tint", native.tint,
                  [](ren::eui::Ui& ui, glm::vec3& value) { return ui.slide("Tint", value, {.min = 0.0f, .max = 1.0f}); });
      });

      ImGui::Separator();
      ImGui::Text("%zu applied / %zu recorded%s", native.history.getCursor(), native.history.getEntries().size(),
                  native.history.isOpen() ? " (editing)" : "");
      const auto& entries = native.history.getEntries();
      for (size_t i = 0; i < entries.size(); ++i) {
        ImGui::BulletText("%s%s", i < native.history.getCursor() ? "" : "Redo: ", entries[i]->getLabel().c_str());
      }
    }



    // Retains one authoring session across frames and inner tab changes. Construct
    // defaults before value and document, which both copy from the initial tree.
    // JSON text and the baked snapshot change only through their explicit actions.
    struct Sketch {
      Value defaults = readNative(ExampleComponent{});
      Value value = defaults;

      SketchDocument model{value};
      EditHistory history;
      PropertyEditor properties{model, history};
      EditorStateStore editorStates;

      std::string document = value.serialize().dump(2);
      std::optional<ExampleComponent> baked;

      std::string error;
      std::string status;
      bool customEditors = true;

      // Records an edit since the last bake; it does not compare snapshot values.
      bool stale = false;
    };

  }  // namespace




  //=========================
  // Tab drawing
  //=========================

  // Uses one persistent session. Inspector edits affect its tree immediately;
  // JSON loading and baking replace their results only after validation succeeds.
  void draw(ren::eui::Ui& ui) {
    static Sketch sketch;
    static NativeFloat native;
    const auto previousRevision = sketch.history.getRevision();

    ImGui::TextWrapped("Edit nested values, load JSON, and bake an in-memory component.");
    ImGui::Checkbox("Custom structure editors", &sketch.customEditors);
    ImGui::BeginDisabled(!sketch.history.canUndo());
    if (ImGui::Button("Undo")) {
      sketch.history.undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!sketch.history.canRedo());
    if (ImGui::Button("Redo")) {
      sketch.history.redo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset values")) {
      sketch.properties.execute("Reset component", {{EditAddress{}, sketch.defaults.serialize()}});
      sketch.error.clear();
      sketch.status = "Values reset. Serialize to update the JSON text.";
    }

    ui.tabBar("Authoring views", [&](ren::eui::Ui& ui) {
      ui.tab("Inspector", [&](ren::eui::Ui& ui) {
        // Reserve footer space so validation errors remain visible below the editor.
        ui.child(
            "Editable component",
            [&](ren::eui::Ui& ui) {
              inspectValue(ui, "ExampleComponent", *sketch.value.type, sketch.properties, sketch.editorStates, EditAddress{}, sketch.customEditors);
            },
            ImVec2(0, -60));
      });
      ui.tab("JSON", [&](ren::eui::Ui& ui) {
        if (ImGui::Button("Serialize")) {
          sketch.document = sketch.value.serialize().dump(2);
          sketch.error.clear();
          sketch.status = "Serialized the current values.";
        }
        ImGui::SameLine();
        if (ImGui::Button("Deserialize")) {
          try {
            // Use defaults, rather than the current edits, as the load baseline.
            auto loaded = sketch.defaults.deserialize(json::parse(sketch.document), "ExampleComponent");
            sketch.properties.execute("Load JSON", {{EditAddress{}, loaded.serialize()}});
            sketch.error.clear();
            sketch.status = "Loaded JSON. Missing fields use component defaults.";
          } catch (const std::exception& error) {
            sketch.error = error.what();
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy JSON")) {
          ImGui::SetClipboardText(sketch.document.c_str());
        }
        ImGui::TextWrapped("JSON changes take effect when you select Deserialize.");
        ImGui::InputTextMultiline("##Document", &sketch.document, ImVec2(-1, 230));
      });
      ui.tab("Baked component", [&](ren::eui::Ui& ui) {
        if (ImGui::Button("Bake")) {
          try {
            // Editors permit incomplete reference text. Use the JSON decoder to
            // validate all leaves before native assignment, without replacing edits.
            auto validated = sketch.defaults.deserialize(sketch.value.serialize(), "ExampleComponent");
            sketch.baked = validated.bake<ExampleComponent>();
            sketch.stale = false;
            sketch.error.clear();
            sketch.status = "Constructed an ExampleComponent in memory.";
          } catch (const std::exception& error) {
            sketch.error = error.what();
          }
        }
        if (sketch.baked) {
          ImGui::TextUnformatted(sketch.stale ? "Snapshot differs from current edits." : "Snapshot matches current values.");

          // Read the stored C++ object so this view reflects the bake result.
          // Drawing a disabled copy also keeps the snapshot independent of edits.
          auto snapshot = readNative(*sketch.baked);
          SketchDocument model(snapshot);
          EditHistory history;
          PropertyEditor properties(model, history);
          EditorStateStore states;
          ui.child(
              "Native component",
              [&](ren::eui::Ui& ui) {
                ImGui::BeginDisabled();
                inspectValue(ui, "Native ExampleComponent", *snapshot.type, properties, states, EditAddress{}, sketch.customEditors);
                ImGui::EndDisabled();
              },
              ImVec2(0, -60));
        }
      });
      ui.tab("Schema", [&](ren::eui::Ui& ui) {
        ImGui::BeginChild("Schema fields", ImVec2(0, -60));
        drawSchema(getType<ExampleComponent>(), "ExampleComponent");
        ImGui::EndChild();
      });
      ui.tab("Native float", [&](ren::eui::Ui& ui) { drawNativeFloat(ui, native); });
      ui.tab("History", [&](ren::eui::Ui& ui) {
        ImGui::Text("%zu applied / %zu recorded%s", sketch.history.getCursor(), sketch.history.getEntries().size(),
                    sketch.history.isOpen() ? " (editing)" : "");
        ImGui::BeginChild("Edit history", ImVec2(0, -60));
        const auto& entries = sketch.history.getEntries();
        for (size_t i = 0; i < entries.size(); ++i) {
          ImGui::PushID(static_cast<int>(i));
          if (ImGui::TreeNode("Edit", "%s%s", i < sketch.history.getCursor() ? "" : "Redo: ", entries[i]->getLabel().c_str())) {
            // Only property edits have field snapshots to show.
            if (const auto* diff = dynamic_cast<const JsonDiffAction*>(entries[i].get())) {
              for (const auto& change : diff->getChanges()) {
                ImGui::TextWrapped("%s: %s -> %s", change.address.display().c_str(), change.before.dump().c_str(), change.after.dump().c_str());
              }
            }
            ImGui::TreePop();
          }
          ImGui::PopID();
        }
        ImGui::EndChild();
      });
    });

    // Switching tabs or hiding a custom editor finishes its active interaction.
    sketch.history.endFrame();
    native.history.endFrame();
    if (sketch.history.getRevision() != previousRevision) {
      sketch.stale = true;
    }

    if (!sketch.error.empty()) {
      ImGui::TextWrapped("%s", sketch.error.c_str());
    } else if (!sketch.status.empty()) {
      ImGui::TextWrapped("%s", sketch.status.c_str());
    }
  }

}  // namespace authoring_sketch
