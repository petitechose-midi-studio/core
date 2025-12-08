#pragma once

#include "IComponent.hpp"

#include <string>

/**
 * Interface for list selector components.
 * Provides navigation, selection and visibility control.
 */
class ISelector : public IComponent {
public:
    virtual ~ISelector() = default;

    virtual void setTitle(const std::string &title) = 0;
    virtual void setSelectedIndex(int index) = 0;
    virtual int getSelectedIndex() const = 0;
    virtual int getItemCount() const = 0;
};
