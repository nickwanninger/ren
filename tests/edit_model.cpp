#include <ren/core/Components.h>
#include <ren/core/ComponentRegistration.h>
#include <ren/edit/PropertyEditor.h>
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
  // Edit addresses
  //=========================

  // Rule: `/` adds exactly one segment. A dot or slash inside a field name does
  // not split it, so the key of ["a.b"] differs from the key of ["a", "b"].
  void testAddressSegmentsDoNotSplit() {
    const auto dotted = edit::EditAddress() / "a.b";
    const auto nested = edit::EditAddress() / "a" / "b";

    verify(dotted.parts().size() == 1 && nested.parts().size() == 2, "`/` must add one segment");
    verify(dotted.key() != nested.key(), "Keys must keep segment boundaries");
    verify(!(dotted == nested), "Addresses with different segments must differ");
    verify(nested.display() == "a/b", "display() must join segments with /");
  }


  // Rule: an address contains itself and every address below it, and nothing else.
  // Sessions use this to reject overlapping writes and writes outside an editor.
  void testAddressContains() {
    const auto v = edit::EditAddress() / "v";

    verify(v.contains(v), "An address must contain itself");
    verify(v.contains(v / "x"), "An address must contain its children");
    verify(!(v / "x").contains(v), "A child must not contain its parent");
    verify(!v.contains(edit::EditAddress() / "vx"), "A sibling with a longer name must not be contained");
    verify(edit::EditAddress().contains(v), "The root must contain every address");
  }



  //=========================
  // Edit history
  //=========================
  //
  // EditHistory is a stack of Command objects. It does not know what a command
  // changes. These tests use plain C++ data with no addresses and no JSON, to
  // show that a document needs no special interface to get undo and redo.

  struct NameList {
    std::vector<std::string> names;
  };


  // Rule: execute() runs the command once and records it. undo() and redo()
  // then call the command's own undo and redo functions.
  void testLambdaActionOnNativeData() {
    NameList list;
    edit::EditHistory history;

    // A LambdaAction holds a label, a redo function and an undo function. It
    // captures its target by reference; the list outlives the history.
    history.execute(makeBox<edit::LambdaAction>("Add Car", [&] { list.names.push_back("Car"); }, [&] { list.names.pop_back(); }));
    verify(list.names.size() == 1, "execute() must run the command");

    // List: ["Car"] → undo → [] → redo → ["Car"].
    verify(history.undo() && list.names.empty(), "Undo must run the undo function");
    verify(history.redo() && list.names.size() == 1, "Redo must run the redo function");
    verify(history.getEntries().at(0)->getLabel() == "Add Car", "The entry must keep its label");
  }


  // Rule: record() stores a command that the caller already applied. It must
  // not run the command again. Use it when the change happens elsewhere first.
  void testRecordDoesNotRun() {
    NameList list;
    edit::EditHistory history;

    // The caller changes the data itself, then records how to undo it.
    list.names.push_back("Car");
    history.record(makeBox<edit::LambdaAction>("Add Car", [&] { list.names.push_back("Car"); }, [&] { list.names.pop_back(); }));

    // A second push_back would give ["Car", "Car"].
    verify(list.names.size() == 1, "record() must not run the command");
    verify(history.undo() && list.names.empty(), "A recorded command must undo");
  }



  //=========================
  // Open commands
  //=========================
  //
  // An interactive edit, such as a slider or gizmo drag, changes the data on
  // each frame but must make only one undo step. The history holds such an edit
  // as an "open" command: it is applied live but not yet in the undo list.
  //
  //   first change     → open the command (it captures the start state)
  //   each change      → the caller applies the new value directly
  //   release          → commit: finish() captures the end state and records it
  //   Escape           → cancel: undo() restores the start state, nothing recorded
  //
  // track() runs these rules from one frame's EditInteraction flags.

  // One float that an interactive edit changes. The constructor runs when the
  // edit starts, so
  // it captures `before`. finish() runs at release and captures `after`.
  struct SetFloat final : edit::Command {
    float &target;
    float before;
    float after = 0.0f;

    explicit SetFloat(float &target)
        : target(target)
        , before(target) {}


    std::string getLabel() const override { return "Set float"; }
    void redo() override { target = after; }
    void undo() override { target = before; }


    // Returns false when the edit ends at its start value. The history then
    // drops the command instead of recording an undo step that does nothing.
    bool finish() override {
      after = target;
      return after != before;
    }
  };


  // Simulates one frame of an edit control with owner (widget) ID 1. The first
  // lambda makes the command; track() calls it only on the first change. The
  // second lambda applies this frame's value; track() calls it on each change.
  void editFrame(edit::EditHistory &history, float &value, edit::EditInteraction interaction, float candidate) {
    history.track(1, interaction, [&] { return makeBox<SetFloat>(value); }, [&] { value = candidate; });
  }


  // Rule: an edit over several frames makes one undo step from the start value
  // to the end value. The values between them are not recorded.
  void testOpenCommandCommits() {
    float value = 1.0f;
    edit::EditHistory history;

    // Frame 1 opens SetFloat (before = 1) and applies 2. Frame 2 applies 3.
    editFrame(history, value, {.changed = true, .active = true}, 2.0f);
    editFrame(history, value, {.changed = true, .active = true}, 3.0f);
    verify(value == 3.0f && history.isOpen() && history.getEntries().empty(), "An edit must apply live and stay open");

    // Frame 3: the mouse is released. finish() sets after = 3, and the history records the command.
    editFrame(history, value, {.finished = true}, 3.0f);
    verify(!history.isOpen() && history.getEntries().size() == 1, "Release must commit one entry");

    // Undo goes back to 1, not to the middle value 2.
    verify(history.undo() && value == 1.0f, "Undo must restore the value from before the edit");
    verify(history.redo() && value == 3.0f, "Redo must restore the value from after the edit");
  }


  // Rule: Escape during an edit puts the start value back and records nothing.
  void testOpenCommandCancels() {
    float value = 1.0f;
    edit::EditHistory history;

    // Frame 1 opens SetFloat (before = 1) and applies 5.
    editFrame(history, value, {.changed = true, .active = true}, 5.0f);

    // Frame 2: Escape. cancel() calls SetFloat::undo(), which sets the value back to 1.
    editFrame(history, value, {.active = true, .canceled = true}, 5.0f);
    verify(value == 1.0f && !history.isOpen() && history.getEntries().empty(), "Escape must restore the value and record nothing");
  }


  // Rule: an edit that ends where it started records nothing.
  void testUnchangedOpenCommandIsDropped() {
    float value = 1.0f;
    edit::EditHistory history;

    // Frame 1 opens SetFloat (before = 1) and applies 4. Frame 2 applies 1 and
    // releases in the same frame. finish() sees after == before and returns false.
    editFrame(history, value, {.changed = true, .active = true}, 4.0f);
    editFrame(history, value, {.changed = true, .finished = true}, 1.0f);
    verify(history.getEntries().empty(), "An unchanged command must not make an entry");
  }



  //=========================
  // Property editing
  //=========================
  //
  // PropertyEditor is an optional layer for documents that expose their data as
  // JSON values at addresses. The generic inspector widgets use it. A session
  // (one interactive edit on one or more fields) is an open JsonDiffAction in the history:
  //
  //   properties.update(owner, label, writes)
  //       The first call opens a JsonDiffAction. Each call saves the before value
  //       of each new address once, then applies the writes to the document.
  //   history.commit()
  //       finish() reads the after values and drops unchanged ones. The history
  //       records the rest.
  //   history.cancel()
  //       JsonDiffAction::undo() writes the before values back.
  //   properties.execute(label, writes)
  //       update() and commit() in one call.
  //
  // These tests started as the characterization tests for the old EditHistory.
  // The rules did not change; only the calls moved between the two classes.

  // A document that stores plain JSON. Each address segment is an object key:
  // the address  v / x  reads data["v"]["x"]. A write of the string "invalid"
  // throws, so tests can check that a failed batch changes nothing.
  struct JsonDocument final : edit::PropertyDocument {
    nlohmann::json data = {{"a", 1}, {"b", 2}, {"v", {{"x", 0}, {"y", 0}}}};


    nlohmann::json read(const edit::EditAddress &address) const override {
      const auto *node = &data;
      for (const auto &segment : address.parts()) {
        node = &node->at(segment);
      }
      return *node;
    }


    // Applies all writes to a copy and replaces the data only when every write
    // succeeds. This is the atomic-batch rule that PropertyDocument requires.
    void apply(const std::vector<edit::PropertyWrite> &writes) override {
      auto candidate = data;
      for (const auto &write : writes) {
        if (write.value == "invalid") {
          throw std::runtime_error("Invalid value at " + write.address.display());
        }
        auto *node = &candidate;
        for (const auto &segment : write.address.parts()) {
          node = &node->at(segment);
        }
        *node = write.value;
      }
      data = std::move(candidate);
    }
  };


  // The three parts that a document editor owns: its data, its history, and
  // the property layer that connects the two.
  struct PropertyFixture {
    JsonDocument document;
    edit::EditHistory history;
    edit::PropertyEditor properties{document, history};
  };


  const edit::EditAddress a = edit::EditAddress() / "a";
  const edit::EditAddress b = edit::EditAddress() / "b";
  const edit::EditAddress v = edit::EditAddress() / "v";


  // Returns the field changes that a committed session recorded.
  const std::vector<edit::FieldChange> &changesOf(const edit::Command &command) {
    return dynamic_cast<const edit::JsonDiffAction &>(command).getChanges();
  }


  // Rule: a session over several frames makes one entry from the first value to the last.
  void testSessionMakesOneEntry() {
    PropertyFixture f;

    // Three frames of an edit on "a": 1 → 3 → 5 → 8. The first update saves before = 1.
    for (int value : {3, 5, 8}) {
      f.properties.update(1, "Edit a", {{a, value}});
    }
    verify(f.history.getEntries().empty(), "An active session must not make an entry");

    // Release. The entry holds one change: a, 1 → 8.
    f.history.commit();
    verify(f.history.getEntries().size() == 1, "A session must make one entry");
    const auto &change = changesOf(*f.history.getEntries()[0]).at(0);
    verify(change.before == 1 && change.after == 8, "The entry must keep the original and final values");

    verify(f.history.undo() && f.document.data["a"] == 1, "Undo must restore the original value");
    verify(f.history.redo() && f.document.data["a"] == 8, "Redo must restore the final value");
    verify(!f.history.redo(), "Redo past the end must return false");
  }


  // Rule: a session that ends at its start value is not an edit, so it must not
  // discard the redo branch. Example: the user clicks a slider without moving it.
  void testUnchangedSessionKeepsRedo() {
    PropertyFixture f;

    // Make "Set a" (1 → 5) and undo it, so the history has one entry to redo.
    f.properties.execute("Set a", {{a, 5}});
    f.history.undo();

    // Edit "b" from 2 to 9 and back to 2, then release.
    f.properties.update(1, "Edit b", {{b, 9}});
    f.properties.update(1, "Edit b", {{b, 2}});
    f.history.commit();

    // "Set a" can still be redone.
    verify(f.history.getEntries().size() == 1 && f.history.canRedo(), "An unchanged session must keep the redo branch");
  }


  // Rule: cancel() puts back the values from before the session and records nothing.
  void testCancelRestoresValues() {
    PropertyFixture f;

    // Edit "a" from 1 to 7, then press Escape.
    f.properties.update(1, "Edit a", {{a, 7}});
    f.history.cancel();

    verify(f.document.data["a"] == 1, "Cancel must restore the original value");
    verify(f.history.getEntries().empty() && !f.history.isOpen(), "Cancel must not make an entry");
  }


  // Rule: Ctrl+Z during an edit cancels the edit. Only the next Ctrl+Z undoes
  // an earlier entry. Otherwise one key press would undo two things.
  void testUndoCancelsActiveSession() {
    PropertyFixture f;

    // One committed entry: "Set a" (1 → 5). Then an edit of "a" to 9 is in progress.
    f.properties.execute("Set a", {{a, 5}});
    f.properties.update(1, "Edit a", {{a, 9}});

    // First undo: the edit is canceled, "a" goes back to 5, and the cursor stays at 1.
    verify(f.history.undo() && f.document.data["a"] == 5 && f.history.getCursor() == 1, "Undo must cancel the session first");

    // Second undo: "Set a" is undone, "a" goes back to 1, and the cursor moves to 0.
    verify(f.history.undo() && f.document.data["a"] == 1 && f.history.getCursor() == 0, "The next undo must restore the entry");
  }


  // Rule: a command from somewhere else during an edit (for example, a
  // menu action) commits the edit first. The two edits stay separate undo steps, in
  // the order that the user made them.
  void testCommandCommitsActiveSession() {
    PropertyFixture f;
    NameList list;

    // An edit of "a" is in progress, then an unrelated command runs.
    f.properties.update(1, "Edit a", {{a, 4}});
    f.history.execute(makeBox<edit::LambdaAction>("Add Car", [&] { list.names.push_back("Car"); }, [&] { list.names.pop_back(); }));

    // Entries: ["Edit a", "Add Car"]. No session is open.
    verify(f.history.getEntries().size() == 2 && !f.history.isOpen(), "A command must commit the active session");
    verify(f.history.getEntries()[0]->getLabel() == "Edit a", "The session must come before the command");
  }


  // Rule: a new edit after an undo removes the entries that could be redone.
  void testNewEditDiscardsRedo() {
    PropertyFixture f;

    // Entries: ["Set a", "Set b"]. Undo "Set b", so the cursor is at 1.
    f.properties.execute("Set a", {{a, 5}});
    f.properties.execute("Set b", {{b, 6}});
    f.history.undo();

    // A new edit replaces "Set b". Entries: ["Set a", "Set a"].
    f.properties.execute("Set a", {{a, 7}});
    verify(f.history.getEntries().size() == 2 && !f.history.canRedo(), "An effective edit must discard the redo branch");
    verify(f.history.getEntries()[1]->getLabel() == "Set a", "The new entry must replace the undone entry");
  }


  // Rule: one session cannot write both a value and a field inside it. With
  // both  v  and  v / x  recorded, undo would have to choose which before value
  // wins. The batch is rejected before it reaches the document.
  void testOverlappingWritesThrow() {
    PropertyFixture f;

    // The session writes v.x = 1, then tries to write all of v.
    f.properties.update(1, "Edit v.x", {{v / "x", 1}});
    throws([&] { f.properties.update(1, "Set v", {{v, {{"x", 2}, {"y", 2}}}}); }, "Overlapping edit paths");

    // v.x is still 1 from the first write, not 2.
    verify(f.document.data["v"]["x"] == 1, "A rejected batch must not change the document");
  }


  // Rule: if the document rejects a batch, nothing changes and nothing is recorded.
  // The before value of "a" must not stay in the session from the failed attempt.
  void testFailedBatchLeavesNoRecord() {
    PropertyFixture f;

    // The batch writes a = 4 and b = "invalid". The document throws on b.
    throws([&] { f.properties.update(1, "Set a and b", {{a, 4}, {b, "invalid"}}); }, "Invalid value");
    verify(f.document.data["a"] == 1, "A failed batch must not change the document");

    f.history.commit();
    verify(f.history.getEntries().empty(), "A failed batch must not make an entry");
  }


  // Rule: only one owner (widget) can have an open session. When a second
  // widget starts to edit, the first widget's session is committed.
  void testOwnerChangeCommits() {
    PropertyFixture f;

    // Widget 1 edits "a". Then widget 2 starts to edit "b".
    f.properties.update(1, "Edit a", {{a, 4}});
    f.properties.update(2, "Edit b", {{b, 5}});

    // "Edit a" is committed, and widget 2 now owns the open session.
    verify(f.history.getEntries().size() == 1 && f.history.isOwner(2), "A new owner must commit the previous session");
  }


  // Rule: if a widget stops drawing during its session (the user selects
  // another node, or switches tabs), endFrame() commits the session. The widget
  // can never send its own "finished" event, so without this the session stays open.
  void testHiddenOwnerCommitsAtEndOfFrame() {
    PropertyFixture f;

    // Frame 1: widget 1 opens the session. open() counts as drawn, so the
    // session survives this frame's endFrame().
    f.properties.update(1, "Edit a", {{a, 4}});
    f.history.endFrame();
    verify(f.history.isOpen(), "A session must survive the frame that opened it");

    // Frame 2: widget 1 draws and calls touch(1), so the session stays open.
    f.history.touch(1);
    f.history.endFrame();
    verify(f.history.isOpen(), "A visible owner must keep its session");

    // Frame 3: widget 1 does not draw, so endFrame() commits its session.
    f.history.endFrame();
    verify(!f.history.isOpen() && f.history.getEntries().size() == 1, "A hidden owner must commit at the end of the frame");
  }
}  // namespace


int main() {
  testRegistrationUsesObjTypes();
  testTransformRoundTrip();

  testAddressSegmentsDoNotSplit();
  testAddressContains();

  testLambdaActionOnNativeData();
  testRecordDoesNotRun();
  testOpenCommandCommits();
  testOpenCommandCancels();
  testUnchangedOpenCommandIsDropped();

  testSessionMakesOneEntry();
  testUnchangedSessionKeepsRedo();
  testCancelRestoresValues();
  testUndoCancelsActiveSession();
  testCommandCommitsActiveSession();
  testNewEditDiscardsRedo();
  testOverlappingWritesThrow();
  testFailedBatchLeavesNoRecord();
  testOwnerChangeCommits();
  testHiddenOwnerCommitsAtEndOfFrame();
  std::cout << "Edit model tests passed\n";
  return 0;
}
