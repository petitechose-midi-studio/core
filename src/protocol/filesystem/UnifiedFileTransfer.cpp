#include "UnifiedFileTransfer.hpp"
#include "FileSystemRpcInternal.hpp"
#include "persistence/AtomicProductFile.hpp"
#include "persistence/PersistenceChecksum.hpp"
#include <cstdio>
#include <cstring>
#include <utility>
#include <config/PlatformCompat.hpp>

namespace core::protocol::filesystem::unified {
namespace p = core::persistence;
using internal::ByteReader;
using internal::ByteWriter;

namespace {
FLASHMEM bool readCanonicalPath(ByteReader& reader, char* path, size_t capacity) {
    const auto before = reader.remaining();
    if (!reader.readString(path, capacity, oc::interface::FILESYSTEM_MAX_PATH_LENGTH) || !path[0]) return false;
    // Reject embedded NULs: wire length and the path seen by storage must agree.
    return before - reader.remaining() == std::strlen(path) + 1;
}

FLASHMEM Error storageError(oc::type::Error error) {
    using E = oc::type::ErrorCode;
    switch (error.code) {
        case E::INVALID_ARGUMENT: return Error::InvalidArgument;
        case E::INVALID_STATE: return Error::PreconditionFailed;
        case E::RESOURCE_NOT_FOUND: return Error::NotFound;
        case E::RESOURCE_EXHAUSTED: case E::HARDWARE_BUSY: return Error::ResourceExhausted;
        case E::HARDWARE_NOT_FOUND: case E::HARDWARE_INIT_FAILED: return Error::StorageUnavailable;
        case E::HARDWARE_TIMEOUT: return Error::DeadlineExceeded;
        case E::STORAGE_READ_FAILED: return Error::StorageReadFailed;
        case E::STORAGE_WRITE_FAILED: return Error::StorageWriteFailed;
        case E::STORAGE_CORRUPT: return Error::StorageCorrupt;
        default: return Error::Internal;
    }
}
}

FileTransfer::~FileTransfer() { release(true); }

FLASHMEM FileTransfer::Record* FileTransfer::find(uint32_t nonce) {
    if (nonce) for (auto& record : records_) if (record.nonce == nonce) return &record;
    return nullptr;
}

FLASHMEM FileTransfer::Record* FileTransfer::available() {
    for (auto& record : records_) if (!record.nonce) return &record;
    return nullptr;
}

FLASHMEM void FileTransfer::expire(uint32_t nowMs) {
    for (auto& record : records_) {
        if (record.nonce && &record != active_ && uint32_t(nowMs - record.terminalAt) >= RETENTION_MS)
            record = {};
    }
}

FLASHMEM void FileTransfer::terminal(State state, Error error, uint32_t nowMs) {
    if (active_) {
        active_->state = state; active_->error = error; active_->terminalAt = nowMs;
        active_ = nullptr;
    }
}

FLASHMEM bool FileTransfer::irreversible() const {
    return plan_.mapped() || plan_.requiresRecoveryOnFailure();
}

FLASHMEM bool FileTransfer::release(bool discard, bool completed) {
    bool okay = true;
    if (files_.owns(lease_)) {
        if (discard && irreversible()) {
            (void)files_.requireRecovery(lease_, oc::type::ErrorCode::STORAGE_WRITE_FAILED);
            okay = false;
        } else if (discard) {
            okay = bool(files_.abortWrite(lease_));
            okay = bool(p::deleteProductFileIfExists(files_, lease_, temporary_)) && okay;
            if (!okay) (void)files_.requireRecovery(lease_, oc::type::ErrorCode::STORAGE_WRITE_FAILED);
        }
        okay = bool(files_.releaseMutation(lease_)) && okay;
    }
    if (token_.valid()) {
        if (completed && okay) (void)files_.persistenceJobs().complete(token_);
        else (void)files_.persistenceJobs().cancelAfterUnwind(token_);
    }
    session_ = 0;
    deferredError_ = Error::None;
    plan_.reset();
    return okay;
}

FLASHMEM bool FileTransfer::discard(uint32_t nowMs, Error& outcome) {
    auto& jobs = files_.persistenceJobs();
    if (!jobs.prepareAdvance(token_, p::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE)
        || !jobs.claimAdvance(token_, nowMs)) return false;
    p::ProductPersistenceWorkUsage usage{};
    bool okay = false;
    {
        auto measured = files_.measurePersistenceWork(usage);
        if (!measured) { (void)jobs.finishAdvance(token_, usage, false); return false; }
        okay = release(true);
    }
    if (!jobs.finishAdvance(token_, usage, session_ == 0)) outcome = Error::ResourceExhausted;
    else if (!okay) outcome = Error::StorageWriteFailed;
    if (!session_) (void)jobs.cancelAfterUnwind(token_);
    return true;
}

FLASHMEM Error FileTransfer::begin(const Frame& request, uint32_t nowMs) {
    ByteReader reader(request.body, request.bodySize);
    uint32_t expected = 0;
    char path[sizeof(final_)]{};
    char resolved[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1]{};
    if (!reader.readU32(expected) || expected > 524'288
        || !readCanonicalPath(reader, path, sizeof(path)) || reader.remaining() != 0
        || !files_.resolvePath(path, resolved, sizeof(resolved))
        || internal::isProtocolReservedPath(files_, path)) return Error::InvalidArgument;
    if (session_ || !available()) return Error::ResourceExhausted;
    // Coordinator IDs never wrap or repeat during its lifetime. A delayed chunk,
    // abort or commit cannot address a later upload, even after result expiry.
    const uint32_t session = token_.id();
    auto lease = files_.acquireMutation(p::ProductMutationOwner::FILESYSTEM_RPC);
    if (!lease) return Error::ResourceExhausted;
    lease_ = std::move(lease.value());
    std::memcpy(final_, path, sizeof(path));
    std::snprintf(temporary_, sizeof(temporary_), "tmp/rpc-write-%08X.tmp", unsigned(session));
    std::snprintf(backup_, sizeof(backup_), "tmp/rpc-backup-%08X.tmp", unsigned(session));
    if (!p::deleteProductFileIfExists(files_, lease_, temporary_) || !files_.beginWrite(lease_, temporary_, expected)) {
        release(true); return Error::StorageWriteFailed;
    }
    session_ = session; expected_ = expected; written_ = 0; uploadStarted_ = nowMs;
    crc_ = p::checksum::CRC32_INITIAL_STATE;
    return Error::None;
}

FLASHMEM Error FileTransfer::execute(const Frame& r, uint32_t nowMs, uint8_t* body, size_t& size) {
    ByteReader reader(r.body, r.bodySize);
    ByteWriter writer(body, MAX_BODY);
    uint16_t count = 0; uint32_t session = 0, offset = 0;
    if (r.operation == Operation::UploadBegin) {
        const auto error = begin(r, nowMs);
        if (error == Error::None) { writer.writeU32(session_); size = writer.position(); }
        return error;
    }
    if (r.operation == Operation::UploadChunk) {
        const uint8_t* data = nullptr;
        if (!reader.readU32(session) || !reader.readU32(offset) || !reader.readU16(count)
            || count > 30'720 || !reader.readBytes(data, count) || reader.remaining()) return Error::InvalidArgument;
        if (!session_ || session != session_ || pending()) return Error::PreconditionFailed;
        if (offset != written_ || offset > expected_ || count > expected_ - offset) return Error::InvalidArgument;
        const auto appended = files_.appendWrite(lease_, data, count);
        if (!appended || appended.value() != count) {
            deferredError_ = Error::StorageWriteFailed;
            return deferredError_; // Cleanup gets its own measured promotion turn.
        }
        crc_ = p::checksum::crc32Update(crc_, data, count); written_ += count;
        writer.writeU32(written_); size = writer.position(); return Error::None;
    }
    if (r.operation == Operation::UploadCommit) {
        if (!reader.readU32(session) || reader.remaining()) return Error::InvalidArgument;
        if (!session_ || session != session_ || written_ != expected_ || pending()) return Error::PreconditionFailed;
        active_ = available();
        if (!active_) return Error::ResourceExhausted;
        *active_ = {r.nonce, session_, r.delayMs, nowMs, 0, files_.storageIdentity().mediaGeneration};
        if (!files_.finishWrite(lease_) || !plan_.begin(files_, lease_, final_, backup_, temporary_,
                expected_, p::checksum::crc32Finish(crc_))) {
            release(true); terminal(State::Failed, Error::StorageWriteFailed, nowMs);
            return Error::StorageWriteFailed;
        }
        return Error::None;
    }
    if (r.operation == Operation::UploadAbort) {
        if (!reader.readU32(session) || reader.remaining()) return Error::InvalidArgument;
        if (session != session_ || !session_ || pending()) return Error::PreconditionFailed;
        return release(true) ? Error::None : Error::StorageWriteFailed;
    }
    if (r.operation == Operation::Read || r.operation == Operation::Stat) {
        char path[sizeof(final_)]{};
        if (!readCanonicalPath(reader, path, sizeof(path))) return Error::InvalidArgument;
        if (r.operation == Operation::Stat) {
            if (reader.remaining()) return Error::InvalidArgument;
            const auto stat = files_.stat(path);
            if (!stat) return storageError(stat.error());
            writer.writeU8(static_cast<uint8_t>(stat.value().type)); writer.writeU32(stat.value().sizeBytes);
            size = writer.position(); return Error::None;
        }
        if (!reader.readU32(offset) || !reader.readU16(count) || count > 30'720 || reader.remaining()) return Error::InvalidArgument;
        const auto read = files_.read(path, offset, body, count);
        if (!read) return storageError(read.error());
        size = read.value(); return Error::None;
    }
    return Error::Unsupported;
}

FLASHMEM size_t FileTransfer::process(const uint8_t* data, size_t size, uint32_t nowMs, bool playing,
                                      uint8_t* output, size_t capacity) {
    Frame request;
    if (!output || capacity < HEADER + MAX_BODY || !decode(data, size, request) || request.state != State::Request) return 0;
    expire(nowMs);
    Frame response = request; response.state = State::Complete; response.delayMs = 0;
    response.body = output + HEADER; response.bodySize = 0;
    auto finish = [&](Error error) {
        response.error = error;
        if (error != Error::None) { response.state = State::Failed; response.delayMs = 0; response.bodySize = 0; }
        // The body is already in its final output location; encode header separately.
        const auto bodySize = response.bodySize;
        response.body = nullptr; response.bodySize = 0;
        const size_t header = encode(response, output, capacity);
        if (!header) return size_t(0);
        for (size_t i = 0; i < 4; ++i) output[20+i] = uint8_t(bodySize >> (8*i));
        return HEADER + bodySize;
    };
    if (request.operation == Operation::Capabilities) {
        if (request.bodySize) return finish(Error::InvalidArgument);
        ByteWriter writer(output + HEADER, MAX_BODY);
        writer.writeU32(0x60fbU); // Implemented subset: capabilities/stat/read/upload/poll/cancel.
        writer.writeU32(30'720); writer.writeU32(524'288); writer.writeU32(RETENTION_MS);
        writer.writeU16(oc::interface::FILESYSTEM_MAX_PATH_LENGTH); writer.writeU8(1); writer.writeU8(RETAINED_CAPACITY);
        response.bodySize = writer.position(); return finish(Error::None);
    }
    const bool query = request.operation == Operation::Poll || request.operation == Operation::Cancel;
    auto* record = find(request.nonce);
    if (query || (request.operation == Operation::UploadCommit && record)) {
        if (!record || (query && request.operationId != record->id)) return finish(Error::ResultExpired);
        response.operationId = record->id;
        if (!query) {
            ByteReader reader(request.body, request.bodySize); uint32_t session = 0;
            if (!reader.readU32(session) || reader.remaining() || session != record->id || request.delayMs != record->deadline)
                return finish(Error::Conflict);
            response.replayed = true;
        }
        if (record->media != files_.storageIdentity().mediaGeneration) return finish(Error::MediaChanged);
        if (request.operation == Operation::Cancel && record == active_) {
            if (irreversible()) return finish(Error::CancelTooLate);
            if (playing) return finish(Error::BusyPlaying);
            Error outcome = Error::Cancelled;
            if (!discard(nowMs, outcome)) return finish(Error::ResourceExhausted);
            terminal(outcome == Error::Cancelled ? State::Cancelled : State::Failed, outcome, nowMs);
        }
        response.state = record->state; response.error = record->error;
        response.delayMs = record->state == State::Pending ? 5 : 0;
        return encode(response, output, capacity);
    }
    if (playing) return finish(Error::BusyPlaying);
    if (session_ && !files_.owns(lease_)) {
        release(false); terminal(State::Failed, Error::MediaChanged, nowMs);
        return finish(Error::MediaChanged);
    }
    if (deferredError_ != Error::None) return finish(Error::PreconditionFailed);
    if (session_ && !pending() && uint32_t(nowMs - uploadStarted_) > MAX_DEADLINE_MS) {
        Error outcome = Error::DeadlineExceeded;
        if (!discard(nowMs, outcome)) return finish(Error::ResourceExhausted);
        return finish(outcome);
    }
    auto& jobs = files_.persistenceJobs();
    if (!token_.valid()) {
        auto admitted = jobs.admit({p::ProductPersistenceJobOwner::FILESYSTEM_RPC, nowMs, 0,
            p::PRODUCT_PERSISTENCE_QUOTA_ENDPOINT_FRAME});
        if (!admitted) return finish(Error::ResourceExhausted);
        token_ = std::move(admitted.value());
    }
    const auto quota = request.operation == Operation::UploadBegin || request.operation == Operation::UploadCommit
        || request.operation == Operation::UploadAbort ? p::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE
        : p::PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO;
    if (!jobs.prepareAdvance(token_, quota) || !jobs.claimAdvance(token_, nowMs)) return finish(Error::ResourceExhausted);
    p::ProductPersistenceWorkUsage usage{};
    Error error = Error::Internal;
    {
        auto measured = files_.measurePersistenceWork(usage);
        if (measured) error = execute(request, nowMs, output + HEADER, response.bodySize);
    }
    if (token_.valid() && !jobs.finishAdvance(token_, usage, session_ == 0)) {
        error = Error::ResourceExhausted;
        if (session_) deferredError_ = error;
    }
    if (!session_ && token_.valid()) (void)jobs.complete(token_);
    if (request.operation == Operation::UploadCommit) {
        if (auto* admitted = find(request.nonce)) {
            response.operationId = admitted->id;
            if (admitted == active_) { response.state = State::Pending; response.delayMs = 5; error = Error::None; }
        }
    }
    return finish(error);
}

FLASHMEM void FileTransfer::advance(uint32_t nowMs, bool playing, uint8_t* scratch, size_t capacity) {
    expire(nowMs);
    if (session_ && !files_.owns(lease_)) {
        release(false); terminal(State::Failed, Error::MediaChanged, nowMs); return;
    }
    if (playing) return;
    if (deferredError_ != Error::None) {
        Error outcome = deferredError_;
        if (discard(nowMs, outcome)) terminal(State::Failed, outcome, nowMs);
        return;
    }
    if (!pending()) {
        if (session_ && (uint32_t(nowMs - uploadStarted_) > MAX_DEADLINE_MS
                        || files_.persistenceJobs().deferredAutosaveAged(nowMs))) {
            Error outcome = Error::DeadlineExceeded;
            (void)discard(nowMs, outcome);
        }
        return;
    }
    if (!scratch || capacity < 30'720) return;
    if (!irreversible() && uint32_t(nowMs - active_->started) >= active_->deadline) {
        Error outcome = Error::DeadlineExceeded;
        if (discard(nowMs, outcome)) terminal(State::Failed, outcome, nowMs);
        return;
    }
    auto& jobs = files_.persistenceJobs();
    const auto quota = plan_.nextAdvanceReadsData() ? p::PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO : p::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE;
    if (!jobs.prepareAdvance(token_, quota) || !jobs.claimAdvance(token_, nowMs)) return;
    p::ProductPersistenceWorkUsage usage{};
    bool done = false; Error error = Error::None;
    {
        auto measured = files_.measurePersistenceWork(usage);
        if (measured) {
            auto advanced = plan_.advance(files_, lease_, scratch, capacity);
            done = advanced && advanced.value();
            if (!advanced) error = storageError(advanced.error());
        } else error = Error::Internal;
    }
    if (!jobs.finishAdvance(token_, usage, done)) error = Error::ResourceExhausted;
    if (error != Error::None) deferredError_ = error;
    else if (done) {
        const bool success = release(false, true);
        terminal(success ? State::Complete : State::Failed,
                 success ? Error::None : Error::StorageWriteFailed, nowMs);
    }
}

}  // namespace core::protocol::filesystem::unified
