#include <ren/edit/Command.h>

#include <utility>

namespace ren::edit {

  LambdaAction::LambdaAction(std::string label, std::function<void()> redo, std::function<void()> undo)
      : label(std::move(label))
      , redoFunction(std::move(redo))
      , undoFunction(std::move(undo)) {}


  std::string LambdaAction::getLabel() const { return label; }


  void LambdaAction::redo() { redoFunction(); }


  void LambdaAction::undo() { undoFunction(); }

}  // namespace ren::edit
