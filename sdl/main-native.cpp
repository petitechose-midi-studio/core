/**
 * @file main-native.cpp
 * @brief Native entry point (Windows/Linux/macOS)
 */

#include "SdlRunner.hpp"

int main(int argc, char** argv) {
    SdlRunner runner;
    
    if (!runner.init(argc, argv)) {
        return 1;
    }

    while (runner.tick()) {}

    runner.shutdown();
    return 0;
}
