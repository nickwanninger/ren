#include <ren/obj/Types.h>



namespace ren::obj {


  // Formats identifier words for display without changing serialized names.
  // Keep acronym runs together, but split their final capital before a word.
  std::string makeFriendlyName(std::string_view name) {
    std::string result;
    result.reserve(name.size());
    bool wordStart = true;
    for (size_t i = 0; i < name.size(); ++i) {
      const auto current = static_cast<unsigned char>(name[i]);
      if (current == '_' || std::isspace(current)) {
        wordStart = true;
        continue;
      }

      if (i > 0) {
        const auto previous = static_cast<unsigned char>(name[i - 1]);
        const auto next = i + 1 < name.size() ? static_cast<unsigned char>(name[i + 1]) : 0;
        const bool capital = std::isupper(current) && (std::islower(previous) || (std::isupper(previous) && std::islower(next)));
        const bool digit = (std::isdigit(current) && std::isalpha(previous)) || (std::isalpha(current) && std::isdigit(previous));
        wordStart |= capital || digit;
      }
      if (wordStart && !result.empty()) {
        result += ' ';
      }
      result += wordStart ? static_cast<char>(std::toupper(current)) : static_cast<char>(current);
      wordStart = false;
    }
    return result;
  }



  //=========================
  // Field
  //=========================

  Field::Field(std::string name, const Type& type)
      : name(std::move(name))
      , friendlyName(makeFriendlyName(this->name))
      , type(&type) {}


  //=========================
  // Values
  //=========================

  // Sequence indices share the path format with field names. Reject malformed
  // indices and missing elements before a write can reach the value tree.
  Value& Value::findChild(std::string_view segment) {
    if (type->kind == TypeKind::Sequence) {
      size_t index = 0;
      const auto [end, error] = std::from_chars(segment.data(), segment.data() + segment.size(), index);
      if (error == std::errc{} && end == segment.data() + segment.size() && std::to_string(index) == segment && index < children.size()) {
        return children[index];
      }
    } else {
      for (size_t i = 0; i < type->fields.size(); ++i) {
        if (type->fields[i]->name == segment) {
          return children[i];
        }
      }
    }
    throw std::runtime_error("Unknown child: " + std::string(segment));
  }


  // Finds a child through dot-separated segments, such as transforms.0.scale.x.
  // Paths do not depend on editor layout. The result refers into this tree;
  // replacing the tree invalidates it. Missing path segments throw.
  Value& Value::findField(std::string_view path) {
    const auto dot = path.find('.');
    const auto name = path.substr(0, dot);
    auto& child = findChild(name);
    return dot == std::string_view::npos ? child : child.findField(path.substr(dot + 1));
  }



}  // namespace ren::obj