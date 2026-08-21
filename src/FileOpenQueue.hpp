#pragma once

// Queues paths delivered before a handler exists, then dispatches directly
// once one is attached.
//
// Extracted out of CalangoApplication (src/CalangoApplication.hpp) so the
// queueing/dispatch decision is unit-testable without instantiating a
// QApplication: only one QApplication/QCoreApplication may exist per
// process, which rules out constructing a second one inside a test binary
// that already runs under a Qt test harness's own QApplication. This class
// has no Qt-application dependency at all, so a test can exercise it
// directly.

#include <QString>
#include <QStringList>

#include <functional>
#include <utility>

namespace calango {

class FileOpenQueue {
public:
    using Handler = std::function<void(const QString&)>;

    /// Attach the handler that opens a path (MainWindow::loadFile in
    /// production, once the main window exists). Drains and dispatches, in
    /// arrival order, everything queued before this call.
    void attachHandler(Handler handler)
    {
        handler_ = std::move(handler);
        for (const QString& path : std::as_const(pending_))
            handler_(path);
        pending_.clear();
    }

    /// Route one path: dispatch immediately if a handler is already
    /// attached, otherwise queue it for the next attachHandler() call. An
    /// empty path is ignored — QFileOpenEvent::file() returning "" is not a
    /// document to open.
    void route(const QString& path)
    {
        if (path.isEmpty())
            return;
        receivedAny_ = true;
        if (handler_)
            handler_(path);
        else
            pending_.append(path);
    }

    /// True once any non-empty path has been routed, whether or not a
    /// handler was attached at the time — used to tell "no documents were
    /// ever requested" from "one is queued, waiting for the window".
    bool receivedAny() const { return receivedAny_; }

private:
    Handler handler_;
    QStringList pending_;
    bool receivedAny_ = false;
};

} // namespace calango
