#pragma once

#include <functional>
#include <string>
#include <utility>

namespace ren::edit {

  // One undoable change. A command captures its own target and the values that
  // it needs, so the history does not know what it changes. Capture IDs and
  // values, not pointers into data that undo can delete and create again.
  struct Command {
    // Supports destruction through the history.
    virtual ~Command() = default;

    // Returns the text for history lists and menus.
    virtual std::string getLabel() const = 0;

    // Applies the change. The history calls this for execute() and redo.
    virtual void redo() = 0;

    // Reverts the change. The history calls this for undo and for cancel.
    virtual void undo() = 0;

    // Called once when an open command closes. Capture the final state here.
    // Return false when nothing changed; the history then drops the command.
    virtual bool finish() { return true; }
  };


  // Wraps two functions as a command, for changes that need no class of their
  // own. The functions own their captures; they must stay valid while the
  // history holds the command.
  class LambdaAction final : public Command {
   public:
    LambdaAction(std::string label, std::function<void()> redo, std::function<void()> undo);

    std::string getLabel() const override;
    void redo() override;
    void undo() override;

   private:
    std::string label;
    std::function<void()> redoFunction;
    std::function<void()> undoFunction;
  };


  // Sets one value in place, for interactive edits of plain data. The history
  // opens it when an edit starts, so the constructor captures the value before
  // the edit; finish() captures the value after it. `target` must outlive the
  // history, so use it only for data that undo never deletes. T must be
  // copyable and comparable.
  template <typename T>
  class SetValue final : public Command {
   public:
    SetValue(std::string label, T& target)
        : label(std::move(label))
        , target(target)
        , before(target)
        , after(target) {}


    std::string getLabel() const override { return label; }
    void redo() override { target = after; }
    void undo() override { target = before; }


    // An edit that ends at its start value makes no entry.
    bool finish() override {
      after = target;
      return after != before;
    }

   private:
    std::string label;
    T& target;
    T before;
    T after;
  };

}  // namespace ren::edit
