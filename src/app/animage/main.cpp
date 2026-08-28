// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QIcon>

#include <cstdio>

#include "crash_report.h"
#include "main_window.h"
#include "shortcuts.h"

int main(int argc, char** argv) {
    installCrashHandler();

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Animage"));
    QCoreApplication::setOrganizationName(QStringLiteral("Animage"));

    // Every window Qt opens takes its icon from here unless it sets its own,
    // so this one call covers the main window, the dialogs and the task
    // switcher. The sizes are added rather than rendered from one large PNG
    // because Qt scales down when it has to, and a 512 squeezed into a 16-pixel
    // title bar is a smear -- these were rasterised at the size they are used
    // at. Which one Qt picks is the platform's business, not ours.
    //
    // Not in MainWindow, for the reason the shortcuts below are not either.
    // The PNGs are compiled into this executable and not into animage_ui, so a
    // test or a screenshot that builds a MainWindow has no icon to look for and
    // does not go looking; an icon belongs to the application in any case.
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256, 512}) {
        icon.addFile(QStringLiteral(":/icons/animage-%1.png").arg(size));
    }
    QApplication::setWindowIcon(icon);

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
