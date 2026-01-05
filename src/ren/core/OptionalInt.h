#pragma once


#include "ren/misc/json_serialize.h"
namespace ren {


  template <typename T, T NoneValue = T(-1)>
  class OptionalInt {
   public:
    OptionalInt()
        : m_value(NoneValue) {}
    OptionalInt(T value)
        : m_value(value) {}

    bool hasValue() const { return m_value != NoneValue; }
    operator bool() const { return hasValue(); }



    T operator*() const { return m_value; }
    T value_or(T defaultValue) const { return hasValue() ? m_value : defaultValue; }




    json toJson() const {
      return hasValue() ? json(m_value) : json(nullptr);
    }

    bool fromJson(const json& j) {
      if (j.is_null()) {
        m_value = NoneValue;
        return true;
      }
      m_value = j.get<T>();
      return true;
    }

   private:
    T m_value;
  };

}  // namespace ren