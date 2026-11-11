// renctl: command-line tool for the asset store, in the style of git porcelain.
//
//   renctl [-C <dir>] <command> [<args>]
//
// Each command is a function that gets its options from a struct. Add a
// command with a new add*Command() function and call it from main().

#include <ren/assets/AssetManager.h>
#include <ren/core/Bundle.h>
#include <CLI11/CLI11.hpp>
#include <fstream>
#include <set>


struct GlobalOptions {
  std::string root = "assets";
};


// Creates the asset manager the same way the engine does. Returns nullptr if
// there is no store under the root: the asset manager creates <root>/store
// when it starts, and a read-only command must not do that.
static std::unique_ptr<ren::AssetManager> openAssets(const GlobalOptions &global) {
  std::error_code ec;
  if (!std::filesystem::is_directory(std::filesystem::path(global.root) / ren::ASSET_STORE_DIR, ec)) {
    fmt::print(stderr, "renctl: no store at '{}/{}'\n", global.root, ren::ASSET_STORE_DIR);
    return nullptr;
  }

  auto am = std::make_unique<ren::AssetManager>();
  am->addDefaultSources(global.root);
  return am;
}


// ---------------------------- verify ---------------------------- //

struct VerifyOptions {
  std::vector<std::string> keys;  // empty = all entries
  bool quiet = false;
};

static const char *describe(ren::AssetStore::VerifyResult result) {
  switch (result) {
    case ren::AssetStore::VerifyResult::Ok: return "ok";
    case ren::AssetStore::VerifyResult::BadKey: return "not a store key";
    case ren::AssetStore::VerifyResult::Missing: return "data.blob or meta.json missing";
    case ren::AssetStore::VerifyResult::Mismatch: return "contents do not match key";
  }
  return "unknown";
}

static int runVerify(const GlobalOptions &global, const VerifyOptions &opts) {
  auto am = openAssets(global);
  if (!am) {
    return 1;
  }
  auto &store = am->getStore();

  std::vector<std::string> keys = opts.keys;
  if (keys.empty()) {
    store.listKeys([&](std::string_view key) { keys.emplace_back(key); });
    std::sort(keys.begin(), keys.end());
  }

  size_t bad = 0;
  for (const auto &key : keys) {
    auto result = store.verify(key);
    if (result != ren::AssetStore::VerifyResult::Ok) {
      bad++;
      fmt::print("FAIL {}: {}\n", key, describe(result));
    } else if (!opts.quiet) {
      fmt::print("ok   {}\n", key);
    }
  }

  fmt::print("{} entries, {} failed\n", keys.size(), bad);
  return bad == 0 ? 0 : 1;
}

static void addVerifyCommand(CLI::App &app, const GlobalOptions &global, int &exitCode) {
  auto opts = std::make_shared<VerifyOptions>();
  auto *cmd = app.add_subcommand("verify", "Verify hashes of the store entries");
  cmd->add_option("keys", opts->keys, "Store keys to verify (default: all entries)");
  cmd->add_flag("-q,--quiet", opts->quiet, "Only print entries that fail");
  cmd->callback([&global, &exitCode, opts] { exitCode = runVerify(global, *opts); });
}


// -------------------------- viewbundle -------------------------- //

struct ViewBundleOptions {
  std::string target;  // a file path, or a store key
  bool noHeader = false;
};

// Reads the bundle file at `target`, or data.blob of the store entry `target`.
static bool readBundleFile(const GlobalOptions &global, const std::string &target, std::vector<u8> &out) {
  std::error_code ec;
  std::filesystem::path path = target;
  if (!std::filesystem::is_regular_file(path, ec)) {
    path = std::filesystem::path(global.root) / ren::ASSET_STORE_DIR / target / "data.blob";
  }

  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) {
    fmt::print(stderr, "renctl: '{}' is not a file or a store key\n", target);
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
  fmt::print("file: {} ({} bytes)\n", path.string(), out.size());
  return true;
}

