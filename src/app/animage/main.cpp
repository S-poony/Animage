// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>

#include "crash_report.h"
#include "main_window.h"

int main(int argc, char** argv) {
    installCrashHandler();

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Animage"));
    QCoreApplication::setOrganizationName(QStringLiteral("Animage"));

    MainWindow window;
    window.show();
    return app.exec();
}
