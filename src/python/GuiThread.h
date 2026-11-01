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
#include <functional>
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

// Executes f on the GUI thread and returns its result. f must not touch
// Python objects; exceptions propagate to the caller.
template <typename F>
auto inGui(F&& f) -> std::invoke_result_t<F>
{
    using R = std::invoke_result_t<F>;
    if (isGuiThread()) return f();
    std::exception_ptr err;
    if constexpr (std::is_void_v<R>) {
        {
            pybind11::gil_scoped_release release;
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [&] {
                    try { f(); } catch (...) { err = std::current_exception(); }
                },
                Qt::BlockingQueuedConnection);
        }
        if (err) std::rethrow_exception(err);
    } else {
        std::optional<R> result;
        {
            pybind11::gil_scoped_release release;
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [&] {
                    try { result.emplace(f()); } catch (...) { err = std::current_exception(); }
                },
                Qt::BlockingQueuedConnection);
        }
        if (err) std::rethrow_exception(err);
        return std::move(*result);
    }
}

}  // namespace gp::pyglue
