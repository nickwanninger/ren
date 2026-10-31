#include <ren/core/Components.h>
#include <ren/core/ComponentRegistration.h>
#include <ren/edit/SceneEditor.h>
#include <ren/obj/Types.h>
#include <functional>
#include <iostream>
#include <string_view>

using namespace ren;
namespace {
  void verify(bool value, const char *message) {
    if (!value) {
      throw std::runtime_error(message);
    }
  }


  void throws(const std::function<void()> &action, const char *message) {
    try {
      action();
    } catch (const std::runtime_error &error) {
      verify(std::string(error.what()).find(message) != std::string::npos, error.what());
      return;
    }
    throw std::runtime_error("Expected an exception");
  }


  const ComponentRegistrationInfo &findRegistration(std::string_view name) {
    for (const auto &info : getRegisteredComponents()) {
      if (info.type != nullptr && info.type->name == name) {
        return info;
      }
    }
    throw std::runtime_error("Component is not registered with a schema: " + std::string(name));
  }



  //=========================
  // Component schemas
  //=========================

  void testRegistrationUsesObjTypes() {
    verify(findRegistration("Name").type == &obj::getType<comp::Name>(), "Name registration has another type");
    verify(findRegistration("Transform").type == &obj::getType<comp::Transform>(), "Transform registration has another type");

    // Components without describe() still register for flecs.
    for (const auto &info : getRegisteredComponents()) {
      if (std::string_view(info.typeName).find("PointLightComponent") != std::string_view::npos) {
        verify(info.type == nullptr, "PointLightComponent has no describe() but has a schema");
        return;
      }
    }
    throw std::runtime_error("PointLightComponent is not registered");
  }


  void testTransformRoundTrip() {
    const auto &type = obj::getType<comp::Transform>();
    verify(type.fields.size() == 3, "Transform must expose translation, rotation and scale only");

    comp::Transform source;
    source.translation = {1.0f, 2.0f, 3.0f};
    source.scale = {4.0f, 5.0f, 6.0f};

    // A default value supplies the field defaults, as a scene load does.
    auto value = obj::readNative(comp::Transform{}).deserialize(obj::readNative(source).serialize(), "Transform");
    auto baked = value.bake<comp::Transform>();
    verify(baked.translation == source.translation, "translation does not round trip");
    verify(baked.rotation == source.rotation, "rotation does not round trip");
    verify(baked.scale == source.scale, "scale does not round trip");
  }



  //=========================
  // Edit history
  //=========================

  // Stores plain JSON. Each address field is an object key below the root.
  // A write of the string "invalid" fails, so tests can check atomic batches.
  struct JsonDocument final : edit::PropertyDocument {
    nlohmann::json data = {{"a", 1}, {"b", 2}, {"v", {{"x", 0}, {"y", 0}}}};


    nlohmann::json read(const edit::FieldAddress &address) const override {
      const auto *node = &data;
      for (const auto &field : address.fields) {
        node = &node->at(field);
      }
      return *node;
    }


    void apply(const std::vector<edit::PropertyWrite> &writes) override {
      auto candidate = data;
      for (const auto &write : writes) {
        if (write.value == "invalid") {
          throw std::runtime_error("Invalid value at " + write.address.display());
        }
        auto *node = &candidate;
        for (const auto &field : write.address.fields) {
          node = &node->at(field);
        }
        *node = write.value;
      }
      data = std::move(candidate);
    }
  };


  edit::FieldAddress at(std::vector<std::string> fields) { return edit::FieldAddress{.fields = std::move(fields)}; }


  void testSessionMakesOneEntry() {
    JsonDocument document;
    edit::EditHistory history(document);

    // A drag updates the value on each frame, and commit records only the first and last value.
    for (int value : {3, 5, 8}) {
      history.update(1, "Drag a", {{at({"a"}), value}});
    }
    verify(history.getEntries().empty(), "An active session must not make an entry");
    history.commit();

    verify(history.getEntries().size() == 1, "A session must make one entry");
    const auto &change = history.getEntries()[0].changes.at(0);
    verify(change.before == 1 && change.after == 8, "The entry must keep the original and final values");

    verify(history.undo() && document.data["a"] == 1, "Undo must restore the original value");
    verify(history.redo() && document.data["a"] == 8, "Redo must restore the final value");
    verify(!history.redo(), "Redo past the end must return false");
  }


