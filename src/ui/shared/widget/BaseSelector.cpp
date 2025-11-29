#include "BaseSelector.hpp"

namespace UI
{

BaseSelector::BaseSelector(lv_obj_t *parent)
    : parent_(parent), overlay_(parent)
{
}

BaseSelector::~BaseSelector() = default;

void BaseSelector::setTitle(const std::string &title)
{
    overlay_.setTitle(title);
}

void BaseSelector::setSelectedIndex(int index)
{
    overlay_.setSelectedIndex(index);
}

int BaseSelector::getSelectedIndex() const
{
    return overlay_.getSelectedIndex();
}

int BaseSelector::getItemCount() const
{
    return overlay_.getItemCount();
}

void BaseSelector::show()
{
    overlay_.show();
}

void BaseSelector::hide()
{
    overlay_.hide();
}

bool BaseSelector::isVisible() const
{
    return overlay_.isVisible();
}

lv_obj_t *BaseSelector::getElement() const
{
    return overlay_.getElement();
}

} // namespace UI
