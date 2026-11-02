#pragma once

#include <string>
#include <ren/types.h>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <ren/core/ui/Widgets.h>
#include <ren/edit/EditContext.h>
#include <flecs/flecs.h>

namespace ren::obj {

  struct Type;
  struct Value;

  // The same generic lookup serves every T. TypeDefinition<T> selects its class;
  // the function itself has no specializations or per-type behavior.
  template <typename T>
  const Type& getType();



  // A field defines one member binding.
  struct Field {
    std::string name;
    std::string friendlyName;  // 'firstName' -> 'First Name', 'user4' -> 'User 4'
    const Type* type;

    Field(std::string name, const Type& type);

    virtual ~Field() = default;


    virtual Value readMember(const void* object) const = 0;
    virtual void writeMember(const Value& value, void* object) const = 0;
  };


  enum class TypeKind { Primitive, Structure, Sequence };

  struct Type {
    std::string name;
    TypeKind kind;
    std::vector<ren::Box<Field>> fields;

    Type(std::string name, TypeKind kind)
        : name(std::move(name))
        , kind(kind) {}


    virtual ~Type() = default;


    // TODO: document.
    virtual Value readNative(const void* object) const = 0;
    virtual void writeNative(const Value& value, void* object) const = 0;
    virtual json serialize(const Value& value) const = 0;
    virtual Value deserialize(const Value& defaults, const json& document, const std::string& path) const = 0;
    // A null editor uses the recursive structure layout.
    virtual const ren::edit::Editor* getEditor() const { return nullptr; }
  };


  // Owns one authoring value. A primitive stores its C++ value in std::any;
  // structures store children in field order; sequences store them in index order.
  // Copies own independent data but share immutable, static type descriptors.
  struct Value {
    const Type* type = nullptr;

    std::any primitive;
    std::vector<Value> children;


    json serialize() const { return type->serialize(*this); }
    Value deserialize(const json& document, const std::string& path) const { return type->deserialize(*this, document, path); }

    // Sequence indices share the path format with field names. Reject malformed
    // indices and missing elements before a write can reach the value tree.
    Value& findChild(std::string_view segment);

    // Finds a child through dot-separated segments, such as transforms.0.scale.x.
    // Paths do not depend on editor layout. The result refers into this tree;
    // replacing the tree invalidates it. Missing path segments throw.
    Value& findField(std::string_view path);

    // Constructs a native snapshot without changing the authoring value. The
    // descriptor must match T. Callers validate primitive data before this step;
    // this function only enforces type identity and performs typed assignment.
    template <typename T>
    T bake() const {
      if (this->type != &getType<T>()) {
        throw std::runtime_error("Bake type mismatch: " + getType<T>().name);
      }

      T result{};
      this->type->writeNative(*this, &result);
      return result;
    }
  };




  // Copies declared members into owned authoring data. Reading a default object
  // also captures member defaults, such as a scale of one instead of zero.
  template <typename T>
  Value readNative(const T& object) {
    return getType<T>().readNative(&object);
  }


  namespace detail {

    // Recovers the enclosing class and member type from a pointer to a member.
    template <typename M>
    struct MemberTraits;

    template <typename C, typename F>
    struct MemberTraits<F C::*> {
      using Class = C;
      using Member = F;
    };


    // Retains a member pointer in the class's template argument. Named virtual
    // methods bridge generic field access to typed copying and assignment.
    template <typename C, auto Member>
    struct MemberField final : Field {
      using Traits = MemberTraits<decltype(Member)>;
      using F = typename Traits::Member;
      static_assert(std::is_same_v<typename Traits::Class, C>);

      explicit MemberField(const char* name)
          : Field(name, getType<F>()) {}

      Value readMember(const void* object) const override {
        // Read the member from the object.
        return ren::obj::readNative(static_cast<const C*>(object)->*Member);
      }

      void writeMember(const Value& value, void* object) const override {
        // Write the value to the member by baking it.
        static_cast<C*>(object)->*Member = value.bake<F>();
      }
    };
  }  // namespace detail


  // Builds one structure descriptor from typed member pointers. Components,
  // nested structures, and library types all use the same field declarations.
  // Add fields during construction, before values depend on descriptor order.
  template <typename C>
  struct TypeBuilder {
    Type& type;

    // Names are JSON keys and path segments. Each must be unique in this structure.
    template <auto Member>
    void field(const char* name) {
      for (const auto& existing : type.fields) {
        if (existing->name == name) {
          throw std::runtime_error("Duplicate field: " + type.name + "." + name);
        }
      }

      type.fields.push_back(ren::makeBox<detail::MemberField<C, Member>>(name));
    }
  };


