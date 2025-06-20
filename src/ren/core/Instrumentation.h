#pragma once


#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <string>
#include <thread>
#include <mutex>
#include <sstream>

#define REN_PROFILE

namespace ren {



  using FloatingPointMicroseconds = std::chrono::duration<double, std::micro>;

  struct ProfileResult {
    std::string Name;

    FloatingPointMicroseconds Start;
    std::chrono::microseconds ElapsedTime;
    std::thread::id ThreadID;
    char ph = 'X';
  };

  struct InstrumentationSession {
    std::string Name;
  };

  class Instrumentor {
   public:
    uint64_t profileEvents = 0;
    uint64_t profileBytes = 0;
    Instrumentor(const Instrumentor&) = delete;
    Instrumentor(Instrumentor&&) = delete;

    void BeginSession(const std::string& name, const std::string& filepath = "results.json") {
      std::lock_guard lock(m_Mutex);
      if (m_CurrentSession) {
        // If there is already a current session, then close it before beginning new one.
        // Subsequent profiling output meant for the original session will end up in the
        // newly opened session instead.  That's better than having badly formatted
        // profiling output.
        InternalEndSession();
      }
      m_OutputStream.open(filepath);

      if (m_OutputStream.is_open()) {
        m_CurrentSession = new InstrumentationSession({name});
        WriteHeader();
      }
    }

    void EndSession() {
      std::lock_guard lock(m_Mutex);
      InternalEndSession();
    }

    template <typename T>
    inline void writeCounter(const char* name, T value) {
      if (!m_OutputEnabled) return;

      std::stringstream json;
      json << std::setprecision(3) << std::fixed;
      json << ",{";
      json << "\"name\":\"" << name << "\",";
      json << "\"ph\":\"C\",";
      json << "\"pid\":\"0\",";
      json << "\"tid\":\"" << std::this_thread::get_id() << "\",";
      json
          << "\"ts\":"
          << FloatingPointMicroseconds{std::chrono::steady_clock::now().time_since_epoch()}.count();
      json << ",\"args\":{\"value\":" << value << "}";
      json << "}";

      writeEvent(json.str());
    }


    inline void writeEvent(const std::string& event) {
      profileBytes += event.size();
      profileEvents++;
      m_OutputStream << event;
    }

    inline void WriteProfile(const ProfileResult& result) {
      if (!m_OutputEnabled) return;

      std::lock_guard lock(m_Mutex);
      if (m_CurrentSession) {
        std::stringstream json;

        json << std::setprecision(3) << std::fixed;
        json << ",{";
        json << "\"cat\":\"function\",";
        json << "\"dur\":" << (result.ElapsedTime.count()) << ',';
        json << "\"name\":\"" << result.Name << "\",";
        json << "\"ph\":\"X\",";
        json << "\"pid\":\"0\",";
        json << "\"tid\":\"" << result.ThreadID << "\",";
        json << "\"ts\":" << result.Start.count();
        json << "}";

        writeEvent(json.str());
      }
    }

    void enableOutput(bool enable) {
      std::lock_guard lock(m_Mutex);
      m_OutputEnabled = enable;
    }

    inline bool isOutputEnabled() const { return m_OutputEnabled; }

    static Instrumentor& Get() {
      static Instrumentor instance;
      return instance;
    }


   private:
    Instrumentor()
        : m_CurrentSession(nullptr) {}

    ~Instrumentor() { EndSession(); }

    void WriteHeader() {
      m_OutputStream << "{\"otherData\": {}, \"traceEvents\":[{}";
      m_OutputStream.flush();
    }

    void WriteFooter() {
      m_OutputStream << "]}";
      m_OutputStream.flush();
    }

    // Note: you must already own lock on m_Mutex before
    // calling InternalEndSession()
    void InternalEndSession() {
      if (m_CurrentSession) {
        WriteFooter();
        m_OutputStream.close();
        delete m_CurrentSession;
        m_CurrentSession = nullptr;
      }
    }

   private:
    std::mutex m_Mutex;
    InstrumentationSession* m_CurrentSession;
    std::ofstream m_OutputStream;
    bool m_OutputEnabled = true;
  };

  class InstrumentationTimer {
   public:
    InstrumentationTimer(const char* name)
        : m_Name(name)
        , m_Stopped(false) {
      m_StartTimepoint = std::chrono::steady_clock::now();
    }

    ~InstrumentationTimer() {
      if (!m_Stopped) Stop();
    }

    void Stop() {
      auto endTimepoint = std::chrono::steady_clock::now();
      auto highResStart = FloatingPointMicroseconds{m_StartTimepoint.time_since_epoch()};
      auto elapsedTime =
          std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch() -
          std::chrono::time_point_cast<std::chrono::microseconds>(m_StartTimepoint)
              .time_since_epoch();

      Instrumentor::Get().WriteProfile(
          {m_Name, highResStart, elapsedTime, std::this_thread::get_id(), 'X'});

      m_Stopped = true;
    }

