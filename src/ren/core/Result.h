#pragma once

#include <utility>
#include <stdexcept>
#include <type_traits>
#include <ren/core/Option.h>

namespace ren {



  template <typename T>
  struct Ok {
    T value;
    explicit Ok(T v)
        : value(std::move(v)) {}
  };

  template <typename E>
  struct Err {
    E value;
    explicit Err(E v)
        : value(std::move(v)) {}
  };


  template <typename T, typename E>
  class Result {
   private:
    alignas(alignof(T) > alignof(E) ? alignof(T) : alignof(E)) unsigned char m_buffer[sizeof(T) > sizeof(E) ? sizeof(T) : sizeof(E)];
    bool m_is_ok;

    T* ok_ptr() { return reinterpret_cast<T*>(m_buffer); }
    const T* ok_ptr() const { return reinterpret_cast<const T*>(m_buffer); }
    E* err_ptr() { return reinterpret_cast<E*>(m_buffer); }
    const E* err_ptr() const { return reinterpret_cast<const E*>(m_buffer); }

   public:
    template <typename U>
      requires std::convertible_to<U, T>
    Result(ren::Ok<U> ok)
        : m_is_ok(true) {
      new (ok_ptr()) T(static_cast<T>(std::move(ok.value)));
    }

    template <typename F>
      requires std::convertible_to<F, E>
    Result(ren::Err<F> err)
        : m_is_ok(false) {
      new (err_ptr()) E(std::move(err.value));
    }


    template <typename U, typename F>
      requires std::convertible_to<U, T> && std::convertible_to<F, E>
    Result(const Result<U, F>& other)
        : m_is_ok(other.is_ok()) {
      if (m_is_ok) {
        new (ok_ptr()) T(static_cast<T>(other.unwrap()));
      } else {
        new (err_ptr()) E(static_cast<E>(other.unwrap_err()));
      }
    }


    // Constructors
    static Result Ok(T value) { return Result(ren::Ok<T>(std::move(value))); }

    static Result Err(E error) { return Result(ren::Err<E>(std::move(error))); }

    // Destructor
    ~Result() {
      if (m_is_ok) {
        if constexpr (!std::is_trivially_destructible_v<T>) {
          ok_ptr()->~T();
        }
      } else {
        if constexpr (!std::is_trivially_destructible_v<E>) {
          err_ptr()->~E();
        }
      }
    }

    // Copy constructor
    Result(const Result& other)
        : m_is_ok(other.m_is_ok) {
      if (m_is_ok) {
        new (ok_ptr()) T(*other.ok_ptr());
      } else {
        new (err_ptr()) E(*other.err_ptr());
      }
    }

    // Move constructor
    Result(Result&& other) noexcept
        : m_is_ok(other.m_is_ok) {
      if (m_is_ok) {
        new (ok_ptr()) T(std::move(*other.ok_ptr()));
      } else {
        new (err_ptr()) E(std::move(*other.err_ptr()));
      }
    }

    // Copy assignment
    Result& operator=(const Result& other) {
      if (this != &other) {
        this->~Result();
        m_is_ok = other.m_is_ok;
        if (m_is_ok) {
          new (ok_ptr()) T(*other.ok_ptr());
        } else {
          new (err_ptr()) E(*other.err_ptr());
        }
      }
      return *this;
    }

    // Move assignment
    Result& operator=(Result&& other) noexcept {
      if (this != &other) {
        this->~Result();
        m_is_ok = other.m_is_ok;
        if (m_is_ok) {
          new (ok_ptr()) T(std::move(*other.ok_ptr()));
        } else {
          new (err_ptr()) E(std::move(*other.err_ptr()));
        }
      }
      return *this;
    }

    // Query methods
    bool is_ok() const { return m_is_ok; }
    bool is_err() const { return !m_is_ok; }
    explicit operator bool() const { return is_ok(); }


    Option<T> ok() const { return is_ok() ? Some(*ok_ptr()) : None; }
    Option<E> err() const { return is_err() ? Some(*err_ptr()) : None; }



    // Access methods (throw if wrong variant)
    T& unwrap() & {
      if (is_err()) {
        throw std::runtime_error("Called unwrap() on Err value");
      }
      return *ok_ptr();
    }

    const T& unwrap() const& {
      if (is_err()) {
        throw std::runtime_error("Called unwrap() on Err value");
      }
      return *ok_ptr();
    }

    T unwrap() && {
      if (is_err()) {
        throw std::runtime_error("Called unwrap() on Err value");
      }
      return std::move(*ok_ptr());
    }

    E& unwrap_err() & {
      if (is_ok()) {
        throw std::runtime_error("Called unwrap_err() on Ok value");
      }
      return *err_ptr();
    }

    const E& unwrap_err() const& {
      if (is_ok()) {
        throw std::runtime_error("Called unwrap_err() on Ok value");
      }
      return *err_ptr();
    }