  // This lets you do something like this:
  //
  //    struct MyComponent {
  //      int x;
  //      float y;
  //
  //      ren_describe(MyComponent, x, y);
  //    };
  //
  // Which is pretty neat.
#define REN_DESCRIBE_FIELD(Member) renDescribeBuilder.template field<&RenDescribeType::Member>(#Member);
#define ren_describe(T, ...)                                             \
  static void describe(::ren::obj::TypeBuilder<T>& renDescribeBuilder) { \
    using RenDescribeType = T;                                           \
    renDescribeBuilder.type.name = #T;                                   \
    __VA_OPT__(NLOHMANN_JSON_PASTE(REN_DESCRIBE_FIELD, __VA_ARGS__))     \
  }


  // Supplies copying and dispatch for one primitive C++ type. std::any owns T
  // in each Value; this descriptor only supplies operations on that data.
  // Concrete definitions implement typed JSON conversion and a typed editor.
  template <typename T>
  struct PrimitiveType : Type, ren::edit::Editor {
    explicit PrimitiveType(std::string name)
        : Type(std::move(name), TypeKind::Primitive) {}


    Value readNative(const void* object) const final { return Value{.type = this, .primitive = *static_cast<const T*>(object)}; }


    void writeNative(const Value& value, void* object) const final { *static_cast<T*>(object) = std::any_cast<const T&>(value.primitive); }


    json serialize(const Value& value) const final { return encodeValue(std::any_cast<const T&>(value.primitive)); }


    Value deserialize(const Value& defaults, const json& document, const std::string& path) const final {
      Value value = defaults;
      try {
        value.primitive = decodeValue(document);
      } catch (const std::exception& error) {
        throw std::runtime_error(path + ": " + error.what());
      }
      return value;
    }


    // Definitions can select another Editor, such as a texture picker with a preview.
    const ren::edit::Editor* getEditor() const override { return this; }


    // Drafts can contain incomplete reference text while the document retains
    // valid data. External writes invalidate the draft through its revision.
    void draw(eui::Ui& ui, ren::edit::EditContext& context) const final {
      auto& draft = context.state<Draft>();
      if (!draft.active || draft.revision != context.getRevision()) {
        draft.value = decodeValue(context.read());
      }
      const auto interaction = context.edit([&] { return inspectPrimitive(ui, "##value", draft.value); }, [&] { return encodeValue(draft.value); });
      draft.active = interaction.active;
      draft.revision = context.getRevision();
    }


    virtual json encodeValue(const T& value) const = 0;
    virtual T decodeValue(const json& document) const = 0;
    virtual bool inspectPrimitive(eui::Ui& ui, const char* label, T& value) const = 0;

    struct Draft {
      T value{};
      bool active = false;
      u64 revision = 0;
    };
  };




  template <typename T>
  struct StructType : Type {
    explicit StructType(const char* name)
        : Type(name, TypeKind::Structure) {}


    Value readNative(const void* object) const final {
      Value value{.type = this};
      for (const auto& field : fields) {
        value.children.push_back(field->readMember(object));
      }
      return value;
    }

    void writeNative(const Value& value, void* object) const final {
      if (value.children.size() != fields.size()) {
        throw std::runtime_error("Bake field count mismatch: " + name);
      }

      for (size_t i = 0; i < value.children.size(); ++i) {
        fields[i]->writeMember(value.children[i], object);
      }
    }

    // Field names supply JSON keys; each child descriptor supplies its own codec.
    json serialize(const Value& value) const final {
      json document = json::object();
      for (size_t i = 0; i < value.children.size(); ++i) {
        document[fields[i]->name] = value.children[i].serialize();
      }
      return document;
    }

    // Omitted fields retain component defaults. The caller commits a new tree
    // only after every child succeeds, so invalid data cannot replace part of it.
    Value deserialize(const Value& defaults, const json& document, const std::string& path) const final {
      if (!document.is_object()) {
        throw std::runtime_error(path + ": Expected an object");
      }

      // Reject unknown keys so a misspelled field cannot silently use its default.
      for (auto it = document.begin(); it != document.end(); ++it) {
        bool known = false;
        for (const auto& field : fields) {
          known |= field->name == it.key();
        }
        if (!known) {
          throw std::runtime_error(path + "." + it.key() + ": Unknown field");
        }
      }

      Value value = defaults;
      for (size_t i = 0; i < value.children.size(); ++i) {
        const auto& field = *fields[i];
        if (document.contains(field.name)) {
          value.children[i] = defaults.children[i].deserialize(document.at(field.name), path + "." + field.name);
        }
      }
      return value;
    }
  };


