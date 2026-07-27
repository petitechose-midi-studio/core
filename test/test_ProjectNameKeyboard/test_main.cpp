#include <cassert>
#include <cstring>
#include <iostream>

#include "../../src/state/project/ProjectNameKeyboard.hpp"

namespace {

using core::state::project::PROJECT_NAME_KEYBOARD_DEFAULT_INDEX;
using core::state::project::projectNameKeyboardCellAt;
using core::state::project::projectNameKeyboardCellCount;
using core::state::project::projectNameKeyboardMoveColumn;
using core::state::project::projectNameKeyboardMoveRow;

void test_default_key_starts_on_q() {
    const auto& cell = projectNameKeyboardCellAt(PROJECT_NAME_KEYBOARD_DEFAULT_INDEX);

    assert(std::strcmp(cell.label, "q") == 0);
    assert(cell.character == 'q');
    assert(cell.row == 1);
    assert(cell.column == 0);

    std::cout << "[PASS] test_default_key_starts_on_q\n";
}

void test_horizontal_navigation_follows_reading_order() {
    auto index = PROJECT_NAME_KEYBOARD_DEFAULT_INDEX;

    index = projectNameKeyboardMoveColumn(index, 1);
    assert(std::strcmp(projectNameKeyboardCellAt(index).label, "w") == 0);

    index = projectNameKeyboardMoveColumn(PROJECT_NAME_KEYBOARD_DEFAULT_INDEX, -1);
    assert(std::strcmp(projectNameKeyboardCellAt(index).label, "0") == 0);

    index = projectNameKeyboardMoveColumn(index, 1);
    assert(std::strcmp(projectNameKeyboardCellAt(index).label, "q") == 0);

    index = projectNameKeyboardMoveColumn(index, 10);
    assert(std::strcmp(projectNameKeyboardCellAt(index).label, "a") == 0);

    std::cout << "[PASS] test_horizontal_navigation_follows_reading_order\n";
}

void test_horizontal_navigation_wraps_between_keyboard_ends() {
    uint8_t index = 0;

    index = projectNameKeyboardMoveColumn(index, -1);
    assert(std::strcmp(projectNameKeyboardCellAt(index).label, "SPC") == 0);
    assert(projectNameKeyboardCellAt(index).character == ' ');

    index = projectNameKeyboardMoveColumn(index, 1);
    assert(std::strcmp(projectNameKeyboardCellAt(index).label, "1") == 0);

    std::cout << "[PASS] test_horizontal_navigation_wraps_between_keyboard_ends\n";
}

void test_vertical_navigation_keeps_nearest_column() {
    uint8_t index = PROJECT_NAME_KEYBOARD_DEFAULT_INDEX;  // q

    index = projectNameKeyboardMoveColumn(index, 4);
    assert(std::strcmp(projectNameKeyboardCellAt(index).label, "t") == 0);

    index = projectNameKeyboardMoveRow(index, 1);
    assert(std::strcmp(projectNameKeyboardCellAt(index).label, "g") == 0);

    index = projectNameKeyboardMoveRow(index, 1);
    assert(std::strcmp(projectNameKeyboardCellAt(index).label, "b") == 0);

    index = projectNameKeyboardMoveRow(index, 1);
    assert(std::strcmp(projectNameKeyboardCellAt(index).label, "5") == 0);

    std::cout << "[PASS] test_vertical_navigation_keeps_nearest_column\n";
}

void test_vertical_navigation_wraps_between_top_and_bottom() {
    uint8_t index = 0;  // 1

    index = projectNameKeyboardMoveRow(index, -1);
    assert(std::strcmp(projectNameKeyboardCellAt(index).label, "z") == 0);

    index = projectNameKeyboardMoveRow(index, 1);
    assert(std::strcmp(projectNameKeyboardCellAt(index).label, "1") == 0);

    std::cout << "[PASS] test_vertical_navigation_wraps_between_top_and_bottom\n";
}

void test_character_grid_includes_space_without_button_binding() {
    const auto& first = projectNameKeyboardCellAt(0);
    assert(std::strcmp(first.label, "1") == 0);
    assert(first.character == '1');
    assert(first.row == 0);
    assert(first.column == 0);

    const auto& last = projectNameKeyboardCellAt(projectNameKeyboardCellCount() - 1U);
    assert(std::strcmp(last.label, "SPC") == 0);
    assert(last.character == ' ');
    assert(last.row == 3);
    assert(last.column == 9);

    std::cout << "[PASS] test_character_grid_includes_space_without_button_binding\n";
}

void test_invalid_index_falls_back_to_default_key() {
    const auto& cell = projectNameKeyboardCellAt(projectNameKeyboardCellCount());

    assert(std::strcmp(cell.label, "q") == 0);
    assert(cell.character == 'q');

    std::cout << "[PASS] test_invalid_index_falls_back_to_default_key\n";
}

}  // namespace

int main() {
    test_default_key_starts_on_q();
    test_horizontal_navigation_follows_reading_order();
    test_horizontal_navigation_wraps_between_keyboard_ends();
    test_vertical_navigation_keeps_nearest_column();
    test_vertical_navigation_wraps_between_top_and_bottom();
    test_character_grid_includes_space_without_button_binding();
    test_invalid_index_falls_back_to_default_key();

    std::cout << "\nAll ProjectNameKeyboard tests passed.\n";
    return 0;
}
