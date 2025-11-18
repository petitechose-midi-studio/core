#pragma once

#include "IElement.hpp"

namespace UI {

/**
 * @brief Interface for atomic UI widgets
 *
 * Represents unit UI elements that are typically always visible
 * once created (labels, progress bars, status indicators, etc.).
 *
 * Widgets don't have explicit show/hide semantics - their visibility
 * is managed by their parent container or view.
 *
 * Examples:
 * - DeviceStateBar (always visible in DeviceView)
 * - Parameter labels
 * - Status indicators
 */
class IWidget : public IElement {
public:
    // Inherits only getElement() from IElement
    // No additional methods - widgets are simple, always-visible elements
};

}  // namespace UI
