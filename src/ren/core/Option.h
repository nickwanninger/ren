#pragma once

#include <optional>
#include <utility>
#include <stdexcept>

namespace ren {

  template <typename T>
  struct Some {
    T value;
    explicit Some(T v)
        : value(std::move(v)) {}
  };

  struct None_t {};
  inline constexpr None_t None{};



  template <typename T>
  class Option {
   private:
    std::optional<T> m_data;

    Option() = default;

   public:
    // Constructors
    static Option Some(T value) { return Option(ren::Some<T>(std::move(value))); }
    static Option None() { return Option(ren::None); }

    Option(ren::Some<T> some)
        : m_data(std::move(some.value)) {}
    Option(None_t)
        : m_data(std::nullopt) {}


    // We also allow implicit conversion from/to std::optional
    Option(std::optional<T> opt)
        : m_data(std::move(opt)) {}
    operator std::optional<T>() const { return m_data; }

    // Allow conversion from some Option<U> to Option<T> if U is convertible to T
    template <typename U>
      requires std::convertible_to<U, T>
    Option(const Option<U>& other) {
      if (other.is_some()) {
        m_data = static_cast<T>(other.unwrap());
      } else {
        m_data = std::nullopt;
      }
    }

    // Query methods
    bool is_some() const { return m_data.has_value(); }
    bool is_none() const { return !m_data.has_value(); }
    explicit operator bool() const { return is_some(); }

    // Access methods (throw if None)
    T& unwrap() & {
      if (is_none()) {
        throw std::runtime_error("Called unwrap() on None value");
      }
      return m_data.value();
    }

    const T& unwrap() const& {
      if (is_none()) {
        throw std::runtime_error("Called unwrap() on None value");
      }
      return m_data.value();
    }

    T unwrap() && {
      if (is_none()) {
        throw std::runtime_error("Called unwrap() on None value");
      }
      return std::move(m_data.value());
    }

    // Safe access with default
    T unwrap_or(T default_value) const& { return is_some() ? m_data.value() : std::move(default_value); }

    T unwrap_or(T default_value) && { return is_some() ? std::move(m_data.value()) : std::move(default_value); }

    template <typename F>
    T unwrap_or_else(F&& fn) const& {
      return is_some() ? m_data.value() : fn();
    }

    template <typename F>
    T unwrap_or_else(F&& fn) && {
      return is_some() ? std::move(m_data.value()) : fn();
    }

    // Expect (unwrap with custom message)
    T& expect(const char* msg) & {
      if (is_none()) {
        throw std::runtime_error(msg);
      }
      return m_data.value();
    }

    const T& expect(const char* msg) const& {
      if (is_none()) {
        throw std::runtime_error(msg);
      }
      return m_data.value();
    }

    T expect(const char* msg) && {
      if (is_none()) {
        throw std::runtime_error(msg);
      }
      return std::move(m_data.value());
    }

    // Map operations
    template <typename F>
    auto map(F&& fn) const& -> Option<decltype(fn(std::declval<T>()))> {
      using U = decltype(fn(std::declval<T>()));
      if (is_some()) {
        return Option<U>::Some(fn(m_data.value()));
      }
      return Option<U>::None();
    }

    template <typename F>
    auto map(F&& fn) && -> Option<decltype(fn(std::declval<T>()))> {
      using U = decltype(fn(std::declval<T>()));
      if (is_some()) {
        return Option<U>::Some(fn(std::move(m_data.value())));
      }
      return Option<U>::None();
    }

    // and_then (flatMap/bind)
    template <typename F>
    auto and_then(F&& fn) const& -> decltype(fn(std::declval<T>())) {
      if (is_some()) {
        return fn(m_data.value());
      }
      return decltype(fn(std::declval<T>()))::None();
    }

    template <typename F>
    auto and_then(F&& fn) && -> decltype(fn(std::declval<T>())) {
      if (is_some()) {
        return fn(std::move(m_data.value()));
      }
      return decltype(fn(std::declval<T>()))::None();
    }

    // Match-style visitor
    template <typename SomeFn, typename NoneFn>
    auto match(SomeFn&& some_fn, NoneFn&& none_fn) const& {
      if (is_some()) {
        return some_fn(m_data.value());
      } else {
        return none_fn();
      }
    }

    template <typename SomeFn, typename NoneFn>
    auto match(SomeFn&& some_fn, NoneFn&& none_fn) && {
      if (is_some()) {
        return some_fn(std::move(m_data.value()));
      } else {
        return none_fn();
      }
    }

    // filter
    template <typename Pred>
    Option filter(Pred&& predicate) const& {
      if (is_some() && predicate(m_data.value())) {
        return Option::Some(m_data.value());
      }
      return Option::None();
    }

    template <typename Pred>
    Option filter(Pred&& predicate) && {
      if (is_some() && predicate(m_data.value())) {
        return Option::Some(std::move(m_data.value()));
      }
      return Option::None();
    }
  };


#define TRY_SOME(expr)  \
  ({                    \
    auto _opt = (expr); \
    if (_opt.is_none()) \
      return ren::None; \
    _opt.unwrap();      \
  })

}  // namespace ren