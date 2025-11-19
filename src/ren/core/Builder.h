// This is a macro for generating builder classes for resources.



#define BUILDER_SETTER(name, type, member) \
  auto &set##name(type newValue) {         \
    this->member = newValue;               \
    return *this;                          \
  }