#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../../src/handler/settings/DeviceSettingsDomainServices.hpp"
#include "../support/MemoryStorage.hpp"

namespace {
using test_support::MemoryStorage;

class BufferedSettingsStorage final : public oc::interface::IStorage {
public:
    enum class FaultMode : uint8_t {
        NONE = 0,
        SHORT_WRITE,
        COMMIT_FAIL,
    };

    explicit BufferedSettingsStorage(size_t capacity = 4096U)
        : durable_(capacity, 0xFF), staged_(durable_) {}

    oc::type::Result<void> init() override {
        initialized_ = true;
        staged_ = durable_;
        dirty_ = false;
        return oc::type::Result<void>::ok();
    }

    bool available() const override { return initialized_; }

    size_t read(uint32_t address, uint8_t* buffer, size_t size) override {
        if (!initialized_ || buffer == nullptr || address >= staged_.size()) {
            return 0U;
        }
        const size_t count = std::min(
            size,
            staged_.size() - static_cast<size_t>(address)
        );
        std::memcpy(buffer, staged_.data() + address, count);
        return count;
    }

    size_t write(
        uint32_t address,
        const uint8_t* buffer,
        size_t size
    ) override {
        if (!initialized_ || buffer == nullptr || address >= staged_.size()) {
            return 0U;
        }
        size_t count = std::min(
            size,
            staged_.size() - static_cast<size_t>(address)
        );
        if (faultMode_ == FaultMode::SHORT_WRITE && count > 0U) --count;
        if (count > 0U) {
            std::memcpy(staged_.data() + address, buffer, count);
            dirty_ = true;
        }
        return count;
    }

    bool commit() override {
        if (!initialized_ || faultMode_ == FaultMode::COMMIT_FAIL) return false;
        durable_ = staged_;
        dirty_ = false;
        return true;
    }

    bool erase(uint32_t address, size_t size) override {
        if (!initialized_ || address >= staged_.size()) return false;
        const size_t count = std::min(
            size,
            staged_.size() - static_cast<size_t>(address)
        );
        std::fill_n(staged_.begin() + address, count, 0xFF);
        dirty_ = true;
        return count == size;
    }

    size_t capacity() const override { return staged_.size(); }
    bool isDirty() const override { return dirty_; }

    void setFaultMode(FaultMode mode) { faultMode_ = mode; }
    void reboot() {
        faultMode_ = FaultMode::NONE;
        staged_ = durable_;
        dirty_ = false;
    }

private:
    std::vector<uint8_t> durable_;
    std::vector<uint8_t> staged_;
    FaultMode faultMode_ = FaultMode::NONE;
    bool initialized_ = false;
    bool dirty_ = false;
};

}  // namespace

