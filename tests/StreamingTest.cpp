// Integration test for JobRunner's live frame streaming: a real python
// subprocess emits CALANGO_CELL / CALANGO_FRAME blocks interleaved with
// ordinary log lines; the parser must produce exactly the streamed
// structures (symbols, positions, cell) and pass other lines through.
//
// Usage: calango_stream_test  (uses the configure-time interpreter)

#include "jobs/JobRunner.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <vector>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    using namespace calango;

    QTemporaryDir dir;
    QFile script(dir.filePath(QStringLiteral("run.py")));
    script.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream(&script)
        << "import sys\n"
           "print('ordinary log line', flush=True)\n"
           "for step in range(3):\n"
           "    lines = ['CALANGO_CELL 4.0 0 0 0 4.0 0 0 0 4.0',\n"
           "             'CALANGO_FRAME 2',\n"
           "             f'Si 0.0 0.0 {step:.1f}',\n"
           "             'C 1.9 0.0 0.0']\n"
           "    sys.stdout.write('\\n'.join(lines) + '\\n')\n"
           "    sys.stdout.flush()\n"
           "    print(f'CALANGO_PROGRESS {step + 1} 3', flush=True)\n"
           "print('done', flush=True)\n";
    script.close();

    jobs::JobRunner runner;
    std::vector<std::shared_ptr<core::Structure>> frames;
    std::vector<QString> logLines;
    int exitCode = -1;
    bool crashed = true;

    QObject::connect(&runner, &jobs::JobRunner::frameStreamed,
                     [&frames](const std::shared_ptr<core::Structure>& frame) {
                         frames.push_back(frame);
                     });
    QObject::connect(&runner, &jobs::JobRunner::outputLine,
                     [&logLines](const QString& line) { logLines.push_back(line); });
    QObject::connect(&runner, &jobs::JobRunner::finished,
                     [&](int code, bool crash) {
                         exitCode = code;
                         crashed = crash;
                         QCoreApplication::quit();
                     });

    runner.start(QStringLiteral(CALANGO_DEFAULT_PYTHON),
                 QStringLiteral("run.py"), dir.path());
    QTimer::singleShot(30000, &app, [] {
        std::fprintf(stderr, "FAIL: timeout\n");
        QCoreApplication::exit(2);
    });
    if (app.exec() != 0)
        return 2;

    if (crashed || exitCode != 0) {
        std::fprintf(stderr, "FAIL: subprocess exit %d (crashed=%d)\n",
                     exitCode, crashed);
        return 1;
    }
    if (frames.size() != 3) {
        std::fprintf(stderr, "FAIL: expected 3 streamed frames, got %zu\n",
                     frames.size());
        return 1;
    }
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto& frame = *frames[i];
        if (frame.size() != 2 || frame.atoms()[0].atomicNumber != 14
            || frame.atoms()[1].atomicNumber != 6
            || std::abs(frame.atoms()[0].position.z - static_cast<double>(i))
                > 1e-9
            || !frame.cell().isDefined()
            || std::abs(frame.cell().vectors()[0].x - 4.0) > 1e-9) {
            std::fprintf(stderr, "FAIL: frame %zu content wrong\n", i);
            return 1;
        }
    }
    // Atom/marker lines must not leak into the log; normal lines must.
    bool sawOrdinary = false, sawDone = false;
    for (const QString& line : logLines) {
        if (line.contains(QLatin1String("ordinary log line")))
            sawOrdinary = true;
        if (line == QLatin1String("done"))
            sawDone = true;
        if (line.startsWith(QLatin1String("CALANGO_FRAME"))
            || line.startsWith(QLatin1String("CALANGO_CELL"))
            || line.startsWith(QLatin1String("Si "))) {
            std::fprintf(stderr, "FAIL: stream line leaked to log: %s\n",
                         qPrintable(line));
            return 1;
        }
    }
    if (!sawOrdinary || !sawDone) {
        std::fprintf(stderr, "FAIL: ordinary lines missing from log\n");
        return 1;
    }

    std::printf("PASS: 3 frames streamed (Si+C, cell 4 Å), log passthrough clean\n");
    return 0;
}
