#include "handler/project/ProjectHandlerInternals.hpp"
#include <oc/log/Log.hpp>

namespace core::handler {

using namespace project_handler_internal;

FLASHMEM bool ProjectHandler::loadProjectWithFeedback(const char* projectId) {
    const auto result = lifecycle_.loadProject(projectId);
    char feedback[32] = {};
    formatProjectLifecycleFeedback(
        feedback,
        sizeof(feedback),
        projectLoadFeedbackLabel(result),
        projectId
    );
    navigation_.setLifecycleFeedback(feedback);
    if (result.success()) {
        OC_LOG_INFO("[Project] load {} bytes={}", projectId, result.bytes);
    } else {
        OC_LOG_WARN("[Project] load {} failed status={}",
                    projectId,
                    static_cast<unsigned>(result.status));
    }
    return result.success();
}

FLASHMEM bool ProjectHandler::saveCurrentAndLoadProjectWithFeedback(const char* projectId) {
    const char* currentProjectId = lifecycle_.currentProjectId();
    const auto saved = lifecycle_.saveCurrentProject();
    if (!saved.success()) {
        char feedback[32] = {};
        formatProjectLifecycleFeedback(
            feedback,
            sizeof(feedback),
            projectLifecycleFailureLabel(saved.status, "Save failed"),
            currentProjectId
        );
        navigation_.setLifecycleFeedback(feedback);
        OC_LOG_WARN("[Project] save {} before load failed status={}",
                    currentProjectId,
                    static_cast<unsigned>(saved.status));
        return false;
    }

    OC_LOG_INFO("[Project] save {} before load bytes={}", currentProjectId, saved.bytes);
    return loadProjectWithFeedback(projectId);
}

FLASHMEM bool ProjectHandler::saveAsAndLoadProjectWithFeedback(const char* projectId) {
    const auto saved = lifecycle_.saveAsNextProject();
    const char* savedProjectId = lifecycle_.currentProjectId();
    if (!saved.success()) {
        char feedback[32] = {};
        formatProjectLifecycleFeedback(
            feedback,
            sizeof(feedback),
            projectLifecycleFailureLabel(saved.status, "Save As failed"),
            savedProjectId
        );
        navigation_.setLifecycleFeedback(feedback);
        OC_LOG_WARN("[Project] save-as before load failed status={}",
                    static_cast<unsigned>(saved.status));
        return false;
    }

    OC_LOG_INFO("[Project] save-as {} before load bytes={}", savedProjectId, saved.bytes);
    return loadProjectWithFeedback(projectId);
}

FLASHMEM bool ProjectHandler::saveAndResetProjectWithFeedback(bool saveAsNew) {
    const auto saved = saveAsNew
        ? lifecycle_.saveAsNextProject()
        : lifecycle_.saveCurrentProject();
    const char* savedProjectId = lifecycle_.currentProjectId();
    char feedback[32] = {};
    const char* verb = saved.success()
        ? "Saved"
        : projectLifecycleFailureLabel(saved.status, saveAsNew ? "Save As failed" : "Save failed");
    formatProjectLifecycleFeedback(feedback, sizeof(feedback), verb, savedProjectId);

    if (!saved.success()) {
        navigation_.setLifecycleFeedback(feedback);
        OC_LOG_WARN("[Project] save before reset failed status={}",
                    static_cast<unsigned>(saved.status));
        return false;
    }

    OC_LOG_INFO("[Project] save {} before reset bytes={}", savedProjectId, saved.bytes);
    resetProject();
    navigation_.setLifecycleFeedback(feedback);
    return true;
}

FLASHMEM bool ProjectHandler::commitProjectNameEditor() {
    const auto node = navigation_.currentNode.get();
    if (!isProjectNameEditorNode(node)) return false;

    const char* slug = navigation_.editingProjectSlug.data();
    if (!core::state::project::validProjectSlug(slug)) {
        navigation_.setLifecycleFeedback("Invalid name");
        return true;
    }

    const bool rename = node == core::state::project::ProjectNodeId::RENAME_PROJECT_NAME;
    const auto result = rename
        ? lifecycle_.renameCurrentProject(slug)
        : lifecycle_.saveAsProject(slug);

    char feedback[32] = {};
    const char* verb = result.success()
        ? (rename ? "Renamed" : "Saved")
        : projectLifecycleFailureLabel(result.status, rename ? "Rename failed" : "Save As failed");
    formatProjectLifecycleFeedback(feedback, sizeof(feedback), verb, slug);
    navigation_.setLifecycleFeedback(feedback);

    if (!result.success()) {
        OC_LOG_WARN("[Project] {} {} failed status={}",
                    rename ? "rename" : "save-as",
                    slug,
                    static_cast<unsigned>(result.status));
        return true;
    }

    OC_LOG_INFO("[Project] {} {} bytes={}",
                rename ? "rename" : "save-as",
                slug,
                result.bytes);
    back();
    return true;
}

FLASHMEM bool ProjectHandler::activateFocusedProjectAction() {
    using core::state::project::ProjectNodeId;

    const auto node = navigation_.currentNode.get();
    const uint8_t row = navigation_.focusedRow.get();
    if (isProjectNameEditorNode(node)) {
        if (!appendProjectNameKey(navigation_)) {
            navigation_.setLifecycleFeedback("Name too long");
        }
        return true;
    }

    if (node == ProjectNodeId::NEW_PROJECT_CONFIRM) {
        if (row == 0) {
            return saveAndResetProjectWithFeedback(
                !lifecycle_.currentProjectHasSavedIdentity() ||
                    !lifecycle_.currentProjectOverwriteSafe()
            );
        }
        if (row == 1) {
            resetProject();
            return true;
        }
        if (row == 2) {
            back();
            return true;
        }
        return false;
    }

    if (node == ProjectNodeId::LOAD_PROJECT) {
        if (row >= navigation_.loadProjects.count) return false;
        const char* projectId = navigation_.loadProjects.entries[row].id.data();
        if (lifecycle_.currentProjectDirty()) {
            core::state::project::openProjectLoadConfirmation(
                navigation_,
                projectId,
                lifecycle_.currentProjectHasSavedIdentity() &&
                    lifecycle_.currentProjectOverwriteSafe()
            );
            return true;
        }
        loadProjectWithFeedback(projectId);
        return true;
    }

    if (node == ProjectNodeId::LOAD_PROJECT_CONFIRM) {
        const char* projectId = navigation_.pendingLoadProjectId.data();
        if (row == 0) {
            const bool loaded = navigation_.pendingLoadCanSaveCurrent
                ? saveCurrentAndLoadProjectWithFeedback(projectId)
                : saveAsAndLoadProjectWithFeedback(projectId);
            if (loaded) {
                back();
            }
            return true;
        }
        if (row == 1) {
            if (navigation_.pendingLoadCanSaveCurrent) {
                if (saveAsAndLoadProjectWithFeedback(projectId)) {
                    back();
                }
                return true;
            }
            if (loadProjectWithFeedback(projectId)) {
                back();
            }
            return true;
        }
        if (row == 2) {
            if (navigation_.pendingLoadCanSaveCurrent) {
                if (loadProjectWithFeedback(projectId)) {
                    back();
                }
                return true;
            }
            back();
            return true;
        }
        if (row == 3) {
            if (!navigation_.pendingLoadCanSaveCurrent) return false;
            back();
            return true;
        }
        return true;
    }

    const bool newProjectAction =
        (node == ProjectNodeId::OVERVIEW_ROOT && row == 0) ||
        (node == ProjectNodeId::STORAGE_ROOT && row == 3);
    if (newProjectAction) {
        core::state::project::openNewProjectConfirmation(navigation_);
        return true;
    }

    const bool loadProjectAction =
        (node == ProjectNodeId::OVERVIEW_ROOT && row == 1) ||
        (node == ProjectNodeId::STORAGE_ROOT && row == 4);
    if (loadProjectAction) {
        navigation_.clearLifecycleFeedback();
        const auto result = lifecycle_.refreshLoadableProjects();
        if (!result.success()) {
            navigation_.setLifecycleFeedback(projectLifecycleFailureLabel(result.status, "List failed"));
            OC_LOG_WARN("[Project] list projects failed status={}",
                        static_cast<unsigned>(result.status));
            return true;
        }
        core::state::project::openProjectLoadPicker(navigation_);
        if (navigation_.loadProjects.count == 0) {
            navigation_.setLifecycleFeedback("No projects");
        } else {
            OC_LOG_INFO("[Project] list projects count={} truncated={}",
                        static_cast<unsigned>(navigation_.loadProjects.count),
                        navigation_.loadProjects.truncated ? 1 : 0);
        }
        return true;
    }

    const bool saveProjectAction =
        (node == ProjectNodeId::OVERVIEW_ROOT && row == 2) ||
        (node == ProjectNodeId::STORAGE_ROOT && row == 0);
    if (saveProjectAction) {
        const auto result = lifecycle_.saveCurrentProject();
        const char* projectId = lifecycle_.currentProjectId();
        char feedback[32] = {};
        const char* verb = result.success()
            ? "Saved"
            : projectLifecycleFailureLabel(result.status, "Save failed");
        formatProjectLifecycleFeedback(feedback, sizeof(feedback), verb, projectId);
        navigation_.setLifecycleFeedback(feedback);
        if (result.success()) {
            OC_LOG_INFO("[Project] save {} bytes={}", projectId, result.bytes);
        } else {
            OC_LOG_WARN("[Project] save {} failed status={}",
                        projectId,
                        static_cast<unsigned>(result.status));
        }
        return true;
    }

    const bool saveAsProjectAction =
        (node == ProjectNodeId::OVERVIEW_ROOT && row == 3) ||
        (node == ProjectNodeId::STORAGE_ROOT && row == 1);
    if (saveAsProjectAction) {
        navigation_.clearLifecycleFeedback();
        core::state::project::openProjectNameEditor(
            navigation_,
            ProjectNodeId::SAVE_AS_PROJECT_NAME,
            ""
        );
        return true;
    }

    const bool renameProjectAction =
        (node == ProjectNodeId::OVERVIEW_ROOT && row == 4) ||
        (node == ProjectNodeId::STORAGE_ROOT && row == 2);
    if (renameProjectAction) {
        navigation_.clearLifecycleFeedback();
        core::state::project::openProjectNameEditor(
            navigation_,
            ProjectNodeId::RENAME_PROJECT_NAME,
            lifecycle_.currentProjectHasSavedIdentity() ? lifecycle_.currentProjectId() : ""
        );
        return true;
    }

    return false;
}


}  // namespace core::handler
