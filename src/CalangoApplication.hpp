#pragma once

// QApplication that understands the OS "open this document" protocol.
//
// Finder (macOS) never passes a double-clicked file on argv: Launch Services
// delivers it as an Apple Event that Qt surfaces as a QFileOpenEvent — both
// when the double-click launches the app and when the app is already
// running (or a file is dropped on the Dock icon). Multiple files selected
// together arrive as separate QFileOpenEvents in quick succession, each
// handled independently below, so each still opens in its own tab. The
// event can therefore arrive before the main window exists, so paths are
// queued (FileOpenQueue) until a handler is attached. On Linux/Windows the
// file-manager association passes the path on argv instead, which main()
// already handles; FileOpen events simply never occur there, making this
// class harmless cross-platform.
//
// Extracted out of main.cpp into its own header, and generalized from
// "attach a MainWindow*" to "attach an open-path callback", so a test can
// construct one directly and synthesize a QFileOpenEvent without linking
// MainWindow (untested, ~9.5k lines) into the test binary.

#include "FileOpenQueue.hpp"

#include <QApplication>
#include <QEvent>
#include <QFileOpenEvent>

namespace calango {

class CalangoApplication : public QApplication {
public:
    using QApplication::QApplication;

    /// Attach the callback that opens a path — MainWindow::loadFile in
    /// production. Drains any FileOpen that arrived before this call.
    void attachHandler(FileOpenQueue::Handler handler)
    {
        queue_.attachHandler(std::move(handler));
    }

    /// True once any document arrived from the OS — used to keep the welcome
    /// screen from opening on top of a launch-by-double-click.
    bool receivedFileOpen() const { return queue_.receivedAny(); }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::FileOpen) {
            queue_.route(static_cast<QFileOpenEvent*>(event)->file());
            return true;
        }
        return QApplication::event(event);
    }

private:
    FileOpenQueue queue_;
};

} // namespace calango
