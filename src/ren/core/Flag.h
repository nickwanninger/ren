#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <fmt/core.h>

namespace ren {

  class FlagBase;  // Forward declaration
  // Global registry of all flags for help and parsing
  class FlagRegistry {
   public:
    friend class FlagBase;
    friend void ParseFlags(int argc, char *argv[]);

    static std::unordered_map<std::string, FlagBase *> &getRegistry() {
      static std::unordered_map<std::string, FlagBase *> registry;
      return registry;
    }

    static void registerFlag(const std::string &name, FlagBase *flag) {
      getRegistry()[name] = flag;
    }

   public:
    static inline void printHelp();
  };

  // Base class for type-erased flag operations
  class FlagBase {
   protected:
    std::string name;
    std::string description;

   public:
    bool isBool = false;
    FlagBase(const std::string &flagName, const std::string &desc = "")
        : name(flagName)
        , description(desc) {
      FlagRegistry::registerFlag(flagName, this);
    }

    virtual ~FlagBase() = default;

    virtual bool parseValue(const std::string &value) = 0;
    virtual void printHelp() const = 0;

    const std::string &getName() const { return name; }
  };



  void FlagRegistry::printHelp() {
    std::cout << "Usage: [options]\n\n";
    std::cout << "Options:\n";
    for (const auto &[name, flag] : getRegistry()) {
      flag->printHelp();
    }
    std::cout << "  --help                           Print this help message\n";
  }


  // Template specialization for different types
  template <typename T>
  class Flag;

  // Specialization for bool
  template <>
  class Flag<bool> : public FlagBase {
   private:
    bool value;
    bool defaultValue;

   public:
    Flag(const std::string &name, bool defaultVal, const std::string &desc = "")
        : FlagBase(name, desc)
        , value(defaultVal)
        , defaultValue(defaultVal) {
      isBool = true;
    }

    bool parseValue(const std::string &value) override {
      if (value == "1" || value == "true" || value == "") {
        this->value = true;
        return true;
      }
      if (value == "0" || value == "false") {
        this->value = false;
        return true;
      }
      return false;
    }

    void printHelp() const override {
      std::string defaultStr = defaultValue ? "true" : "false";
      if (!description.empty()) {
        fmt::print("  --{:<30} {:<40} (default: {})\n", name, description, defaultStr);
      } else {
        fmt::print("  --{:<30} (default: {})\n", name, defaultStr);
      }
      fmt::print("  --no-{:<27} Toggle {} off\n", name, name);
    }

    operator bool() const { return value; }
    bool operator*() const { return value; }
    bool get() const { return value; }

    void set(bool v) { value = v; }
  };

  // Specialization for int
  template <>
  class Flag<int> : public FlagBase {
   private:
    int value;
    int defaultValue;

   public:
    Flag(const std::string &name, int defaultVal, const std::string &desc = "")
        : FlagBase(name, desc)
        , value(defaultVal)
        , defaultValue(defaultVal) {}

    bool parseValue(const std::string &value) override {
      try {
        this->value = std::stoi(value);
        return true;
      } catch (...) { return false; }
    }

    void printHelp() const override {
      if (!description.empty()) {
        fmt::print("  --{:<30} {:<40} (default: {})\n", name, description, defaultValue);
      } else {
        fmt::print("  --{:<30} <value>           (default: {})\n", name, defaultValue);
      }
    }

    operator int() const { return value; }
    int operator*() const { return value; }
    int get() const { return value; }

    void set(int v) { value = v; }
  };

  template <>
  class Flag<float> : public FlagBase {
   private:
    float value;
    float defaultValue;

   public:
    Flag(const std::string &name, float defaultVal, const std::string &desc = "")
        : FlagBase(name, desc)
        , value(defaultVal)
        , defaultValue(defaultVal) {}

    bool parseValue(const std::string &value) override {
      try {
        this->value = std::stof(value);
        fmt::println("Parsed float flag {} = {}", name, this->value);
        return true;
      } catch (...) { return false; }
    }

    void printHelp() const override {
      if (!description.empty()) {
        fmt::print("  --{:<30} {:<40} (default: {})\n", name, description, defaultValue);
      } else {
        fmt::print("  --{:<30} <value>           (default: {})\n", name, defaultValue);
      }
    }

    operator float() const { return value; }
    float operator*() const { return value; }
    float get() const { return value; }

    void set(float v) { value = v; }
  };

  // Specialization for std::string
  template <>
  class Flag<std::string> : public FlagBase {
   private:
    std::string value;
    std::string defaultValue;

   public:
    Flag(const std::string &name, const std::string &defaultVal, const std::string &desc = "")
        : FlagBase(name, desc)
        , value(defaultVal)
        , defaultValue(defaultVal) {}

    bool parseValue(const std::string &value) override {
      this->value = value;
      return true;
    }

    void printHelp() const override {
      if (!description.empty()) {
        fmt::print("  --{:<30} {:<40} (default: \"{}\")\n", name, description, defaultValue);
      } else {
        fmt::print("  --{:<30} <value>           (default: \"{}\")\n", name, defaultValue);
      }
    }

    operator const std::string &() const { return value; }
    const std::string &operator*() const { return value; }
    const std::string &get() const { return value; }

    void set(const std::string &v) { value = v; }
  };

  // Parse command line arguments
  inline void parseFlags(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];

      if (arg == "--help" || arg == "-h") {
        FlagRegistry::printHelp();
        exit(0);
      }

      // Handle boolean flags like --no-vsync
      if (arg.substr(0, 5) == "--no-") {
        std::string flagName = arg.substr(5);
        auto &registry = FlagRegistry::getRegistry();
        auto it = registry.find(flagName);
        if (it != registry.end() && it->second->isBool) {
          Flag<bool> *boolFlag = dynamic_cast<Flag<bool> *>(it->second);
          boolFlag->set(false);
        } else {
          fmt::print(stderr, "Error: Unknown flag '{}'\n", arg);
          exit(1);
        }
        continue;
      }

      // Handle regular flags
      if (arg[0] == '-' && arg[1] == '-') {
        std::string flagName = arg.substr(2);
        auto &registry = FlagRegistry::getRegistry();
        auto it = registry.find(flagName);

        if (it == registry.end()) {
          fmt::print(stderr, "Error: Unknown flag '{}'\n", arg);
          exit(1);
        }

        FlagBase *flag = it->second;

        // For boolean flags, just set to true if not using --no- prefix
        if (flag->isBool) {
          auto *boolFlag = dynamic_cast<Flag<bool> *>(flag);
          boolFlag->set(true);
        } else {
          // For other types, expect a value as next argument
          if (i + 1 >= argc) {
            fmt::print(stderr, "Error: Flag '{}' requires a value\n", arg);
            exit(1);
          }
          ++i;
          std::string value = argv[i];
          if (!flag->parseValue(value)) {
            fmt::print(stderr, "Error: Invalid value '{}' for flag '{}'\n", value, arg);
            exit(1);
          }
        }
      } else {
        fmt::print(stderr, "Error: Unexpected positional argument '{}'\n", arg);
        exit(1);
      }
    }
  }

}  // namespace ren
