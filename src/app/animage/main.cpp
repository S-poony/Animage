// SPDX-License-Identifier: GPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickStyle>
#include <QStyleHints>
#include <QUrl>
#include <cstdio>

#include "crash_report.h"
#include "qml_registry.h"

int main(int argc, char** argv) {
    installCrashHandler();

    // The whole interface is painted from Theme.qml: our own buttons, dialogs,
    // panels and wrappers. The remaining Qt Quick Controls -- ComboBox, SpinBox,
    // Slider, TextField -- get their behaviour from the controls module and
    // their looks from Animage wrappers (AppSpinBox, AppComboBox, ...), which
    // only makes sense if the underlying style is predictable. Basic is exactly
    // that: deliberately neutral, with no Material or native geometry to fight.
    // The style must be chosen before any control is instantiated.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("Animage"));
    QCoreApplication::setOrganizationName(QStringLiteral("Animage"));

    registerAnimageQmlTypes();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));

    // Theme.qml reads a top-level boolean `animageDark` (see Theme.qml); the
    // scheme for the underlying controls' palette is the same decision. It is
    // resolved synchronously here -- resolveColorScheme() guesses from the OS
    // palette luminance while Qt.styleHints.colorScheme is still Unknown, so
    // the first frame matches the user's theme instead of flashing light --
    // and brought up to date below every time the OS reports a real scheme.
    const auto reapply = [&](Qt::ColorScheme s) {
        const bool dark = s != Qt::ColorScheme::Light;  // Unknown, Dark -> dark
        app.setPalette(animagePalette(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light));
        engine.rootContext()->setContextProperty(QStringLiteral("animageDark"), dark);
    };
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [&reapply](Qt::ColorScheme s) { reapply(s); });

    // Seed the scheme now, before any QML is evaluated, from the synchronous
    // resolver so the first frame already matches the user's theme.
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
