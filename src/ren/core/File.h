#pragma once

#include <ren/types.h>
#include <ren/core/Watcher.h>


namespace ren {


  struct IoRequest {
    u8 *buffer;
    size_t size;
    size_t offset;
  };


  class File;  // Forward Decl.
  enum class FileWatcherEvent { Modified, Deleted };


  using FileWatcher = Watcher<FileWatcherEvent, File>;

  // A File is just a thing that can have bytes read from it, and can maybe have
  // bytes written to it. Note that this interface does not actually require the
  // file is on disk, and as such it could be in memory.
  class File {
   public:
    virtual ~File() = default;

    /**
     * @brief Read bytes from the file.
     */
    virtual bool read(const IoRequest &req) { return false; }
    /**
     * @brief Write bytes to the file.
     */
    virtual bool write(const IoRequest &req) { return false; }
    /**
     * @brief Quickly get the size of the file in bytes
     */
    virtual size_t getSize() = 0;

    /**
     * @brief Add a watcher to this file that will be notified when the file changes.
     *
     * When the file changes, the onFileChanged method of the watcher will be
     * called, if the file implementation supports it under the hood.  Note that
     * we use weak_ref here to avoid the file watching itself being kept alive
     * solely by being watched.
     *
     * When the file has events from FileWatcherEvent, the onEvent method of the
     * watcher is expected to be called.
     *
     * @return true if the watcher was added successfully.
     */
    virtual bool addWatcher(weak_ref<FileWatcher> watcher) { return false; }



    // Helper methods.
    inline bool read(u8 *buffer, size_t size, size_t offset = 0) {
      IoRequest req;
      req.buffer = buffer;
      req.size = size;
      req.offset = offset;
      return read(req);
    }

    inline bool write(const u8 *buffer, size_t size, size_t offset = 0) {
      IoRequest req;
      req.buffer = const_cast<u8 *>(buffer);
      req.size = size;
      req.offset = offset;
      return write(req);
    }
  };



}  // namespace ren