#include <cassert>
#include <iostream>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/state/modulation/ProjectControlMacroOps.hpp"
#include "../../src/state/modulation/ProjectControlStructureTransferOps.hpp"
#include "../../src/state/modulation/ProjectModulationDomainOps.hpp"

namespace {

namespace modulation = core::state::modulation;

modulation::ModulatorId addLfo(
    modulation::ProjectControlDomainState& domain,
    uint8_t page
) {
    modulation::ModulatorLfoDraft source{};
    source.name = "Transfer LFO";
    const auto created =
        modulation::createLfoModulator(domain.modulation, source);
    assert(created.changed());

    modulation::ModulationBindingDraft binding{};
    binding.sourceId = created.sourceId;
    binding.destination = modulation::projectControlDestination({
        .track = 0U,
        .page = page,
        .macro = 0U,
    });
    binding.amountQ15 = 16384;
    assert(modulation::addProjectModulationBinding(
        domain.modulation,
        binding
    ).changed());
    return created.sourceId;
}

void addBinding(
    modulation::ProjectControlDomainState& domain,
    modulation::ModulatorId sourceId,
    uint8_t page
) {
    modulation::ModulationBindingDraft binding{};
    binding.sourceId = sourceId;
    binding.destination = modulation::projectControlDestination({
        .track = 0U,
        .page = page,
        .macro = 0U,
    });
    binding.amountQ15 = 8192;
    assert(modulation::addProjectModulationBinding(
        domain.modulation,
        binding
    ).changed());
}

modulation::ProjectControlStructureTransferPlan pagePlan(
    uint8_t sourcePage,
    uint8_t targetPage
) {
    modulation::ProjectControlStructureTransferPlan plan{};
    plan.count = 1U;
    plan.entries[0] = {
        .sourceTrack = 0U,
        .targetTrack = 0U,
        .sourcePage = sourcePage,
        .targetPage = targetPage,
        .wholeTrack = false,
    };
    return plan;
}

void test_repeated_local_source_paste_does_not_leak_clones() {
    auto source = core::app::makeExtmemUnique<
        modulation::ProjectControlDomainState>();
    auto target = core::app::makeExtmemUnique<
        modulation::ProjectControlDomainState>();
    assert(source && target);
    (void)addLfo(*source, 0U);

    const auto plan = pagePlan(0U, 1U);
    assert(modulation::replaceProjectControlStructureInDomain(
        *target,
        *source,
        plan
    ));
    assert(target->modulation.sourceCount == 1U);
    assert(target->modulation.outputBindingCount == 1U);

    assert(modulation::replaceProjectControlStructureInDomain(
        *target,
        *source,
        plan
    ));
    assert(target->modulation.sourceCount == 1U);
    assert(target->modulation.outputBindingCount == 1U);

    std::cout
        << "[PASS] repeated local-source Paste replaces its prior clone\n";
}

void test_shared_source_keeps_stable_project_identity() {
    auto source = core::app::makeExtmemUnique<
        modulation::ProjectControlDomainState>();
    auto target = core::app::makeExtmemUnique<
        modulation::ProjectControlDomainState>();
    assert(source && target);
    const auto sourceId = addLfo(*source, 0U);
    addBinding(*source, sourceId, 2U);
    *target = *source;

    const auto plan = pagePlan(0U, 1U);
    assert(modulation::replaceProjectControlStructureInDomain(
        *target,
        *source,
        plan
    ));
    assert(target->modulation.sourceCount == 1U);
    assert(target->modulation.sources[0].id == sourceId);
    assert(target->modulation.outputBindingCount == 3U);

    assert(modulation::replaceProjectControlStructureInDomain(
        *target,
        *source,
        plan
    ));
    assert(target->modulation.sourceCount == 1U);
    assert(target->modulation.outputBindingCount == 3U);

    std::cout
        << "[PASS] shared Project source keeps one stable identity\n";
}

}  // namespace

int main() {
    test_repeated_local_source_paste_does_not_leak_clones();
    test_shared_source_keeps_stable_project_identity();
    std::cout
        << "\nAll ProjectControlStructureTransferOps tests passed.\n";
    return 0;
}
