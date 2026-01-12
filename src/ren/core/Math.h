#pragma once

#include <ren/types.h>

namespace ren {


  template <typename T>
  inline T lerp(const T& a, const T& b, float t) {
    return a + (b - a) * t;
  }


  template <typename T>
  class AdaptiveIntegrator {
    T value_ = T(0);
    T shortTermMean_ = T(0);
    T shortTermVariance_ = T(0);
    float k_ = 0.1f;
    float baseK_ = 0.1f;
    float minK_ = 0.01f;
    float maxK_ = 0.5f;
    float varianceScale_ = 0.1f;
    float distanceScale_ = 0.05f;
    bool initialized_ = false;

   public:
    explicit AdaptiveIntegrator(float baseK = 0.1f, float minK = 0.01f,
                                float maxK = 0.5f)
      : k_(baseK), baseK_(baseK), minK_(minK), maxK_(maxK) {}

    T update(T sample) {
      if (!initialized_) {
        value_ = sample;
        shortTermMean_ = sample;
        shortTermVariance_ = T(0);
        initialized_ = true;
        return value_;
      }

      // Update short-term statistics using exponential moving average
      constexpr float statsK = 0.2f;
      T delta = sample - shortTermMean_;
      shortTermMean_ = lerp(shortTermMean_, sample, statsK);
      shortTermVariance_ = lerp(shortTermVariance_, delta * delta, statsK);

      // Compute distance between current integrated value and short-term mean
      T distance = value_ - shortTermMean_;
      T stdDev = glm::sqrt(shortTermVariance_);

      // Adapt k: reduce if variance is high, increase if far from short-term mean
      float kAdjust = distanceScale_ * glm::abs(distance) - varianceScale_ * stdDev;
      k_ = glm::clamp(baseK_ + kAdjust, minK_, maxK_);

      // Update integrated value using adaptive k
      value_ = lerp(value_, sample, k_);

      return value_;
    }

    T getValue() const { return value_; }
    float getK() const { return k_; }
    T getVariance() const { return shortTermVariance_; }
    T getMean() const { return shortTermMean_; }
  };

}  // namespace ren