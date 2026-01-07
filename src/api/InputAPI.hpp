#pragma once

/**
 * @file InputAPI.hpp
 * @brief Facade combining encoder and button APIs for input handling
 *
 * Groups EncoderAPI and ButtonAPI references to reduce handler parameter count.
 * Handlers that need both inputs can accept a single InputAPI& instead of two
 * separate API references.
 */

#include <oc/api/ButtonAPI.hpp>
#include <oc/api/EncoderAPI.hpp>

namespace core::api {

/**
 * @brief Facade combining encoder and button APIs for input handling
 *
 * Usage in handlers:
 * @code
 * class MyHandler {
 * public:
 *     MyHandler(InputAPI& input) : input_(input) {
 *         input_.encoders.bind(...);
 *         input_.buttons.bind(...);
 *     }
 * private:
 *     InputAPI& input_;
 * };
 * @endcode
 */
struct InputAPI {
    oc::api::EncoderAPI& encoders;
    oc::api::ButtonAPI& buttons;
};

}  // namespace core::api
