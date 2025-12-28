#pragma once


namespace ren {

  template <typename S, typename T>
  class Watcher : public RefCounted<Watcher<S, T>> {
   public:
    using State = S;
    using Event = T;

    virtual ~Watcher() = default;

    virtual void onEvent(State &s, Event &e) = 0;
  };



  template <typename S, typename T>
  class LambdaWatcher : public Watcher<S, T> {
   public:
    using Callback = std::function<void(S &, T &)>;

    LambdaWatcher(Callback cb)
        : callback(cb) {}

    void onEvent(S &s, T &e) override { callback(s, e); }

   private:
    Callback callback;
  };

  // A notifier is something that watchers can watch and be notified of events.
  template <typename S, typename T>
  class Notifier : public RefCounted<Notifier<S, T>> {
   public:
    using WatcherType = Watcher<S, T>;
    void addWatcher(weak_ref<WatcherType> watcher) { watchers.push_back(std::move(watcher)); }

    void removeWatcher(weak_ref<WatcherType> watcher) {
      watchers.erase(std::remove_if(watchers.begin(), watchers.end(),
                                    [&](const weak_ref<WatcherType> &w) {
                                      return w.lock() == watcher.lock();
                                    }),
                     watchers.end());
    }

    void notifyWatchers(S &state, T &event) {
      for (auto it = watchers.begin(); it != watchers.end();) {
        if (auto watcher = it->lock()) {
          watcher->onEvent(state, event);
          ++it;
        } else {
          // Remove expired watcher
          it = watchers.erase(it);
        }
      }
    }

   private:
    std::vector<weak_ref<WatcherType>> watchers;
  };
}  // namespace ren