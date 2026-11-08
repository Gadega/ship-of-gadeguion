# V46 Essential C++ Bridge

This phase adds nine real Shipwright C++ translation units that do not require
SDL, ImGui, JSON, spdlog, networking, or desktop filesystem services. They
provide the game-interactor hook surface, frame interpolation, actor metadata,
object extensions, item-category adjustment, and Fast3D command wrappers.

Three C++20 library calls were replaced with behavior-equivalent C++17 code for
PS3 GCC 7: `unordered_map::contains` uses `find`, `std::erase_if` uses an iterator
erase loop, and `std::bit_cast<float>` uses `memcpy`. These changes are portable
and do not introduce runtime stubs.

The script links the resulting C++ objects with the previously generated
696-source C core object. It remains a relocatable link, not an executable.
Desktop services and the actual PS3 window/RSX/audio/input/filesystem backends
remain separate work.
