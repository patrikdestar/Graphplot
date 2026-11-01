#include "GuiThread.h"

#include "model/Style.h"

#include <QApplication>
#include <QEventLoop>
#include <QTimer>

#ifdef _WIN32
#include <conio.h>
#else
#include <QSocketNotifier>
#include <unistd.h>
#endif

namespace py = pybind11;

namespace gp::pyglue {

namespace {

// PyOS_InputHook is called by the interactive prompt (readline, the classic
// REPL and the new 3.13+ REPL) while it waits for keyboard input, *with the
// GIL released*. We run Qt's event loop until stdin becomes readable, so
// figures stay fully interactive while the user types.
int inputHook()
{
    if (!QCoreApplication::instance()) return 0;
#ifdef _WIN32
    while (!_kbhit()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
#else
    QEventLoop loop;
    QSocketNotifier notifier(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&notifier, &QSocketNotifier::activated, &loop, &QEventLoop::quit);
    loop.exec();
#endif
    return 0;
}

int s_argc = 1;
char s_arg0[] = "plotter";
char* s_argv[] = {s_arg0, nullptr};

}  // namespace

bool isGuiThread()
{
    auto* app = QCoreApplication::instance();
    return app && QThread::currentThread() == app->thread();
}

void ensureApp()
{
    if (QCoreApplication::instance()) {
        if (!qobject_cast<QApplication*>(QCoreApplication::instance()))
            throw std::runtime_error("A non-widget QCoreApplication already exists");
        return;
    }
    // Must be Python's main thread (see header).
    py::module_ threading = py::module_::import("threading");
    if (!threading.attr("current_thread")().is(threading.attr("main_thread")()))
        throw std::runtime_error(
            "The first plotter figure must be created from Python's main thread");
    auto* app = new QApplication(s_argc, s_argv);  // lives until process exit
    app->setQuitOnLastWindowClosed(false);
    QApplication::setApplicationName("plotter");
    if (!PyOS_InputHook) PyOS_InputHook = inputHook;
}

bool runEventLoopUntil(const std::function<bool()>& done, int timeoutMs)
{
    if (!QCoreApplication::instance()) return true;
    if (!isGuiThread())
        throw std::runtime_error("The event loop can only be run from the GUI (main) thread");
    if (done()) return true;

    QEventLoop loop;
    bool timedOut = false;
    bool interrupted = false;
    QTimer poll;
    poll.setInterval(30);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (done()) {
            loop.quit();
            return;
        }
        // Let Python see Ctrl+C (signal handlers only run with the GIL).
        py::gil_scoped_acquire gil;
        if (PyErr_CheckSignals() != 0) {
            PyErr_Clear();
            interrupted = true;
            loop.quit();
        }
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    if (timeoutMs >= 0) {
        QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
            timedOut = true;
            loop.quit();
        });
        timeout.start(timeoutMs);
    }
    poll.start();
    {
        py::gil_scoped_release release;
        loop.exec();
    }
    if (interrupted) {
        PyErr_SetNone(PyExc_KeyboardInterrupt);
        throw py::error_already_set();
    }
    return !timedOut;
}

void processEvents(int ms)
{
    if (!QCoreApplication::instance()) return;
    runEventLoopUntil([] { return false; }, std::max(0, ms));
}

}  // namespace gp::pyglue
