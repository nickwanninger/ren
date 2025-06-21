#pragma once
#include <ren/types.h>


namespace ren {
  // This is a class that provides a simple framerate counter by providing a delta time.
  // it tracks frame times and gives an average over the past 10 frames.
  // it uses a fixed sized array to store the frame times and implements it using
  // a circular buffer.

  constexpr int FRAMERATE_TRACKER_SIZE = 10;
  class FramerateCounter {
   private:
    float deltaTimes[FRAMERATE_TRACKER_SIZE];
    size_t head;
    size_t count;
    float sum;

   public:
    FramerateCounter()
        : head(0)
        , count(0)
        , sum(0.0f) {
      // Initialize buffer to zero
      for (size_t i = 0; i < FRAMERATE_TRACKER_SIZE; ++i) {
        deltaTimes[i] = 0.0f;
      }
    }

    void addFrame(float deltaTime) {
      // Remove old value from sum if buffer is full
      if (count == FRAMERATE_TRACKER_SIZE) {
        sum -= deltaTimes[head];
      } else {
        ++count;
      }

      // Add new value
      deltaTimes[head] = deltaTime;
      sum += deltaTime;

      // Advance head pointer
      head = (head + 1) % FRAMERATE_TRACKER_SIZE;
    }

    float getAverageDeltaTime() const { return count > 0 ? sum / count : 0.0f; }

    float getAverageFramerate() const {
      float avgDelta = getAverageDeltaTime();
      return avgDelta > 0.0f ? 1.0f / avgDelta : 0.0f;
    }

    size_t getFrameCount() const { return count; }

    bool isFull() const { return count == FRAMERATE_TRACKER_SIZE; }

    void reset() {
      head = 0;
      count = 0;
      sum = 0.0f;
      for (size_t i = 0; i < FRAMERATE_TRACKER_SIZE; ++i) {
        deltaTimes[i] = 0.0f;
      }
    }
  };

}  // namespace ren