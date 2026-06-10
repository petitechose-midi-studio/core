#pragma once

#include <cstddef>
#include <cstdint>

#include "state/project/ProjectState.hpp"

namespace core::state::project {

inline constexpr const char* DEFAULT_UNSAVED_PROJECT_SLUG = "untitled";
inline constexpr const char* PROJECT_FILE_EXTENSION = ".mspj";
inline constexpr const char* PROJECT_BACKUP_FILE_SUFFIX = ".mspj.bak";
inline constexpr const char* PROJECT_TEMP_FILE_SUFFIX = ".mspj.tmp";
inline constexpr uint8_t PROJECT_FILE_EXTENSION_LENGTH = sizeof(".mspj") - 1U;
inline constexpr uint8_t PROJECT_BACKUP_FILE_SUFFIX_LENGTH = sizeof(".mspj.bak") - 1U;
inline constexpr uint8_t PROJECT_TEMP_FILE_SUFFIX_LENGTH = sizeof(".mspj.tmp") - 1U;
inline constexpr uint8_t PROJECT_SLUG_SIZE = ProjectMetadata::ID_SIZE;
inline constexpr uint8_t PROJECT_SLUG_MAX_LENGTH = PROJECT_SLUG_SIZE - 1U;

bool isProjectSlugChar(char c);
bool validProjectSlug(const char* slug);
bool assignProjectSlug(ProjectMetadata& metadata, const char* slug);
bool formatGeneratedProjectSlug(uint16_t index, char* out, size_t outSize);

}  // namespace core::state::project
