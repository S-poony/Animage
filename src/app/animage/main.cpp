// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QStyleHints>
#include <QUrl>
#include <cstdio>

#include "crash_report.h"
#include "qml_registry.h"

int main(int argc, char** argv) {
    installCrashHandler();

    // No QQuickStyle::setStyle() call — let Qt pick the native platform style:
    // macOS style on macOS, Windows style on Windows, Fusion/Basic on Linux.
    // This gives menu bars, buttons, sliders and form controls their OS-native
    // look without custom chrome.

    QGuiApplication app(argc, argv);

    // Ensure themed icons (Adwaita) are found in headless/offscreen and provide
    // a fallback theme from bundled resources for Windows/macOS where the
    // system theme may not contain Freedesktop names like input-tablet/edit-delete.
    {
        auto paths = QIcon::themeSearchPaths();
        if (!paths.contains(QStringLiteral("/usr/share/icons")))
            paths << QStringLiteral("/usr/share/icons");
        QIcon::setThemeSearchPaths(paths);
        QIcon::setFallbackSearchPaths(QIcon::fallbackSearchPaths() << QStringLiteral(":/icons") << QStringLiteral(":/Animage/animage/icons"));
        if (QIcon::themeName().isEmpty())
            QIcon::setThemeName(QStringLiteral("Adwaita"));
        if (QIcon::fallbackThemeName().isEmpty())
            QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
    }

    QCoreApplication::setApplicationName(QStringLiteral("Animage"));
    QCoreApplication::setOrganizationName(QStringLiteral("Animage"));

    registerAnimageQmlTypes();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));

    // Theme.qml uses SystemPalette (platform native). Keep palette in sync
    // with the OS so dark really is dark (SystemPalette follows app palette).
    const auto reapply = [&](Qt::ColorScheme s) {
        const bool dark = s != Qt::ColorScheme::Light;
        app.setPalette(animagePalette(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light));
        engine.rootContext()->setContextProperty(QStringLiteral("animageDark"), dark);
    };
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [&reapply](Qt::ColorScheme s) { reapply(s); });

    const Qt::ColorScheme initial = resolveColorScheme();
    app.setPalette(animagePalette(initial));
    engine.rootContext()->setContextProperty(QStringLiteral("animageDark"),
                                             initial != Qt::ColorScheme::Light);

    QObject::connect(&engine, &QQmlEngine::warnings,
                     [](const QList<QQmlError>& warnings) {
                         for (const QQmlError& w : warnings) {
                             std::fprintf(stderr, "QML Warning: %s\n", qPrintable(w.toString()));
                         }
                     });

    // Try loading via module first, or directly via resource URL
    engine.loadFromModule("Animage", "Main");
    if (engine.rootObjects().isEmpty()) {
        engine.load(QUrl(QStringLiteral("qrc:/Animage/animage/qml/Main.qml")));
    }

    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "Animage: failed to load QML root object from module Animage/Main\n");
        return 1;
    }

    return app.exec();
}
