#pragma once
#include <ren/assets/AssetManager.h>
#include <slang.h>
#include <slang-com-ptr.h>
#include <filesystem>
#include <string>
#include <vector>
#include <atomic>

namespace ren {

// A simple blob implementation to wrap vector data
class SlangBlob final : public ISlangBlob {
public:
    SlangBlob(std::vector<u8>&& data) : m_data(std::move(data)) {}

    // ISlangUnknown
    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override {
        if (uuid == ISlangBlob::getTypeGuid() || uuid == ISlangUnknown::getTypeGuid()) {
            *outObject = static_cast<ISlangBlob*>(this);
            addRef();
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }

    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return ++m_refCount; }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override {
        uint32_t result = --m_refCount;
        if (result == 0) {
            delete this;
        }
        return result;
    }

    // ISlangBlob
    SLANG_NO_THROW void const* SLANG_MCALL getBufferPointer() override { return m_data.data(); }
    SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() override { return m_data.size(); }

private:
    std::vector<u8> m_data;
    std::atomic<uint32_t> m_refCount{1};
};

class SlangFileSystem : public ISlangFileSystem {
public:
    // ISlangUnknown
    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override {
        if (uuid == ISlangFileSystem::getTypeGuid() || uuid == ISlangUnknown::getTypeGuid() || uuid == ISlangCastable::getTypeGuid()) {
            *outObject = static_cast<ISlangFileSystem*>(this);
            addRef();
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }

    // For static instance, addRef/release don't need to do anything with memory
    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override { return 1; }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override { return 1; }

    // ISlangCastable
    SLANG_NO_THROW void* SLANG_MCALL castAs(const SlangUUID& guid) override {
        if (guid == ISlangFileSystem::getTypeGuid() || guid == ISlangUnknown::getTypeGuid() || guid == ISlangCastable::getTypeGuid()) {
            return static_cast<ISlangFileSystem*>(this);
        }
        return nullptr;
    }

    // ISlangFileSystem
    SLANG_NO_THROW SlangResult SLANG_MCALL loadFile(
        char const* path,
        ISlangBlob** outBlob) override {

        
        std::vector<u8> content;
        // Try loading through asset manager
        if (ren::loadAssetBytes(path, content)) {
            *outBlob = new SlangBlob(std::move(content));
            ren::println("SlangFileSystem::loadFile({})", path);
            return SLANG_OK;
        }
        
        return SLANG_E_NOT_FOUND;
    }
};

} // namespace ren
