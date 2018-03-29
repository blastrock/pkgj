#pragma once

template <typename F>
struct ScopedExit {
    ScopedExit(F f) : f(f) {}
    ~ScopedExit() { f(); }
    F f;
};
