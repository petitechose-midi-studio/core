#pragma once

#include <cstring>
#include <oc/interface/IFileSystem.hpp>

namespace core::validation::benchmark {

// Qualification-only namespace. Recovery and journal paths are rooted here too,
// so a benchmark cannot recover, replace or delete the user's product files.
class BenchFileSystem final : public oc::interface::IFileSystem {
public:
    static constexpr const char* ROOT = "/ms-rpc-bench";
    explicit BenchFileSystem(oc::interface::IFileSystem& backend) : backend_(backend) {}
    oc::type::Result<void> init() override {
        auto result = backend_.init();
        if (!result) return result;
        auto info = backend_.stat(ROOT);
        if (!info) {
            if (info.error().code == oc::type::ErrorCode::RESOURCE_NOT_FOUND)
                return backend_.createDirectory(ROOT);
            return oc::type::Result<void>::err(info.error());
        }
        if (info.value().type == oc::interface::FileType::DIRECTORY) return result;
        if (info.value().type != oc::interface::FileType::MISSING) return invalid<void>();
        return backend_.createDirectory(ROOT);
    }
    bool available() const override { return backend_.available(); }
    oc::type::Result<oc::interface::FileInfo> stat(const char* path) override {
        return at(path, [&](const char* p) { return backend_.stat(p); });
    }
    oc::type::Result<void> list(const char* path, oc::interface::DirectoryEntryVisitor visitor,
                               void* context) override {
        return at(path, [&](const char* p) { return backend_.list(p, visitor, context); });
    }
    oc::type::Result<void> createDirectory(const char* path) override {
        return at(path, [&](const char* p) { return backend_.createDirectory(p); });
    }
    oc::type::Result<void> remove(const char* path, oc::interface::RemoveMode mode) override {
        if (path && std::strcmp(path, "/") == 0) return invalid<void>();
        return at(path, [&](const char* p) { return backend_.remove(p, mode); });
    }
    oc::type::Result<void> rename(const char* from, const char* to) override {
        if ((from && std::strcmp(from, "/") == 0) ||
            (to && std::strcmp(to, "/") == 0)) return invalid<void>();
        return at(from, [&](const char* a) {
            return at(to, [&](const char* b) { return backend_.rename(a, b); });
        });
    }
    oc::type::Result<size_t> read(const char* path, uint32_t offset, uint8_t* data,
                                 size_t size) override {
        return at(path, [&](const char* p) { return backend_.read(p, offset, data, size); });
    }
    oc::type::Result<size_t> write(const char* path, uint32_t offset, const uint8_t* data,
                                  size_t size) override {
        return at(path, [&](const char* p) { return backend_.write(p, offset, data, size); });
    }
    oc::type::Result<void> flush(const char* path) override {
        return at(path, [&](const char* p) { return backend_.flush(p); });
    }
    oc::type::Result<void> beginWrite(const char* path, uint32_t size) override {
        return at(path, [&](const char* p) { return backend_.beginWrite(p, size); });
    }
    oc::type::Result<size_t> appendWrite(const uint8_t* data, size_t size) override {
        return backend_.appendWrite(data, size);
    }
    oc::type::Result<void> finishWrite() override { return backend_.finishWrite(); }
    void abortWrite() override { backend_.abortWrite(); }

private:
    template<typename T> static oc::type::Result<T> invalid() {
        return oc::type::Result<T>::err({oc::type::ErrorCode::INVALID_ARGUMENT,
                                       "benchmark path outside bounded namespace"});
    }
    template<typename F> auto at(const char* path, F action) -> decltype(action(path)) {
        using Result = decltype(action(path));
        char mapped[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1];
        auto fail = [] { return Result::err({oc::type::ErrorCode::INVALID_ARGUMENT,
                                             "invalid benchmark path"}); };
        if (!path || path[0] != '/') return fail();
        const size_t prefix = std::strlen(ROOT);
        size_t length = 0, segment = 1;
        for (; path[length]; ++length) {
            if (prefix + length >= sizeof(mapped) - 1 || path[length] == '\\' ||
                path[length] == ':' || static_cast<unsigned char>(path[length]) < 32) return fail();
            if (length && path[length] == '/') {
                if (length == segment || path[length - 1] == ' ' || path[length - 1] == '.')
                    return fail();
                segment = length + 1;
            }
        }
        if (length > 1 && (path[length - 1] == ' ' || path[length - 1] == '.')) return fail();
        std::memcpy(mapped, ROOT, prefix);
        std::memcpy(mapped + prefix, path, length + 1);
        return action(mapped);
    }
    oc::interface::IFileSystem& backend_;
};
} // namespace core::validation::benchmark
