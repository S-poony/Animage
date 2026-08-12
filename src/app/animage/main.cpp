// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>

#include <cstdio>

#include "crash_report.h"
#include "main_window.h"
#include "shortcuts.h"

int main(int argc, char** argv) {
    installCrashHandler();

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Animage"));
    QCoreApplication::setOrganizationName(QStringLiteral("Animage"));

    // Here and not in MainWindow, which is deliberate: a test and a screenshot
    // both build a MainWindow, and either would then run on whatever the person
    // running it had rebound. The application is the only thing that should read
    // somebody's settings, so it is the only thing that does.
    //
    // The names have to be set first -- the config directory is named after them
    // -- and a file that will not read is reported and stepped over rather than
    // refused. Refusing to start over a keyboard would be the worst thing this
    // feature could do.
    QString trouble;
    if (!shortcuts::current().load(shortcuts::userFilePath(), &trouble)) {
        std::fprintf(stderr, "shortcuts: %s -- starting on the defaults\n",
                     qPrintable(trouble));
    }

    MainWindow window;
    window.show();
    return app.exec();
}
