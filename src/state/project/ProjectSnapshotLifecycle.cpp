#include "state/project/ProjectSnapshot.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::project {

ProjectSnapshot::ProjectSnapshot() = default;
ProjectSnapshot::~ProjectSnapshot() = default;
ProjectSnapshot::ProjectSnapshot(ProjectSnapshot&&) noexcept = default;
ProjectSnapshot& ProjectSnapshot::operator=(ProjectSnapshot&&) noexcept = default;

}  // namespace core::state::project
