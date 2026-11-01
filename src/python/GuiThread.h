// Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
// Qt <-> Python threading glue.
//
// Rules this module enforces:
//  * The QApplication and every widget live in the thread that created the
//    first figure, which must be Python's main thread (macOS requires GUI
//    work on the process main thread).
//  * Any binding call made from another Python thread is marshalled to the
//    GUI thread with a blocking queued call, with the GIL released while
//    waiting, so the GUI thread can run Python callbacks meanwhile.
//  * The Qt event loop runs (a) inside PyOS_InputHook while the interactive
//    prompt waits for input, (b) in show(block=True)/pause()/wait_*() with
//    the GIL released. Python callbacks re-acquire the GIL.
#pragma once

#include <pybind11/pybind11.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <exception>
#include <QHash>
#include <functional>
#include <utility>
#include <optional>
#include <type_traits>

namespace gp::pyglue {

// Creates the QApplication on first use (main thread only) and installs the
// interactive input hook.
void ensureApp();
bool isGuiThread();

// Runs Qt's event loop for `ms` milliseconds (GIL released); Ctrl+C aware.
void processEvents(int ms);

// Runs a nested event loop until `done()` returns true, the timeout expires
// (timeoutMs < 0: no timeout) or Ctrl+C is pressed (raises KeyboardInterrupt).
// Returns false on timeout.
bool runEventLoopUntil(const std::function<bool()>& done, int timeoutMs);

// Per-thread call context, carried into the GUI-thread task of every call
// made from that thread: which panel each figure's calls target (set by
// plotter.Axes for the duration of one call). Being per calling thread, it
// needs no lock and a user click cannot redirect a call.
struct CallContext {
    QHash<const void*, int> panelTargets;  // figure -> panel index
};
CallContext& callContext();  // of the current thread

// Executes f on the GUI thread and returns its result. f must not touch
// Python objects; exceptions propagate to the caller.
// Runs `task` on the GUI thread and waits for it, with the GIL released so
// the GUI thread can run Python callbacks meanwhile. Throws if there is no
// application or the call could not be delivered.
void runOnGuiThreadBlocking(const std::function<void()>& task);

template <typename F>
auto inGui(F&& f) -> std::invoke_result_t<F>
{
    using R = std::invoke_result_t<F>;
    if (isGuiThread()) return f();  // the common case: called from the main thread
    std::exception_ptr err;
    std::optional<std::conditional_t<std::is_void_v<R>, int, R>> result;
    const CallContext caller = callContext();
    runOnGuiThreadBlocking([&] {
        // The caller's context applies while its task runs on the GUI thread.
        const CallContext saved = std::exchange(callContext(), caller);
        struct Restore {
            const CallContext& saved;
            ~Restore() { callContext() = saved; }
        } restore{saved};
        try {
            if constexpr (std::is_void_v<R>) {
                f();
                result.emplace(0);
            } else {
                result.emplace(f());
            }
        } catch (...) {
            err = std::current_exception();
        }
    });
    if (err) std::rethrow_exception(err);
    if constexpr (!std::is_void_v<R>) return std::move(*result);
}

}  // namespace gp::pyglue
