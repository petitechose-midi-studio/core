#include <cassert>
#include <iostream>

#include "state/modulation/ProjectModulatorSourceSession.hpp"

namespace mod = core::state::modulation;

namespace {

struct Fixture {
    mod::ProjectControlState control{};
    mod::ModulatorId sourceId{};
    mod::ModulationBindingId bindingId{};
    mod::ModulationDestination destination{
        mod::ModulationDestinationKind::MACRO_SLOT,
        1U,
        2U,
        3U,
    };

    Fixture() {
        mod::ModulatorLfoDraft source{};
        source.name = "Session LFO";
        const auto created = mod::createLfoModulator(
            control.authored.modulation,
            source
        );
        assert(created.changed());
        sourceId = created.sourceId;

        mod::ModulationBindingDraft binding{};
        binding.sourceId = sourceId;
        binding.destination = destination;
        binding.amountQ15 = 8192;
        const auto assigned = mod::addProjectModulationBinding(
            control.authored.modulation,
            binding
        );
        assert(assigned.changed());
        bindingId = assigned.bindingId;
    }

    void setMode(mod::ProjectModulatorSourceSessionMode mode) {
        control.audition = {
            .sourceId = sourceId,
            .bindingId = bindingId,
            .destination = destination,
            .generation = 7U,
            .mode = mode,
        };
    }
};

bool allows(
    const mod::ProjectModulatorSourceSessionDescriptor& session,
    mod::ProjectModulatorSourceSessionCapability capability
) {
    return session.allows(capability);
}

void test_durable_project_permissions() {
    Fixture fixture;
    const auto session = mod::resolveProjectModulatorSourceSession(
        fixture.control,
        fixture.sourceId
    );
    assert(session.valid());
    assert(!session.audition());
    assert(session.mode ==
           mod::ProjectModulatorSourceSessionMode::DURABLE_PROJECT);
    assert(allows(session,
        mod::ProjectModulatorSourceSessionCapability::EDIT_SOURCE));
    assert(allows(session,
        mod::ProjectModulatorSourceSessionCapability::EDIT_TRIGGER));
    assert(allows(session,
        mod::ProjectModulatorSourceSessionCapability::MANAGE_ROUTING));
    assert(allows(session,
        mod::ProjectModulatorSourceSessionCapability::MANAGE_SOURCE));
    assert(!allows(session,
        mod::ProjectModulatorSourceSessionCapability::EDIT_DEPTH));
    assert(!allows(session,
        mod::ProjectModulatorSourceSessionCapability::APPLY_CANCEL));
}

void test_new_audition_permissions() {
    Fixture fixture;
    fixture.setMode(mod::ProjectModulatorSourceSessionMode::AUDITION_NEW);
    const auto session = mod::resolveProjectModulatorSourceSession(
        fixture.control,
        fixture.sourceId
    );
    assert(session.valid());
    assert(session.audition());
    assert(session.newAudition());
    assert(!session.existingAudition());
    assert(fixture.control.audition.sourceCreated());
    assert(allows(session,
        mod::ProjectModulatorSourceSessionCapability::EDIT_SOURCE));
    assert(allows(session,
        mod::ProjectModulatorSourceSessionCapability::EDIT_TRIGGER));
    assert(allows(session,
        mod::ProjectModulatorSourceSessionCapability::EDIT_DEPTH));
    assert(allows(session,
        mod::ProjectModulatorSourceSessionCapability::APPLY_CANCEL));
    assert(!allows(session,
        mod::ProjectModulatorSourceSessionCapability::MANAGE_ROUTING));
    assert(!allows(session,
        mod::ProjectModulatorSourceSessionCapability::MANAGE_SOURCE));
}

void test_existing_audition_is_depth_only() {
    Fixture fixture;
    fixture.setMode(mod::ProjectModulatorSourceSessionMode::AUDITION_EXISTING);
    const auto session = mod::resolveProjectModulatorSourceSession(
        fixture.control,
        fixture.sourceId
    );
    assert(session.valid());
    assert(session.audition());
    assert(!session.newAudition());
    assert(session.existingAudition());
    assert(fixture.control.audition.existingSource());
    assert(!allows(session,
        mod::ProjectModulatorSourceSessionCapability::EDIT_SOURCE));
    assert(!allows(session,
        mod::ProjectModulatorSourceSessionCapability::EDIT_TRIGGER));
    assert(allows(session,
        mod::ProjectModulatorSourceSessionCapability::EDIT_DEPTH));
    assert(allows(session,
        mod::ProjectModulatorSourceSessionCapability::APPLY_CANCEL));
    assert(!allows(session,
        mod::ProjectModulatorSourceSessionCapability::MANAGE_ROUTING));
    assert(!allows(session,
        mod::ProjectModulatorSourceSessionCapability::MANAGE_SOURCE));
}

void test_audition_resolution_fails_closed() {
    Fixture fixture;
    fixture.setMode(mod::ProjectModulatorSourceSessionMode::AUDITION_EXISTING);

    auto session = mod::resolveProjectModulatorSourceSession(
        fixture.control,
        mod::ModulatorId{fixture.sourceId.value + 1U}
    );
    assert(!session.valid());
    assert(session.capabilityMask == 0U);

    fixture.control.audition.generation = 0U;
    session = mod::resolveProjectModulatorSourceSession(
        fixture.control,
        fixture.sourceId
    );
    assert(!session.valid());
    assert(session.capabilityMask == 0U);

    fixture.control.audition.generation = 7U;
    fixture.control.audition.destination.macro = 4U;
    session = mod::resolveProjectModulatorSourceSession(
        fixture.control,
        fixture.sourceId
    );
    assert(!session.valid());
    assert(session.capabilityMask == 0U);

    fixture.control.audition.destination = fixture.destination;
    fixture.control.audition.bindingId = mod::ModulationBindingId{999U};
    session = mod::resolveProjectModulatorSourceSession(
        fixture.control,
        fixture.sourceId
    );
    assert(!session.valid());
    assert(session.capabilityMask == 0U);

    fixture.control.audition.bindingId = fixture.bindingId;
    fixture.control.authored.modulation.outputBindings[0].sourceId =
        mod::ModulatorId{fixture.sourceId.value + 1U};
    session = mod::resolveProjectModulatorSourceSession(
        fixture.control,
        fixture.sourceId
    );
    assert(!session.valid());
    assert(!session.audition());
    assert(!session.existingAudition());
    assert(session.capabilityMask == 0U);
}

void test_unknown_mode_never_falls_back_to_durable() {
    Fixture fixture;
    fixture.control.audition.mode =
        static_cast<mod::ProjectModulatorSourceSessionMode>(0xFFU);
    const auto session = mod::resolveProjectModulatorSourceSession(
        fixture.control,
        fixture.sourceId
    );
    assert(!fixture.control.audition.active());
    assert(!session.valid());
    assert(session.capabilityMask == 0U);
}

}  // namespace

int main() {
    test_durable_project_permissions();
    test_new_audition_permissions();
    test_existing_audition_is_depth_only();
    test_audition_resolution_fails_closed();
    test_unknown_mode_never_falls_back_to_durable();
    std::cout << "[PASS] Project Modulator source-session contract\n";
    return 0;
}