    E unwrap_err() && {
      if (is_ok()) {
        throw std::runtime_error("Called unwrap_err() on Ok value");
      }
      return std::move(*err_ptr());
    }

    // Safe access with default
    T unwrap_or(T default_value) const& { return is_ok() ? *ok_ptr() : std::move(default_value); }

    T unwrap_or(T default_value) && { return is_ok() ? std::move(*ok_ptr()) : std::move(default_value); }

    template <typename F>
    T unwrap_or_else(F&& fn) const& {
      return is_ok() ? *ok_ptr() : fn(*err_ptr());
    }

    template <typename F>
    T unwrap_or_else(F&& fn) && {
      return is_ok() ? std::move(*ok_ptr()) : fn(std::move(*err_ptr()));
    }

    // Expect (unwrap with custom message)
    T& expect(const char* msg) & {
      if (is_err()) {
        throw std::runtime_error(msg);
      }
      return *ok_ptr();
    }

    const T& expect(const char* msg) const& {
      if (is_err()) {
        throw std::runtime_error(msg);
      }
      return *ok_ptr();
    }

    T expect(const char* msg) && {
      if (is_err()) {
        throw std::runtime_error(msg);
      }
      return std::move(*ok_ptr());
    }

    // Map operations
    template <typename F>
    auto map(F&& fn) const& -> Result<decltype(fn(std::declval<T>())), E> {
      using U = decltype(fn(std::declval<T>()));
      if (is_ok()) {
        return Result<U, E>::Ok(fn(*ok_ptr()));
      }
      return Result<U, E>::Err(*err_ptr());
    }

    template <typename F>
    auto map(F&& fn) && -> Result<decltype(fn(std::declval<T>())), E> {
      using U = decltype(fn(std::declval<T>()));
      if (is_ok()) {
        return Result<U, E>::Ok(fn(std::move(*ok_ptr())));
      }
      return Result<U, E>::Err(std::move(*err_ptr()));
    }

    template <typename F>
    auto map_err(F&& fn) const& -> Result<T, decltype(fn(std::declval<E>()))> {
      using F2 = decltype(fn(std::declval<E>()));
      if (is_err()) {
        return Result<T, F2>::Err(fn(*err_ptr()));
      }
      return Result<T, F2>::Ok(*ok_ptr());
    }

    template <typename F>
    auto map_err(F&& fn) && -> Result<T, decltype(fn(std::declval<E>()))> {
      using F2 = decltype(fn(std::declval<E>()));
      if (is_err()) {
        return Result<T, F2>::Err(fn(std::move(*err_ptr())));
      }
      return Result<T, F2>::Ok(std::move(*ok_ptr()));
    }

    // and_then (flatMap/bind)
    template <typename F>
    auto and_then(F&& fn) const& -> decltype(fn(std::declval<T>())) {
      if (is_ok()) {
        return fn(*ok_ptr());
      }
      return decltype(fn(std::declval<T>()))::Err(*err_ptr());
    }

    template <typename F>
    auto and_then(F&& fn) && -> decltype(fn(std::declval<T>())) {
      if (is_ok()) {
        return fn(std::move(*ok_ptr()));
      }
      return decltype(fn(std::declval<T>()))::Err(std::move(*err_ptr()));
    }

    // Match-style visitor
    template <typename OkFn, typename ErrFn>
    auto match(OkFn&& ok_fn, ErrFn&& err_fn) const& {
      if (is_ok()) {
        return ok_fn(*ok_ptr());
      } else {
        return err_fn(*err_ptr());
      }
    }

    template <typename OkFn, typename ErrFn>
    auto match(OkFn&& ok_fn, ErrFn&& err_fn) && {
      if (is_ok()) {
        return ok_fn(std::move(*ok_ptr()));
      } else {
        return err_fn(std::move(*err_ptr()));
      }
    }

   private:
    Result() = default;
  };

  template <typename T, typename E>
  inline Result<T, E> OkOr(Option<T> opt, E err) {
    if (opt.is_some()) {
      return Ok(opt.unwrap());
    } else {
      return Err(err);
    }
  }


#define TRY_OK(expr)                                                                           \
  ({                                                                                           \
    auto _res = (expr);                                                                        \
    if (_res.is_err())                                                                         \
      return Err<std::remove_reference<decltype(_res.unwrap_err())>::type>(_res.unwrap_err()); \
    _res.unwrap();                                                                             \
  })

#define TRY_ABORT(expr)                                                       \
  ({                                                                          \
    auto _res = (expr);                                                       \
    if (_res.is_err()) {                                                      \
      ren::errln("Aborting due to error: {} ({})", _res.unwrap_err(), #expr); \
      std::abort();                                                           \
    }                                                                         \
    _res.unwrap();                                                            \
  })
}  // namespace ren