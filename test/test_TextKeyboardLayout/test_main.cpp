#include <cassert>
#include <cstring>
#include <iostream>

#include "../../src/state/interaction/TextKeyboardLayout.hpp"

namespace {

using core::state::interaction::TEXT_KEYBOARD_DEFAULT_INDEX;
using core::state::interaction::textKeyboardCellAt;
using core::state::interaction::textKeyboardCellCount;
using core::state::interaction::textKeyboardCharacterAt;
using core::state::interaction::textKeyboardAppend;
using core::state::interaction::textKeyboardBackspace;
using core::state::interaction::textKeyboardClear;
using core::state::interaction::textKeyboardMoveColumn;
using core::state::interaction::textKeyboardMoveRow;

void test_default_key_starts_on_q() {
    const auto& cell = textKeyboardCellAt(TEXT_KEYBOARD_DEFAULT_INDEX);

    assert(std::strcmp(cell.label, "q") == 0);
    assert(cell.character == 'q');
    assert(cell.row == 1);
    assert(cell.column == 0);

    std::cout << "[PASS] test_default_key_starts_on_q\n";
}

void test_horizontal_navigation_follows_reading_order() {
    auto index = TEXT_KEYBOARD_DEFAULT_INDEX;

    index = textKeyboardMoveColumn(index, 1);
    assert(std::strcmp(textKeyboardCellAt(index).label, "w") == 0);

    index = textKeyboardMoveColumn(TEXT_KEYBOARD_DEFAULT_INDEX, -1);
    assert(std::strcmp(textKeyboardCellAt(index).label, "0") == 0);

    index = textKeyboardMoveColumn(index, 1);
    assert(std::strcmp(textKeyboardCellAt(index).label, "q") == 0);

    index = textKeyboardMoveColumn(index, 10);
    assert(std::strcmp(textKeyboardCellAt(index).label, "a") == 0);

    std::cout << "[PASS] test_horizontal_navigation_follows_reading_order\n";
}

void test_horizontal_navigation_wraps_between_keyboard_ends() {
    uint8_t index = 0;

    index = textKeyboardMoveColumn(index, -1);
    assert(std::strcmp(textKeyboardCellAt(index).label, "SPC") == 0);
    assert(textKeyboardCellAt(index).character == ' ');

    index = textKeyboardMoveColumn(index, 1);
    assert(std::strcmp(textKeyboardCellAt(index).label, "1") == 0);

    std::cout << "[PASS] test_horizontal_navigation_wraps_between_keyboard_ends\n";
}

void test_vertical_navigation_keeps_nearest_column() {
    uint8_t index = TEXT_KEYBOARD_DEFAULT_INDEX;  // q

    index = textKeyboardMoveColumn(index, 4);
    assert(std::strcmp(textKeyboardCellAt(index).label, "t") == 0);

    index = textKeyboardMoveRow(index, 1);
    assert(std::strcmp(textKeyboardCellAt(index).label, "g") == 0);

    index = textKeyboardMoveRow(index, 1);
    assert(std::strcmp(textKeyboardCellAt(index).label, "b") == 0);

    index = textKeyboardMoveRow(index, 1);
    assert(std::strcmp(textKeyboardCellAt(index).label, "5") == 0);

    std::cout << "[PASS] test_vertical_navigation_keeps_nearest_column\n";
}

void test_vertical_navigation_wraps_between_top_and_bottom() {
    uint8_t index = 0;  // 1

    index = textKeyboardMoveRow(index, -1);
    assert(std::strcmp(textKeyboardCellAt(index).label, "z") == 0);

    index = textKeyboardMoveRow(index, 1);
    assert(std::strcmp(textKeyboardCellAt(index).label, "1") == 0);

    std::cout << "[PASS] test_vertical_navigation_wraps_between_top_and_bottom\n";
}

void test_character_grid_includes_space_without_button_binding() {
    const auto& first = textKeyboardCellAt(0);
    assert(std::strcmp(first.label, "1") == 0);
    assert(first.character == '1');
    assert(first.row == 0);
    assert(first.column == 0);

    const auto& last = textKeyboardCellAt(textKeyboardCellCount() - 1U);
    assert(std::strcmp(last.label, "SPC") == 0);
    assert(last.character == ' ');
    assert(last.row == 3);
    assert(last.column == 9);

    std::cout << "[PASS] test_character_grid_includes_space_without_button_binding\n";
}

void test_invalid_index_falls_back_to_default_key() {
    const auto& cell = textKeyboardCellAt(textKeyboardCellCount());

    assert(std::strcmp(cell.label, "q") == 0);
    assert(cell.character == 'q');

    std::cout << "[PASS] test_invalid_index_falls_back_to_default_key\n";
}

void test_shared_text_editing_is_bounded() {
    char text[4] = "ab";
    assert(textKeyboardCharacterAt(TEXT_KEYBOARD_DEFAULT_INDEX, true) == 'Q');
    assert(textKeyboardAppend(text, sizeof(text), 'c'));
    assert(std::strcmp(text, "abc") == 0);
    assert(!textKeyboardAppend(text, sizeof(text), 'd'));
    assert(textKeyboardBackspace(text));
    assert(std::strcmp(text, "ab") == 0);
    assert(textKeyboardClear(text));
    assert(std::strcmp(text, "") == 0);
    assert(!textKeyboardBackspace(text));

    std::cout << "[PASS] test_shared_text_editing_is_bounded\n";
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
    test_shared_text_editing_is_bounded();

    std::cout << "\nAll TextKeyboardLayout tests passed.\n";
    return 0;
}