   private:
    const char* m_Name;
    std::chrono::time_point<std::chrono::steady_clock> m_StartTimepoint;
    bool m_Stopped;
  };


  static inline void emitInstrumentationMark(const char* name) {
    Instrumentor::Get().WriteProfile(
        {name, FloatingPointMicroseconds{std::chrono::steady_clock::now().time_since_epoch()},
         std::chrono::microseconds(0), std::this_thread::get_id(), 'M'});
  }

  namespace InstrumentorUtils {

    template <size_t N>
    struct ChangeResult {
      char Data[N];
    };

    template <size_t N, size_t K>
    constexpr auto CleanupOutputString(const char (&expr)[N], const char (&remove)[K]) {
      ChangeResult<N> result = {};

      size_t srcIndex = 0;
      size_t dstIndex = 0;
      while (srcIndex < N) {
        size_t matchIndex = 0;
        while (matchIndex < K - 1 && srcIndex + matchIndex < N - 1 &&
               expr[srcIndex + matchIndex] == remove[matchIndex])
          matchIndex++;
        if (matchIndex == K - 1) srcIndex += matchIndex;
        result.Data[dstIndex++] = expr[srcIndex] == '"' ? '\'' : expr[srcIndex];
        srcIndex++;
      }
      return result;
    }
  }  // namespace InstrumentorUtils



}  // namespace ren

#ifdef REN_PROFILE
   // Resolve which function signature macro will be used. Note that this only
  // is resolved when the (pre)compiler starts, so the syntax highlighting
  // could mark the wrong one in your editor!
  #if defined(__GNUC__) || (defined(__MWERKS__) && (__MWERKS__ >= 0x3000)) || \
      (defined(__ICC) && (__ICC >= 600)) || defined(__ghs__)
    #define REN_FUNC_SIG __PRETTY_FUNCTION__
  #elif defined(__DMC__) && (__DMC__ >= 0x810)
    #define REN_FUNC_SIG __PRETTY_FUNCTION__
  #elif (defined(__FUNCSIG__) || (_MSC_VER))
    #define REN_FUNC_SIG __FUNCSIG__
  #elif (defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 600)) || \
      (defined(__IBMCPP__) && (__IBMCPP__ >= 500))
    #define REN_FUNC_SIG __FUNCTION__
  #elif defined(__BORLANDC__) && (__BORLANDC__ >= 0x550)
    #define REN_FUNC_SIG __FUNC__
  #elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901)
    #define REN_FUNC_SIG __func__
  #elif defined(__cplusplus) && (__cplusplus >= 201103)
    #define REN_FUNC_SIG __func__
  #else
    #define REN_FUNC_SIG "REN_FUNC_SIG unknown!"
  #endif

  #define REN_PROFILE_BEGIN_SESSION(name, filepath) \
    ::ren::Instrumentor::Get().BeginSession(name, filepath)
  #define REN_PROFILE_END_SESSION() ::ren::Instrumentor::Get().EndSession()
  #define REN_PROFILE_SCOPE_LINE2(name, line)                            \
    constexpr auto fixedName##line =                                     \
        ::ren::InstrumentorUtils::CleanupOutputString(name, "__cdecl "); \
    ::ren::InstrumentationTimer timer##line(fixedName##line.Data)
  #define REN_PROFILE_SCOPE_LINE(name, line) REN_PROFILE_SCOPE_LINE2(name, line)
  #define REN_PROFILE_SCOPE(name) REN_PROFILE_SCOPE_LINE(name, __LINE__)
  #define REN_PROFILE_FUNCTION() REN_PROFILE_SCOPE(REN_FUNC_SIG)
  #define REN_PROFILE_MARK(name) ::ren::emitInstrumentationMark(name)
  #define REN_PROFILE_OUTPUT(enable) ::ren::Instrumentor::Get().enableOutput(enable)
  #define REN_PROFILE_OUTPUT_ENABLED() ::ren::Instrumentor::Get().isOutputEnabled()
  #define REN_PROFILE_COUNTER(name, value) ::ren::Instrumentor::Get().writeCounter(name, value)
#else
  #define REN_PROFILE_BEGIN_SESSION(name, filepath)
  #define REN_PROFILE_END_SESSION()
  #define REN_PROFILE_SCOPE(name)
  #define REN_PROFILE_FUNCTION()
  #define REN_PROFILE_MARK(name)
  #define REN_PROFILE_OUTPUT(enable)
  #define REN_PROFILE_OUTPUT_ENABLED() (false)
  #define REN_PROFILE_COUNTER(name, value)
#endif