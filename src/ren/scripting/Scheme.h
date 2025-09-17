#pragma once
#include <ren/scripting/scheme/s7.h>

#include <cstdlib>
#include <string>
#include <string_view>


namespace ren {


  class Scheme; // fwd

  class SVal {
   protected:
    friend class ren::Scheme;
    SVal()
        : s_(nullptr)
        , v_(nullptr) {}
    SVal(s7_scheme* s, s7_pointer v)
        : s_(s)
        , v_(v) {}

   public:
    s7_pointer raw() const { return v_; }
    s7_scheme* state() const { return s_; }
    explicit operator bool() const { return v_ != nullptr; }

    bool isBool() const { return s7_is_boolean(v_); }
    bool isInt() const { return s7_is_integer(v_); }
    bool isReal() const { return s7_is_real(v_) || s7_is_integer(v_); }
    bool isString() const { return s7_is_string(v_); }
    bool isList() const { return s7_is_pair(v_) || v_ == s7_nil(s_); }
    bool isPointer() const { return s7_is_c_pointer(v_); }

    bool asBool() const { return s7_boolean(s_, v_); }
    long long asInt() const { return static_cast<long long>(s7_integer(v_)); }
    double asReal() const {
      return s7_is_integer(v_) ? static_cast<double>(s7_integer(v_)) : s7_real(v_);
    }
    void *asPointer() const { return s7_c_pointer(v_); }
    std::string str() const {
      if (s7_is_string(v_)) return s7_string(v_);
      char* s = s7_object_to_c_string(s_, v_);
      if (!s) return {};
      std::string out(s);
      free(s);
      return out;
    }

    static SVal makeBool(s7_scheme* s, bool b) { return {s, b ? s7_t(s) : s7_f(s)}; }
    static SVal makeInt(s7_scheme* s, long long i) { return {s, s7_make_integer(s, i)}; }
    static SVal makeReal(s7_scheme* s, double d) { return {s, s7_make_real(s, d)}; }
    static SVal makeString(s7_scheme* s, const std::string& str) {
      return {s, s7_make_string(s, str.c_str())};
    }

   private:
    s7_scheme* s_;
    s7_pointer v_;
  };

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


    /* ----- construction helpers ----- */
    SVal makeBool(bool b) { return SVal::makeBool(scheme_, b); }
    SVal makeInt(long long i) { return SVal::makeInt(scheme_, i); }
    SVal makeReal(double d) { return SVal::makeReal(scheme_, d); }
    SVal makeString(const std::string& s) { return SVal::makeString(scheme_, s); }


    SVal makePointer(void *p) {
      return SVal(scheme_, s7_make_c_pointer(scheme_, p));
    }

    void define(const std::string_view &name, SVal value);

   private:
    s7_scheme* scheme_;
  };


}  // namespace ren