int main() {
    MemoryStorage storage;
    storage.init();

    core::state::MidiSyncState sync;
    core::persistence::DeviceSettingsStore store(storage);
    assert(store.load(sync));
    core::handler::DeviceSettingsDomainServices services(
        core::handler::DeviceSettingsDomainServices::StateRefs{
            sync,
            store,
        }
    );

    assert(services.choiceCount(0) == 3);
    assert(services.choiceCount(1) == 2);
    assert(services.choiceCount(2) == 7);
    assert(services.choiceCount(3) == 8);
    assert(services.currentChoiceIndex(0) == 2);
    assert(services.currentChoiceIndex(1) == 1);
    assert(services.currentChoiceIndex(2) == 2);
    assert(services.currentChoiceIndex(3) == 4);

    const auto modeResult = services.applyChoice(0, 1);
    assert(modeResult.success());
    assert(modeResult.changed());
    assert(modeResult.persistenceStatus ==
           core::persistence::PersistenceWriteStatus::OK);
    assert(sync.mode.get() == core::state::MidiSyncMode::SLAVE);
    assert(!storage.isDirty());

    const auto followResult = services.applyChoice(1, 0);
    assert(followResult.success() && followResult.changed());
    assert(!sync.followTransport.get());
    assert(!storage.isDirty());

    const auto fallbackResult = services.applyChoice(2, 5);
    assert(fallbackResult.success() && fallbackResult.changed());
    assert(sync.autoFallbackMs.get() == 1500);
    assert(!storage.isDirty());

    const auto lockResult = services.applyChoice(3, 7);
    assert(lockResult.success() && lockResult.changed());
    assert(sync.autoLockClockCount.get() == 24);
    assert(!storage.isDirty());

    const int commitsBeforeNoChange = storage.commitCount;
    const auto noChange = services.applyMidiSyncMode(
        core::state::MidiSyncMode::SLAVE
    );
    assert(noChange.success());
    assert(!noChange.changed());
    assert(storage.commitCount == commitsBeforeNoChange);

    const auto invalid = services.applyMidiSyncMode(
        static_cast<core::state::MidiSyncMode>(0xFFU)
    );
    assert(!invalid.success());
    assert(!invalid.changed());
    assert(invalid.persistenceStatus ==
           core::persistence::PersistenceWriteStatus::INVALID_CONFIG);
    assert(storage.commitCount == commitsBeforeNoChange);

    BufferedSettingsStorage bufferedStorage;
    assert(bufferedStorage.init());
    core::state::MidiSyncState bufferedSync;
    core::persistence::DeviceSettingsStore bufferedStore(bufferedStorage);
    assert(bufferedStore.load(bufferedSync));
    core::handler::DeviceSettingsDomainServices bufferedServices(
        core::handler::DeviceSettingsDomainServices::StateRefs{
            bufferedSync,
            bufferedStore,
        }
    );

    const auto applied = bufferedServices.applyMidiSyncMode(
        core::state::MidiSyncMode::SLAVE
    );
    assert(applied.success() && applied.changed());
    assert(bufferedSync.mode.get() == core::state::MidiSyncMode::SLAVE);
    bufferedStorage.reboot();
    core::state::MidiSyncState rebootedAfterSuccess;
    assert(bufferedStore.load(rebootedAfterSuccess));
    assert(rebootedAfterSuccess.mode.get() == core::state::MidiSyncMode::SLAVE);

    bufferedStorage.setFaultMode(BufferedSettingsStorage::FaultMode::SHORT_WRITE);
    const auto stageFailure = bufferedServices.applyMidiSyncMode(
        core::state::MidiSyncMode::MASTER
    );
    assert(!stageFailure.success());
    assert(!stageFailure.changed());
    assert(stageFailure.persistenceStatus ==
           core::persistence::PersistenceWriteStatus::IO_ERROR);
    assert(bufferedSync.mode.get() == core::state::MidiSyncMode::SLAVE);
    bufferedStorage.reboot();
    core::state::MidiSyncState rebootedAfterStageFailure;
    assert(bufferedStore.load(rebootedAfterStageFailure));
    assert(rebootedAfterStageFailure.mode.get() ==
           core::state::MidiSyncMode::SLAVE);

    bufferedStorage.setFaultMode(BufferedSettingsStorage::FaultMode::COMMIT_FAIL);
    const auto commitFailure = bufferedServices.applyMidiSyncMode(
        core::state::MidiSyncMode::AUTO
    );
    assert(!commitFailure.success());
    assert(!commitFailure.changed());
    assert(commitFailure.persistenceStatus ==
           core::persistence::PersistenceWriteStatus::COMMIT_FAILED);
    assert(bufferedSync.mode.get() == core::state::MidiSyncMode::SLAVE);
    bufferedStorage.reboot();
    core::state::MidiSyncState rebootedAfterCommitFailure;
    assert(bufferedStore.load(rebootedAfterCommitFailure));
    assert(rebootedAfterCommitFailure.mode.get() ==
           core::state::MidiSyncMode::SLAVE);

    return 0;
}