// Parses the bundle layout by hand instead of with ren::Bundle, so that all
// problems are reported, not only the first one.
static int runViewBundle(const GlobalOptions &global, const ViewBundleOptions &opts) {
  std::vector<u8> data;
  if (!readBundleFile(global, opts.target, data)) {
    return 1;
  }

  size_t errors = 0, warnings = 0;
  auto error = [&](const std::string &msg) { errors++; fmt::print("ERROR {}\n", msg); };
  auto warn = [&](const std::string &msg) { warnings++; fmt::print("WARN  {}\n", msg); };

  if (data.size() < sizeof(u32)) {
    error("file is too small to contain the header size");
    return 1;
  }
  u32 jsonSize;
  std::memcpy(&jsonSize, data.data(), sizeof(u32));
  if (jsonSize > data.size() - sizeof(u32)) {
    error(fmt::format("header size {} is larger than the {} bytes after it", jsonSize, data.size() - sizeof(u32)));
    return 1;
  }

  json j;
  try {
    j = json::from_msgpack(data.begin() + sizeof(u32), data.begin() + sizeof(u32) + jsonSize);
  } catch (const json::exception &e) {
    error(fmt::format("header is not valid msgpack: {}", e.what()));
    return 1;
  }

  const u64 blobBase = sizeof(u32) + jsonSize;
  const u64 blobRegionSize = data.size() - blobBase;
  fmt::print("header: {} bytes, blob region: {} bytes at offset {}\n", jsonSize, blobRegionSize, blobBase);
  if (!opts.noHeader) {
    fmt::print("{}\n", j.dump(2));
  }

  if (!j.is_object()) {
    error("header is not an object");
    return 1;
  }

  if (!j.contains("version") || !j["version"].is_number_unsigned()) {
    error("version is missing or is not an unsigned integer");
  } else if (j["version"].get<u32>() != ren::Bundle::VERSION) {
    error(fmt::format("version is {}, expected {}", j["version"].get<u32>(), ren::Bundle::VERSION));
  }
  if (!j.contains("name") || !j["name"].is_string()) {
    error("name is missing or is not a string");
  }
  if (!j.contains("metadata")) {
    error("metadata is missing");
  }
  if (!j.contains("blobs") || !j["blobs"].is_array()) {
    error("blobs is missing or is not an array");
    fmt::print("{} errors, {} warnings\n", errors, warnings);
    return 1;
  }

  struct Range {
    size_t index;
    std::string name;
    u64 offset, size;
  };
  std::vector<Range> ranges;
  std::set<std::string> names;

  fmt::print("\n{:>4}  {:>12}  {:>12}  {}\n", "#", "offset", "size", "name");
  const auto &blobs = j["blobs"];
  for (size_t i = 0; i < blobs.size(); i++) {
    const auto &jb = blobs[i];
    if (!jb.is_object() || !jb.contains("name") || !jb["name"].is_string() || !jb.contains("offset") ||
        !jb["offset"].is_number_unsigned() || !jb.contains("size") || !jb["size"].is_number_unsigned()) {
      error(fmt::format("blob {} must be an object with a string name and unsigned offset and size: {}", i, jb.dump()));
      continue;
    }
    Range r{i, jb["name"].get<std::string>(), jb["offset"].get<u64>(), jb["size"].get<u64>()};
    fmt::print("{:>4}  {:>12}  {:>12}  {}\n", i, r.offset, r.size, r.name);

    if (!names.insert(r.name).second) {
      warn(fmt::format("blob {} '{}': name is used by an earlier blob", i, r.name));
    }
    if (r.offset > blobRegionSize || r.size > blobRegionSize - r.offset) {
      error(fmt::format("blob {} '{}': range [{}, {}) is outside the blob region ({} bytes)", i, r.name, r.offset,
                        r.offset + r.size, blobRegionSize));
      continue;
    }
    ranges.push_back(std::move(r));
  }

  // serialize() packs blobs end to end in attach order, so overlaps are
  // errors, and gaps, reordering, and trailing bytes are warnings.
  std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) { return a.offset < b.offset; });
  u64 end = 0;
  for (size_t k = 0; k < ranges.size(); k++) {
    const auto &r = ranges[k];
    if (k > 0 && r.index < ranges[k - 1].index) {
      warn(fmt::format("blob {} '{}': offset order is not attach order", r.index, r.name));
    }
    if (r.offset < end) {
      error(fmt::format("blob {} '{}': overlaps the previous blob by {} bytes", r.index, r.name, end - r.offset));
    } else if (r.offset > end) {
      warn(fmt::format("blob {} '{}': {} unused bytes before it", r.index, r.name, r.offset - end));
    }
    end = std::max(end, r.offset + r.size);
  }
  if (end < blobRegionSize) {
    warn(fmt::format("{} unused bytes after the last blob", blobRegionSize - end));
  }

  fmt::print("{} errors, {} warnings\n", errors, warnings);
  return errors == 0 ? 0 : 1;
}

static void addViewBundleCommand(CLI::App &app, const GlobalOptions &global, int &exitCode) {
  auto opts = std::make_shared<ViewBundleOptions>();
  auto *cmd = app.add_subcommand("viewbundle", "Print the header of a bundle and validate its blob layout");
  cmd->add_option("target", opts->target, "Bundle file, or store key of a bundle entry")->required();
  cmd->add_flag("--no-header", opts->noHeader, "Do not print the JSON header");
  cmd->callback([&global, &exitCode, opts] { exitCode = runViewBundle(global, *opts); });
}


// ----------------------------- main ----------------------------- //

int main(int argc, char **argv) {
  CLI::App app{"renctl: manage a ren asset store"};
  app.require_subcommand(1);

  GlobalOptions global;
  app.add_option("-C,--root", global.root, "Asset directory that contains the store")->capture_default_str();

  int exitCode = 0;
  addVerifyCommand(app, global, exitCode);
  addViewBundleCommand(app, global, exitCode);

  CLI11_PARSE(app, argc, argv);
  return exitCode;
}
