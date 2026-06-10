#include <cassert>
#include <array>
#include <cstring>

#include "../../src/state/project/ProjectSlug.hpp"

namespace {

namespace project = core::state::project;

void testValidSlugCharacters() {
    assert(project::isProjectSlugChar('a'));
    assert(project::isProjectSlugChar('z'));
    assert(project::isProjectSlugChar('A'));
    assert(project::isProjectSlugChar('Z'));
    assert(project::isProjectSlugChar('0'));
    assert(project::isProjectSlugChar('9'));
    assert(project::isProjectSlugChar('-'));
    assert(project::isProjectSlugChar('.'));
    assert(project::isProjectSlugChar(' '));
    assert(!project::isProjectSlugChar('_'));
    assert(!project::isProjectSlugChar('/'));
}

void testSlugValidation() {
    assert(project::validProjectSlug("p001"));
    assert(project::validProjectSlug("P001"));
    assert(project::validProjectSlug("live-set.01"));
    assert(project::validProjectSlug("Live Set.01"));
    assert(project::validProjectSlug("kick-pattern-2"));
    assert(project::validProjectSlug("untitled"));

    assert(!project::validProjectSlug(nullptr));
    assert(!project::validProjectSlug(""));
    assert(!project::validProjectSlug("project_001"));
    assert(!project::validProjectSlug(" project"));
    assert(!project::validProjectSlug("project "));
    const char accentedSlug[] = {
        'p',
        'r',
        'o',
        'j',
        'e',
        't',
        '-',
        static_cast<char>(0xC3),
        static_cast<char>(0xA9),
        '\0',
    };
    assert(!project::validProjectSlug(accentedSlug));
    assert(!project::validProjectSlug("folder/name"));
    assert(!project::validProjectSlug(".hidden"));
    assert(!project::validProjectSlug("trailing."));
    assert(!project::validProjectSlug("double..dot"));
}

void testSlugLengthFollowsFilesystemSafeProjectNameLimit() {
    std::array<char, project::PROJECT_SLUG_SIZE> maxSlug{};
    for (size_t i = 0; i < project::PROJECT_SLUG_MAX_LENGTH; ++i) {
        maxSlug[i] = 'a';
    }
    maxSlug[project::PROJECT_SLUG_MAX_LENGTH] = '\0';
    assert(project::validProjectSlug(maxSlug.data()));

    std::array<char, project::PROJECT_SLUG_SIZE + 1U> tooLong{};
    for (size_t i = 0; i < project::PROJECT_SLUG_MAX_LENGTH + 1U; ++i) {
        tooLong[i] = 'a';
    }
    tooLong[project::PROJECT_SLUG_MAX_LENGTH + 1U] = '\0';
    assert(!project::validProjectSlug(tooLong.data()));
}

void testWindowsReservedNamesAreRejected() {
    assert(!project::validProjectSlug("con"));
    assert(!project::validProjectSlug("CON"));
    assert(!project::validProjectSlug("con.project"));
    assert(!project::validProjectSlug("prn"));
    assert(!project::validProjectSlug("aux"));
    assert(!project::validProjectSlug("nul"));
    assert(!project::validProjectSlug("com1"));
    assert(!project::validProjectSlug("COM1"));
    assert(!project::validProjectSlug("com9.project"));
    assert(!project::validProjectSlug("lpt1"));
    assert(!project::validProjectSlug("LPT1"));
    assert(!project::validProjectSlug("lpt9.backup"));

    assert(project::validProjectSlug("conga"));
    assert(project::validProjectSlug("Con Project"));
    assert(project::validProjectSlug("com10"));
    assert(project::validProjectSlug("lpt10"));
    assert(project::validProjectSlug("my.con"));
}

void testAssignProjectSlugMirrorsIdentityAndName() {
    core::state::project::ProjectMetadata metadata{};
    assert(project::assignProjectSlug(metadata, "p042"));
    assert(std::strcmp(metadata.id.data(), "p042") == 0);
    assert(std::strcmp(metadata.name.data(), "p042") == 0);

    assert(project::assignProjectSlug(metadata, "P042 Set"));
    assert(std::strcmp(metadata.id.data(), "P042 Set") == 0);
    assert(std::strcmp(metadata.name.data(), "P042 Set") == 0);

    assert(!project::assignProjectSlug(metadata, "P042 Set "));
    assert(std::strcmp(metadata.id.data(), "P042 Set") == 0);
    assert(std::strcmp(metadata.name.data(), "P042 Set") == 0);
}

void testGeneratedSlug() {
    char out[project::PROJECT_SLUG_SIZE] = {};
    assert(project::formatGeneratedProjectSlug(1, out, sizeof(out)));
    assert(std::strcmp(out, "p001") == 0);

    assert(project::formatGeneratedProjectSlug(999, out, sizeof(out)));
    assert(std::strcmp(out, "p999") == 0);

    assert(!project::formatGeneratedProjectSlug(0, out, sizeof(out)));
    assert(!project::formatGeneratedProjectSlug(1000, out, sizeof(out)));

    char tooSmall[4] = {};
    assert(!project::formatGeneratedProjectSlug(1, tooSmall, sizeof(tooSmall)));
}

}  // namespace

int main() {
    testValidSlugCharacters();
    testSlugValidation();
    testSlugLengthFollowsFilesystemSafeProjectNameLimit();
    testWindowsReservedNamesAreRejected();
    testAssignProjectSlugMirrorsIdentityAndName();
    testGeneratedSlug();
    return 0;
}