  // This concrete descriptor describes T; it does not contain an instance of T.
  // The default uses T::describe(). Specializations choose another descriptor
  // class for primitives or declare fields for library types such as glm::vec3.
  template <typename T>
  struct TypeDefinition final : StructType<T> {
    TypeDefinition()
        : StructType<T>("") {
      TypeBuilder<T> builder{*this};
      T::describe(builder);
    }
  };


  template <typename T>
  const Type& getType() {
    // Should always return the same static instance for each T.
    // TODO: how does this interact with dynamic libraries? Is it possible to
    // get two different statics for the same T?
    static const TypeDefinition<T> type;
    return type;
  }

  // A sequence type, (std::vector<T>).
  struct SequenceType : Type {
    const Type& elementType;

    explicit SequenceType(const Type& elementType)
        : Type("vector<" + elementType.name + ">", TypeKind::Sequence)
        , elementType(elementType) {}


    virtual Value makeElement() const = 0;


    json serialize(const Value& value) const final {
      json document = json::array();
      for (const auto& child : value.children) {
        document.push_back(child.serialize());
      }
      return document;
    }


    // An array replaces the full sequence. Each element starts from T{} so
    // omitted structure fields retain defaults, including newly added elements.
    Value deserialize(const Value&, const json& document, const std::string& path) const final {
      if (!document.is_array()) {
        throw std::runtime_error(path + ": Expected an array");
      }

      Value value{.type = this};
      value.children.reserve(document.size());
      for (size_t i = 0; i < document.size(); ++i) {
        value.children.push_back(makeElement().deserialize(document[i], path + "." + std::to_string(i)));
      }
      return value;
    }
  };

  // Copies through T rather than element addresses to support vector<bool>'s
  // proxy references. Native storage stays separate from authoring children.
  template <typename T, typename Allocator>
  struct TypeDefinition<std::vector<T, Allocator>> final : SequenceType {
    using Vector = std::vector<T, Allocator>;

    TypeDefinition()
        : SequenceType(getType<T>()) {}


    Value makeElement() const override { return ren::obj::readNative(T{}); }


    Value readNative(const void* object) const override {
      const auto& native = *static_cast<const Vector*>(object);
      Value value{.type = this};
      value.children.reserve(native.size());
      for (const T element : native) {
        value.children.push_back(ren::obj::readNative(element));
      }
      return value;
    }


    void writeNative(const Value& value, void* object) const override {
      auto& native = *static_cast<Vector*>(object);
      Vector result(native.get_allocator());
      result.reserve(value.children.size());
      for (const auto& child : value.children) {
        result.push_back(child.bake<T>());
      }
      native = std::move(result);
    }
  };




  //=========================
  // Primitive definitions
  //=========================

  // Supplies names, JSON conversion, validation, and editors for ordinary scalar
  // and string types. Registration needs only an empty TypeDefinition<T> class.
  // Semantic primitives, such as asset references, can still define custom methods.
  template <typename T>
  struct SimplePrimitiveType : PrimitiveType<T> {
    static_assert(std::is_same_v<T, std::string> || std::is_same_v<T, float> || std::is_same_v<T, double> ||
                      (std::is_integral_v<T> && sizeof(T) <= sizeof(u64)),
                  "Define custom primitive behavior for this type");

    SimplePrimitiveType()
        : PrimitiveType<T>(getName()) {}


    json encodeValue(const T& value) const override { return value; }


    T decodeValue(const json& document) const override {
      if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::string>) {
        // Exact access rejects other JSON kinds. Returning T copies owned data.
        return document.template get_ref<const T&>();
      } else if constexpr (std::is_floating_point_v<T>) {
        if (!document.is_number()) {
          throw std::runtime_error("Expected a finite " + this->name);
        }

        const double number = document.get<double>();
        if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<T>::max()) {
          throw std::runtime_error("Expected a finite " + this->name);
        }
        return static_cast<T>(number);
      } else {
        // Deal with JSON numbers.
        if (document.is_number_unsigned()) {
          return narrowInteger(document.template get_ref<const json::number_unsigned_t&>());
        }
        if (document.is_number_integer()) {
          return narrowInteger(document.template get_ref<const json::number_integer_t&>());
        }
        throw std::runtime_error(expectedInteger());
      }
    }


    bool inspectPrimitive(eui::Ui& ui, const char* label, T& value) const override { return ui.inspect(label, value); }


    // Names describe the scalar width rather than a compiler-specific spelling.
    // Keep the original int name for i32 so existing schema labels stay stable.
    static std::string getName() {
      if constexpr (std::is_same_v<T, std::string>) {
        return "string";
      } else if constexpr (std::is_same_v<T, bool>) {
        return "bool";
      } else if constexpr (std::is_same_v<T, float>) {
        return "float";
      } else if constexpr (std::is_same_v<T, double>) {
        return "double";
      } else if constexpr (std::is_same_v<T, i32>) {
        return "int";
      } else {
        return fmt::format("{}{}", std::is_signed_v<T> ? "i" : "u", sizeof(T) * 8);
      }
    }


    std::string expectedInteger() const {
      return fmt::format("Expected {} {}-bit integer", std::is_signed_v<T> ? "a" : "an unsigned", sizeof(T) * 8);
    }


    template <typename Integer>
    T narrowInteger(Integer number) const {
      if (!std::in_range<T>(number)) {
        throw std::runtime_error(expectedInteger());
      }
      return static_cast<T>(number);
    }
  };



  // Use this macro for types that share scalar behavior. Define a specialization
  // directly when the type needs another JSON format, validation rule, or editor.
