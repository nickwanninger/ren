#include <ren/scripting/Scheme.h>

#include <ren/scripting/EcsBindings.h>

#include <stdexcept>

namespace ren {

  Scheme::Scheme()
      : scheme_(s7_init()) {}

  Scheme::~Scheme() { s7_free(scheme_); }

  s7_pointer Scheme::eval(const std::string &code) const {
    if (code.empty()) { throw std::invalid_argument("Scheme::eval requires non-empty code"); }
    return s7_eval_c_string(scheme_, code.c_str());
  }


  s7_pointer Scheme::bind(const std::string &name, s7_function function, s7_int required_args,
                          s7_int optional_args, bool rest_arg, const char *doc) {
    if (name.empty()) { throw std::invalid_argument("Scheme::bind requires a function name"); }
    if (!function) {
      throw std::invalid_argument("Scheme::bind requires a valid function callback");
    }
    return s7_define_function(scheme_, name.c_str(), function, required_args, optional_args,
                              rest_arg, doc);
  }



  s7_pointer Scheme::load(const std::string &path) const {
    if (path.empty()) { throw std::invalid_argument("Scheme::load requires a path"); }
    if (auto result = s7_load(scheme_, path.c_str()); result != nullptr) { return result; }
    throw std::runtime_error("Scheme::load failed to load " + path);
  }


  void Scheme::define(const std::string_view &name, SVal value) {
    if (name.empty()) { throw std::invalid_argument("Scheme::define requires a name"); }
    s7_define_variable(scheme_, name.data(), value.raw());
  }

}  // namespace ren
