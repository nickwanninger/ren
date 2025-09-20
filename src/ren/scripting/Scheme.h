#pragma once
#include <ren/scripting/scheme/s7.h>
#include <cstdlib>
#include <string>
#include <string_view>


namespace ren {


  class Scheme; // fwd

  // An interface around the s7 scheme interpreter.
  class Scheme {
   public:
    Scheme();
    ~Scheme();

    // Evaluate a snippet of Scheme code and return the resulting value.
    s7_pointer eval(const std::string& code) const;

    // Bind a native function into the Scheme environment.
    s7_pointer bind(const std::string& name,
                    s7_function function,
                    s7_int required_args,
                    s7_int optional_args = 0,
                    bool rest_arg = false,
                    const char* doc = nullptr);


    // Load and evaluate a Scheme source file.
    s7_pointer load(const std::string& path) const;

    s7_scheme* context() const { return scheme_; }

   private:
    s7_scheme* scheme_;
  };


}  // namespace ren
