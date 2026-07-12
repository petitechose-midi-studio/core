#include "protocol/filesystem/FileSystemRpc.hpp"

#include <config/PlatformCompat.hpp>

namespace core::protocol::filesystem {

FLASHMEM FileSystemRpcEndpoint::FileSystemRpcEndpoint(
    oc::interface::ITransport& transport,
    core::persistence::ProductFileService& files,
    NowProvider nowProvider,
    FileSystemRpcHandler::Config handlerConfig
) : transport_(transport),
    nowProvider_(nowProvider),
    handler_(files, handlerConfig) {}

FLASHMEM FileSystemRpcEndpoint::~FileSystemRpcEndpoint() {
    end();
}

FLASHMEM void FileSystemRpcEndpoint::begin() {
    transport_.setOnReceive([this](const uint8_t* data, size_t size) {
        handleReceive_(data, size);
    });
    active_ = true;
}

FLASHMEM void FileSystemRpcEndpoint::end() {
    if (!active_) {
        return;
    }
    transport_.setOnReceive({});
    handler_.abortWriteSession();
    active_ = false;
}

void FileSystemRpcEndpoint::update() {
    if (!active_ || !handler_.hasActiveWriteSession()) return;
    handler_.update(nowProvider_ ? nowProvider_() : 0);
}

FLASHMEM bool FileSystemRpcEndpoint::active() const {
    return active_;
}

FLASHMEM void FileSystemRpcEndpoint::handleReceive_(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return;
    }
    const uint8_t messageId = data[0];
    if (!FileSystemRpcCodec::isFileSystemRequestId(messageId)) {
        return;
    }

    const uint32_t nowMs = nowProvider_ ? nowProvider_() : 0;
    auto response = handler_.handleFrame(
        data,
        size,
        nowMs,
        response_,
        sizeof(response_)
    );
    if (!response || response.value() == 0) {
        return;
    }
    transport_.send(response_, response.value());
}


}  // namespace core::protocol::filesystem
