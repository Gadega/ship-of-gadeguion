#pragma once
namespace BS {
using priority_t = int;
namespace pr {
static const priority_t normal = 0;
} // namespace pr
class thread_pool {
  public:
    thread_pool() = default;
    explicit thread_pool(unsigned int) {}
    template <typename F> void detach_task(F&& fn) { fn(); }
    void wait() {}
    void pause() {}
    void unpause() {}
    void purge() {}
};
} // namespace BS
