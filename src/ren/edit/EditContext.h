#pragma once

#include <ren/edit/ImGuiEdit.h>
#include <ren/edit/PropertyEditor.h>
#include <any>
#include <map>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeindex>

namespace ren::edit {

  // Owns optional state for each field, widget, and state type. Drafts and preview
  // handles stay here, outside descriptors and history. The inspector owns its
  // lifetime; cached resources release when it clears or destroys this store.
  class EditorStateStore {
   public:
    // Returns persistent state for this editor instance. State must be copyable
    // and default constructible because std::any owns it.
    template <typename T>
    T& get(const EditAddress& address, std::uint64_t widget) {
      auto& value = values[{address.key(), widget, std::type_index(typeid(T))}];
      if (!value.has_value()) {
        value = T{};
      }
      return std::any_cast<T&>(value);
    }

    // Releases drafts and caches when an inspector closes or changes documents.
    void clear();

   private:
    std::map<std::tuple<std::string, std::uint64_t, std::type_index>, std::any> values;
  };


  // Gives one editor read-only snapshots, optional state, and a controlled write
  // path. Invalid candidates stay outside the document and report a local error.
  class EditContext {
   public:
    // Binds a field and widget within an inspector. Dependencies must outlive drawing.
    EditContext(PropertyEditor& properties, EditorStateStore& states, EditAddress address, std::uint64_t widget);

    // Returns the current authored value as an owned snapshot.
    nlohmann::json read() const;

    // Submits a candidate and its lifecycle. Commit ends the undo step; cancel
    // restores it. Returns true if document data changes, including cancellation.
    bool submit(const nlohmann::json& candidate, EditInteraction interaction);

    // Submits several child fields as one session update. Writes must stay inside
    // this context's subtree; validation and history treat the batch atomically.
    bool submitBatch(const std::vector<PropertyWrite>& candidates, EditInteraction interaction);

    // Draws one control with `draw`, which returns true when it changed its
    // value, then submits `candidate()` with the control's ImGui state. Several
    // items drawn by `draw` act as one control. `candidate` returns either one
    // JSON value for this address or a batch of writes inside it. Returns the
    // interaction, so the caller can keep a draft while the control is active.
    template <typename Draw, typename Candidate>
    EditInteraction edit(Draw&& draw, Candidate&& candidate) {
      ImGui::BeginGroup();
      const bool changed = draw();
      ImGui::EndGroup();

      const auto interaction = interactionFromLastItem(changed);
      auto result = candidate();
      if constexpr (std::is_same_v<decltype(result), std::vector<PropertyWrite>>) {
        submitBatch(result, interaction);
      } else {
        submit(result, interaction);
      }
      return interaction;
    }

    // Binds a nested field to another control within a custom editor.
    EditContext child(std::string field, std::uint64_t childWidget) const;

    // Returns transient state for this field and widget, independent of history.
    template <typename T>
    T& state() const {
      return states.get<T>(address, widget);
    }

    // Returns the document revision so custom editors can invalidate cached drafts.
    std::uint64_t getRevision() const;

    // Returns the bound address for custom editors that submit a child batch.
    const EditAddress& getAddress() const;

    // Returns the last candidate error for this editor, or an empty string.
    const std::string& getError() const;

   private:
    struct Feedback {
      std::string error;
    };

    PropertyEditor& properties;
    EditorStateStore& states;
    EditAddress address;
    std::uint64_t widget;
  };


  // Defines a shared, stateless editor. The parent draws its label and supplies an
  // ImGui ID scope and width. Implementations draw hidden-label controls, keep
  // optional state in the context, and submit candidates instead of mutating data.
  class Editor {
   public:
    // Supports destruction through the editor interface.
    virtual ~Editor() = default;

    // Draws the control contents and reports interaction events through the context.
    virtual void draw(EditContext& context) const = 0;
  };

}  // namespace ren::edit
