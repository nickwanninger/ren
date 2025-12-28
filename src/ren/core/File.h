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


  using FileWatcher = Watcher<File, FileWatcherEvent>;

  // A File is just a thing that can have bytes read from it, and can maybe have
  // bytes written to it. Note that this interface does not actually require the
  // file is on disk, and as such it could be in memory.
  class File : public Notifier<File, FileWatcherEvent> {
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