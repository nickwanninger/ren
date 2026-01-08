#pragma once

#include <variant>
#include <utility>
#include <stdexcept>
#include <optional>

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
    std::variant<T, E> m_data;

   public:
    // Constructors
    static Result Ok(T value) { return Result(ren::Ok<T>(std::move(value))); }

    static Result Err(E error) { return Result(ren::Err<E>(std::move(error))); }


    Result(ren::Ok<T> ok)
        : m_data(std::in_place_index<0>, std::move(ok.value)) {}
    Result(ren::Err<E> err)
        : m_data(std::in_place_index<1>, std::move(err.value)) {}

    // Query methods
    bool is_ok() const { return m_data.index() == 0; }
    bool is_err() const { return m_data.index() == 1; }
    explicit operator bool() const { return is_ok(); }


    std::optional<T> ok() const { return is_ok() ? std::optional<T>(std::get<0>(m_data)) : std::nullopt; }
    std::optional<E> err() const { return is_err() ? std::optional<E>(std::get<1>(m_data)) : std::nullopt; }



    // Access methods (throw if wrong variant)
    T& unwrap() & {
      if (is_err()) {
        throw std::runtime_error("Called unwrap() on Err value");
      }
      return std::get<0>(m_data);
    }

    const T& unwrap() const& {
      if (is_err()) {
        throw std::runtime_error("Called unwrap() on Err value");
      }
      return std::get<0>(m_data);
    }

    T unwrap() && {
      if (is_err()) {
        throw std::runtime_error("Called unwrap() on Err value");
      }
      return std::move(std::get<0>(m_data));
    }

    E& unwrap_err() & {
      if (is_ok()) {
        throw std::runtime_error("Called unwrap_err() on Ok value");
      }
      return std::get<1>(m_data);
    }

    const E& unwrap_err() const& {
      if (is_ok()) {
        throw std::runtime_error("Called unwrap_err() on Ok value");
      }
      return std::get<1>(m_data);
    }

    E unwrap_err() && {
      if (is_ok()) {
        throw std::runtime_error("Called unwrap_err() on Ok value");
      }
      return std::move(std::get<1>(m_data));
    }

    // Safe access with default
    T unwrap_or(T default_value) const& { return is_ok() ? std::get<0>(m_data) : std::move(default_value); }

    T unwrap_or(T default_value) && { return is_ok() ? std::move(std::get<0>(m_data)) : std::move(default_value); }

    template <typename F>
    T unwrap_or_else(F&& fn) const& {
      return is_ok() ? std::get<0>(m_data) : fn(std::get<1>(m_data));
    }

    template <typename F>
    T unwrap_or_else(F&& fn) && {
      return is_ok() ? std::move(std::get<0>(m_data)) : fn(std::move(std::get<1>(m_data)));
    }

    // Expect (unwrap with custom message)
    T& expect(const char* msg) & {
      if (is_err()) {
        throw std::runtime_error(msg);
      }
      return std::get<0>(m_data);
    }

    const T& expect(const char* msg) const& {
      if (is_err()) {
        throw std::runtime_error(msg);
      }
      return std::get<0>(m_data);
    }

    T expect(const char* msg) && {
      if (is_err()) {
        throw std::runtime_error(msg);
      }
      return std::move(std::get<0>(m_data));
    }

    // Map operations
    template <typename F>
    auto map(F&& fn) const& -> Result<decltype(fn(std::declval<T>())), E> {
      using U = decltype(fn(std::declval<T>()));
      if (is_ok()) {
        return Result<U, E>::Ok(fn(std::get<0>(m_data)));
      }
      return Result<U, E>::Err(std::get<1>(m_data));
    }

    template <typename F>
    auto map(F&& fn) && -> Result<decltype(fn(std::declval<T>())), E> {
      using U = decltype(fn(std::declval<T>()));
      if (is_ok()) {
        return Result<U, E>::Ok(fn(std::move(std::get<0>(m_data))));
      }
      return Result<U, E>::Err(std::move(std::get<1>(m_data)));
    }

    template <typename F>
    auto map_err(F&& fn) const& -> Result<T, decltype(fn(std::declval<E>()))> {
      using F2 = decltype(fn(std::declval<E>()));
      if (is_err()) {
        return Result<T, F2>::Err(fn(std::get<1>(m_data)));
      }
      return Result<T, F2>::Ok(std::get<0>(m_data));
    }

    template <typename F>
    auto map_err(F&& fn) && -> Result<T, decltype(fn(std::declval<E>()))> {
      using F2 = decltype(fn(std::declval<E>()));
      if (is_err()) {
        return Result<T, F2>::Err(fn(std::move(std::get<1>(m_data))));
      }
      return Result<T, F2>::Ok(std::move(std::get<0>(m_data)));
    }

    // and_then (flatMap/bind)
    template <typename F>
    auto and_then(F&& fn) const& -> decltype(fn(std::declval<T>())) {
      if (is_ok()) {
        return fn(std::get<0>(m_data));
      }
      return decltype(fn(std::declval<T>()))::Err(std::get<1>(m_data));
    }

    template <typename F>
    auto and_then(F&& fn) && -> decltype(fn(std::declval<T>())) {
      if (is_ok()) {
        return fn(std::move(std::get<0>(m_data)));
      }
      return decltype(fn(std::declval<T>()))::Err(std::move(std::get<1>(m_data)));
    }

    // Match-style visitor
    template <typename OkFn, typename ErrFn>
    auto match(OkFn&& ok_fn, ErrFn&& err_fn) const& {
      if (is_ok()) {
        return ok_fn(std::get<0>(m_data));
      } else {
        return err_fn(std::get<1>(m_data));
      }
    }

    template <typename OkFn, typename ErrFn>
    auto match(OkFn&& ok_fn, ErrFn&& err_fn) && {
      if (is_ok()) {
        return ok_fn(std::move(std::get<0>(m_data)));
      } else {
        return err_fn(std::move(std::get<1>(m_data)));
      }
    }

   private:
    Result() = default;
  };


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