  void testUnchangedSessionKeepsRedo() {
    JsonDocument document;
    edit::EditHistory history(document);
    history.execute("Set a", {{at({"a"}), 5}});
    history.undo();

    // A session that returns to its start value is not an effective edit.
    history.update(1, "Drag b", {{at({"b"}), 9}});
    history.update(1, "Drag b", {{at({"b"}), 2}});
    history.commit();

    verify(history.getEntries().size() == 1 && history.canRedo(), "An unchanged session must keep the redo branch");
  }


  void testCancelRestoresValues() {
    JsonDocument document;
    edit::EditHistory history(document);
    history.update(1, "Drag a", {{at({"a"}), 7}});
    history.cancel();

    verify(document.data["a"] == 1, "Cancel must restore the original value");
    verify(history.getEntries().empty() && !history.isActive(), "Cancel must not make an entry");
  }


  void testUndoCancelsActiveSession() {
    JsonDocument document;
    edit::EditHistory history(document);
    history.execute("Set a", {{at({"a"}), 5}});
    history.update(1, "Drag a", {{at({"a"}), 9}});

    // The first undo only cancels the preview. The second undo goes into history.
    verify(history.undo() && document.data["a"] == 5 && history.getCursor() == 1, "Undo must cancel the session first");
    verify(history.undo() && document.data["a"] == 1 && history.getCursor() == 0, "The next undo must restore the entry");
  }


  void testNewEditDiscardsRedo() {
    JsonDocument document;
    edit::EditHistory history(document);
    history.execute("Set a", {{at({"a"}), 5}});
    history.execute("Set b", {{at({"b"}), 6}});
    history.undo();
    history.execute("Set a", {{at({"a"}), 7}});

    verify(history.getEntries().size() == 2 && !history.canRedo(), "An effective edit must discard the redo branch");
    verify(history.getEntries()[1].label == "Set a", "The new entry must replace the undone entry");
  }


  void testOverlappingWritesThrow() {
    JsonDocument document;
    edit::EditHistory history(document);
    history.update(1, "Drag v.x", {{at({"v", "x"}), 1}});

    throws([&] { history.update(1, "Set v", {{at({"v"}), {{"x", 2}, {"y", 2}}}}); }, "Overlapping edit paths");
    verify(document.data["v"]["x"] == 1, "A rejected batch must not change the document");
  }


  void testFailedBatchLeavesNoRecord() {
    JsonDocument document;
    edit::EditHistory history(document);

    throws([&] { history.update(1, "Set a and b", {{at({"a"}), 4}, {at({"b"}), "invalid"}}); }, "Invalid value");
    verify(document.data["a"] == 1, "A failed batch must not change the document");
    history.commit();
    verify(history.getEntries().empty(), "A failed batch must not make an entry");
  }


  void testOwnerChangeCommits() {
    JsonDocument document;
    edit::EditHistory history(document);
    history.update(1, "Drag a", {{at({"a"}), 4}});
    history.update(2, "Drag b", {{at({"b"}), 5}});

    verify(history.getEntries().size() == 1 && history.isOwner(2), "A new owner must commit the previous session");
  }


  void testHiddenOwnerCommitsAtEndOfFrame() {
    JsonDocument document;
    edit::EditHistory history(document);
    history.update(1, "Drag a", {{at({"a"}), 4}});

    history.beginFrame();
    history.touch(1);
    history.endFrame();
    verify(history.isActive(), "A visible owner must keep its session");

    history.beginFrame();
    history.endFrame();
    verify(!history.isActive() && history.getEntries().size() == 1, "A hidden owner must commit at the end of the frame");
  }
}  // namespace


int main() {
  testRegistrationUsesObjTypes();
  testTransformRoundTrip();

  testSessionMakesOneEntry();
  testUnchangedSessionKeepsRedo();
  testCancelRestoresValues();
  testUndoCancelsActiveSession();
  testNewEditDiscardsRedo();
  testOverlappingWritesThrow();
  testFailedBatchLeavesNoRecord();
  testOwnerChangeCommits();
  testHiddenOwnerCommitsAtEndOfFrame();
  std::cout << "Edit model tests passed\n";
  return 0;
}
