#pragma once

namespace core::state {
struct CoreState;
}

namespace core::handler {

class ProjectLifecycleDomainServices {
public:
    using CommandFn = bool (*)(void* context);

    struct Operations {
        void* context = nullptr;
        CommandFn resetMusicalProject = nullptr;
    };

    ProjectLifecycleDomainServices() = default;
    explicit ProjectLifecycleDomainServices(Operations operations);
    static ProjectLifecycleDomainServices fromCoreState(core::state::CoreState& state);

    bool resetMusicalProject() const;

private:
    Operations operations_{};
};

}  // namespace core::handler
