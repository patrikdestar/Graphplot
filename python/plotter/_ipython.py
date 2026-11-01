# Copyright (c) 2026 Justinas Minkevicius. All rights reserved. See LICENSE.
"""IPython integration: enables ``%gui plotter``.

IPython's terminal (prompt_toolkit) does not use PyOS_InputHook; it has its
own input-hook registry. The hook below runs the Qt event loop until the
prompt has input.
"""


def _inputhook(context):
    from ._plotter import _has_app, process_events

    if not _has_app():
        return
    while not context.input_is_ready():
        process_events(15)


def register():
    try:
        from IPython.terminal.pt_inputhooks import register as _register
    except ImportError:
        return
    _register("plotter", _inputhook)