#define REN_SIMPLE_PRIMITIVE(CppType) \
  template <>                         \
  struct TypeDefinition<CppType> final : SimplePrimitiveType<CppType> {}

  REN_SIMPLE_PRIMITIVE(float);
  REN_SIMPLE_PRIMITIVE(double);
  REN_SIMPLE_PRIMITIVE(i8);
  REN_SIMPLE_PRIMITIVE(i16);
  REN_SIMPLE_PRIMITIVE(i32);
  REN_SIMPLE_PRIMITIVE(i64);
  REN_SIMPLE_PRIMITIVE(u8);
  REN_SIMPLE_PRIMITIVE(u16);
  REN_SIMPLE_PRIMITIVE(u32);
  REN_SIMPLE_PRIMITIVE(u64);
  REN_SIMPLE_PRIMITIVE(bool);
  REN_SIMPLE_PRIMITIVE(std::string);

#undef REN_SIMPLE_PRIMITIVE




  // ==================================
  // Core types
  // ==================================

  // Draws all components as one control. The parent owns its label. Each
  // component is still its own write at its own address, so an edit of X
  // records only x: JsonDiffAction drops the unchanged components at commit.
  template <int N, typename T>
  struct VectorEditor final : ren::edit::Editor {
    static_assert(N >= 1 && N <= 4);

    void draw(eui::Ui& ui, ren::edit::EditContext& context) const override {
      static const char* fields[] = {"x", "y", "z", "w"};

      // Each frame applies its change to the document, so the document always
      // holds the value that the control shows.
      const auto current = context.read();
      glm::vec<N, T, glm::defaultp> value;
      for (int i = 0; i < N; ++i) {
        value[i] = current.at(fields[i]).template get<T>();
      }

      context.edit([&] { return ui.drag("##value", value); },
                   [&] {
                     std::vector<ren::edit::PropertyWrite> writes;
                     for (int i = 0; i < N; ++i) {
                       writes.push_back({context.getAddress() / fields[i], value[i]});
                     }
                     return writes;
                   });
    }
  };


  // Declares ordinary float children for a library type. Its shared editor owns
  // no field data or mutable state; the edit context supplies both per instance.
  template <int N, typename T>
  struct TypeDefinition<glm::vec<N, T, glm::defaultp>> final : StructType<glm::vec<N, T, glm::defaultp>> {
    using Vec = glm::vec<N, T, glm::defaultp>;
    TypeDefinition()
        : StructType<Vec>(flecs::_::type_name<Vec>()) {
      TypeBuilder<Vec> builder{*this};
      if constexpr (N > 0) {
        builder.template field<&Vec::x>("x");
      }

      if constexpr (N > 1) {
        builder.template field<&Vec::y>("y");
      }

      if constexpr (N > 2) {
        builder.template field<&Vec::z>("z");
      }

      if constexpr (N > 3) {
        builder.template field<&Vec::w>("w");
      }
    }


    const ren::edit::Editor* getEditor() const override {
      static const VectorEditor<N, T> editor;
      return &editor;
    }
  };


  template <typename T, glm::qualifier P>
  struct TypeDefinition<glm::qua<T, P>> final : StructType<glm::qua<T, P>> {
    using Quat = glm::qua<T, P>;
    TypeDefinition()
        : StructType<Quat>(flecs::_::type_name<Quat>()) {
      TypeBuilder<Quat> builder{*this};
      builder.template field<&Quat::x>("x");
      builder.template field<&Quat::y>("y");
      builder.template field<&Quat::z>("z");
      builder.template field<&Quat::w>("w");
    }


    const ren::edit::Editor* getEditor() const override {
      // Quaternions are just four floats, so we can treat it like a 4d vector just fine.
      static const VectorEditor<4, T> editor;
      return &editor;
    }
  };

}  // namespace ren::obj