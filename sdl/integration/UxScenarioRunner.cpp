#include "integration/UxScenarioRunner.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include <SDL2/SDL.h>
#include <oc/hal/sdl/InputMapper.hpp>
#include <oc/state/NotificationQueue.hpp>

#include "integration/UxInputIds.hpp"
#include "integration/UxReplayTimeline.hpp"
#include "integration/UxScriptParser.hpp"

namespace sdl::integration {

namespace {

std::string safeName(std::string name) {
    for (char& c : name) {
        const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
        if (!ok) c = '_';
    }
    return name.empty() ? "capture" : name;
}

std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

uint32_t elapsedSince(uint32_t start) {
    return SDL_GetTicks() - start;
}

bool pumpUntil(uint32_t dueMs,
               uint32_t start,
               ::sdl::SdlEnvironment& env,
               oc::app::OpenControlApp& app,
               core::state::CoreState& state,
               const UxScenarioRunner::StateTick& stateTick) {
    while (env.isRunning() && elapsedSince(start) < dueMs) {
        if (!env.processEvents()) return false;
        app.update();
        if (stateTick) {
            stateTick();
        } else {
            state.update();
        }
        env.refresh();

        const uint32_t elapsed = elapsedSince(start);
        const uint32_t remaining = dueMs > elapsed ? dueMs - elapsed : 0;
        SDL_Delay(static_cast<Uint32>(std::min<uint32_t>(16, std::max<uint32_t>(1, remaining))));
    }
    return env.isRunning();
}

void flushFrame(::sdl::SdlEnvironment& env,
                oc::app::OpenControlApp& app,
                core::state::CoreState& state,
                const UxScenarioRunner::StateTick& stateTick) {
    env.processEvents();
    app.update();
    if (stateTick) {
        stateTick();
    } else {
        state.update();
    }
    env.refresh();
}

}  // namespace

bool UxScenarioRunner::run(const UxRunOptions& options,
                           ::sdl::SdlEnvironment& env,
                           oc::app::OpenControlApp& app,
                           core::state::CoreState& state,
                           ScenarioApplier scenarioApplier,
                           StateTick stateTick) {
    error_.clear();
    if (!options.scriptPath || options.scriptPath[0] == '\0') {
        error_ = "missing UX script path";
        return false;
    }
    if (!options.outputDir || options.outputDir[0] == '\0') {
        error_ = "missing UX output directory";
        return false;
    }

    std::vector<UxAction> actions;
    UxScriptParser parser;
    if (!parser.load(options.scriptPath, actions)) {
        error_ = parser.error();
        return false;
    }

    std::filesystem::path outputDir(options.outputDir);
    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        error_ = "unable to create UX output directory: " + ec.message();
        return false;
    }

    std::ofstream trace(outputDir / "trace.ndjson");
    if (!trace) {
        error_ = "unable to create UX trace file";
        return false;
    }

    auto& notificationQueue = oc::state::NotificationQueue::instance();
    notificationQueue.flush();
    const size_t preRunOverflowCount = notificationQueue.overflowCount();
    trace << "{\"event\":\"run_start\",\"script\":\"" << jsonEscape(options.scriptPath)
          << "\",\"actions\":" << actions.size()
          << ",\"pre_run_notification_overflow_count\":" << preRunOverflowCount << "}\n";
    if (preRunOverflowCount != 0) {
        error_ = "NotificationQueue dropped " + std::to_string(preRunOverflowCount) +
                 " notification(s) before UX workflow start";
        return false;
    }
    notificationQueue.resetOverflowCount();

    const uint32_t start = SDL_GetTicks();
    UxReplayTimeline timeline;
    for (const UxAction& action : actions) {
        const uint32_t scheduledMs = timeline.schedule(action.dueMs);

        // A slow render may delay an action, but must never compress the next
        // gesture interval (notably button holds used to enter an overlay).
        if (!pumpUntil(scheduledMs, start, env, app, state, stateTick)) {
            error_ = "UX run stopped before action at " + std::to_string(action.dueMs) + "ms";
            return false;
        }

        const uint32_t actualMs = elapsedSince(start);
        timeline.record(action.dueMs, actualMs);
        std::string capturePath;

        switch (action.kind) {
            case UxActionKind::Button: {
                oc::type::ButtonID id = 0;
                if (!uxButtonIdFromName(action.id, id)) {
                    error_ = "line " + std::to_string(action.line) + ": unknown button `" + action.id + "`";
                    return false;
                }
                env.inputMapper().post(id, action.value == "down");
                break;
            }

            case UxActionKind::Encoder: {
                oc::type::EncoderID id = 0;
                if (!uxEncoderIdFromName(action.id, id)) {
                    error_ = "line " + std::to_string(action.line) + ": unknown encoder `" + action.id + "`";
                    return false;
                }
                env.inputMapper().post(id, action.amount);
                break;
            }

            case UxActionKind::Capture: {
                flushFrame(env, app, state, stateTick);
                const std::string fileName = std::to_string(action.dueMs) + "_" +
                    safeName(action.id) + "_" + uxScopeName(action.scope) + ".bmp";
                const std::filesystem::path path = outputDir / fileName;
                if (!env.saveScreenshotBmp(path.string().c_str(), action.scope)) {
                    error_ = "line " + std::to_string(action.line) + ": capture failed";
                    return false;
                }
                capturePath = path.string();
                break;
            }

            case UxActionKind::Scenario:
                if (!scenarioApplier || !scenarioApplier(action.id.c_str())) {
                    error_ = "line " + std::to_string(action.line) + ": scenario failed `" + action.id + "`";
                    return false;
                }
                break;

            case UxActionKind::Tick:
            default:
                flushFrame(env, app, state, stateTick);
                break;
        }

        trace << "{\"event\":\"action\",\"line\":" << action.line
              << ",\"due_ms\":" << action.dueMs
              << ",\"scheduled_ms\":" << scheduledMs
              << ",\"actual_ms\":" << actualMs
              << ",\"drift_ms\":" << static_cast<int32_t>(actualMs) - static_cast<int32_t>(action.dueMs)
              << ",\"action\":\"" << uxActionName(action.kind)
              << "\",\"id\":\"" << jsonEscape(action.id)
              << "\",\"value\":\"" << jsonEscape(action.value)
              << "\",\"scope\":\"" << uxScopeName(action.scope)
              << "\",\"active_view\":" << static_cast<int>(state.activeView.get())
              << ",\"playing\":" << (state.statusBar.playing.get() ? "true" : "false")
              << ",\"playhead_step\":" << state.sequencer.playheadStep.get()
              << ",\"sequencer_page\":" << static_cast<int>(state.sequencer.page.get())
              << ",\"capture\":\"" << jsonEscape(capturePath) << "\"}\n";
    }

    flushFrame(env, app, state, stateTick);
    // The state tick runs after app.update(), matching firmware order. Drain
    // the resulting reactive wave so a final callback-triggered overflow cannot
    // escape the workflow gate.
    notificationQueue.flush();
    const size_t notificationOverflowCount = notificationQueue.overflowCount();
    trace << "{\"event\":\"run_end\",\"actual_ms\":" << elapsedSince(start)
          << ",\"notification_overflow_count\":" << notificationOverflowCount << "}\n";
    if (notificationOverflowCount != 0) {
        error_ = "NotificationQueue dropped " + std::to_string(notificationOverflowCount) +
                 " notification(s) during UX workflow";
        return false;
    }
    return true;
}

}  // namespace sdl::integration
