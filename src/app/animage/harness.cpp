// SPDX-License-Identifier: GPL-3.0-or-later
//
// Assembles the real interface offscreen and saves a picture of it. Two jobs:
//
//   - it is the "the whole window loads without a QML error" check, which is
//     the closest thing the interface has to a smoke test;
//   - run with a path, it makes a screenshot:
//
//       animage_qml_harness shot.png
//
//     The window is given a moment to settle, a frame is grabbed through the
//     scene graph, and the png lands where asked.
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QStringList>
#include <QEventLoop>
#include <cstdio>

#include "qml_registry.h"
#include "app_controller.h"

int main(int argc, char** argv) {
    // Native platform style — same as main.cpp, no forced Basic.

    QGuiApplication app(argc, argv);
    // Ensure themed icons are found in offscreen (headless has empty search path)
    // and provide fallback for Windows/macOS where Freedesktop theme not installed.
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
    // A trailing "light" argument forces the light theme to render, so it can
    // be screenshotted on a platform that does not report one (offscreen
    // colorScheme is Unknown, which Theme.qml treats as dark). Scan the whole
    // tail so it combines with the capture mode ("harness shot.png min light").
    const QStringList tail = [&] {
        QStringList t;
        for (int i = 2; i < argc; ++i) t << QString::fromLocal8Bit(argv[i]);
        return t;
    }();
    const bool force_light = tail.contains(QLatin1String("light"));
    // The offscreen platform has no color scheme. Theme.qml reads animageDark
    // (seeded here) rather than Qt.styleHints.colorScheme, so this harness
    // picks the look itself: dark by default -- the design's primary look, so
    // the workflow screenshots are stable -- and light on request. main.cpp,
    // the real app, resolves the OS scheme via resolveColorScheme().
    const Qt::ColorScheme scheme = force_light ? Qt::ColorScheme::Light
                                               : Qt::ColorScheme::Dark;
    app.setPalette(animagePalette(scheme));
    QCoreApplication::setApplicationName(QStringLiteral("Animage"));

    registerAnimageQmlTypes();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));
    // Same context property as main.cpp: Theme.qml reads animageDark.
    engine.rootContext()->setContextProperty(QStringLiteral("animageDark"),
                                             scheme != Qt::ColorScheme::Light);

    bool has_warnings = false;
    QObject::connect(&engine, &QQmlEngine::warnings,
                     [&has_warnings](const QList<QQmlError>& warnings) {
                         for (const QQmlError& w : warnings) {
                             std::fprintf(stderr, "QML: %s\n", qPrintable(w.toString()));
                             has_warnings = true;
                         }
                     });

    engine.loadFromModule("Animage", "Main");
    if (engine.rootObjects().isEmpty()) {
        engine.load(QUrl(QStringLiteral("qrc:/Animage/animage/qml/Main.qml")));
    }

    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "the Animage interface failed to load\n");
        return 1;
    }

    QQuickWindow* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) return 1;

    // Show window offscreen and process pending layout events immediately
    window->show();
    QCoreApplication::processEvents();

    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (!out.isEmpty()) {
        // An optional second argument selects the capture mode:
        //   "min"    grabs the window at its minimum size, where overlapping
        //            panels and overflowing inspectors show up;
        //   "popup"  opens the brush-size popup before grabbing, so the
        //            popover itself can be inspected;
        //   "onion"  turns some onion skin and a longer hold on (and opens
        //            the temporal-strip popover when "popup" is argv[3]) so
        //            the active ghost trail can be inspected;
        //   "geometry" prints the mapped geometry of key named items and
        //            exits without saving a picture (for layout checks);
        //   anything else grabs at the designed size.
        const QString mode = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
        if (mode == QLatin1String("min")) {
            window->setWidth(window->minimumWidth());
            window->setHeight(window->minimumHeight());
            QCoreApplication::processEvents();
        } else if (mode == QLatin1String("popup")) {
            if (QObject* p = window->findChild<QObject*>("brushSizePopup")) {
                QMetaObject::invokeMethod(p, "open");
                QCoreApplication::processEvents();
                QCoreApplication::processEvents();
                const QVariant x = p->property("x"), y = p->property("y");
                const QVariant w = p->property("width"), h = p->property("height");
                std::printf("popup-open x=%s y=%s w=%s h=%s\n", qPrintable(x.toString()),
                            qPrintable(y.toString()), qPrintable(w.toString()),
                            qPrintable(h.toString()));
            }
        } else if (mode == QLatin1String("onion")) {
            // Verification mode for the unified temporal strip: turn some
            // onion skin on (previous and next), make the hold a bit longer,
            // and open the advanced popover, then grab the result.
            if (QObject* strip = window->findChild<QObject*>("temporalStrip")) {
                if (auto* ctl = qobject_cast<AppController*>(
                        qvariant_cast<QObject*>(strip->property("controller")))) {
                    ctl->setOnionBefore(2);
                    ctl->setOnionAfter(1);
                    ctl->setCurrentHold(3);
                    std::printf("onion before=%d after=%d hold=%d\n",
                                ctl->onionBefore(), ctl->onionAfter(),
                                ctl->currentHold());
                } else {
                    std::printf("onion: no controller\n");
                }
            } else {
                std::printf("onion: no strip\n");
            }
            // Optionally open the advanced popover when the mode requests it,
            // so the states can be checked with and without the scrim.
            if (argc > 3 && QLatin1String(argv[3]) == QLatin1String("popup")) {
                if (QObject* p = window->findChild<QObject*>("onionSettingsPopup")) {
                    QMetaObject::invokeMethod(p, "open");
                }
            }
            QCoreApplication::processEvents();
            // Let any on-screen state animations settle so the grab shows the
            // final look rather than a mid-fling frame.
            {
                // Real wall-clock wait: processEvents alone can spin faster than
                // the opacity/colour Behaviour animations, leaving them mid-flight.
                QEventLoop waiter;
                QTimer::singleShot(350, &waiter, &QEventLoop::quit);
                waiter.exec();
            }
            QCoreApplication::processEvents();
        } else if (mode == QLatin1String("geometry")) {
            const QStringList names = {
                "brushSizePopup", "sizeTrigger", "brushToolButton",
                "toolOptionsBar", "sizeValueField", "sizeValueEditor",
                "temporalStrip", "onionSettingsPopup"
            };
            for (const QString& n : names) {
                QObject* o = window->findChild<QObject*>(n);
                if (!o) { std::printf("geom %s <missing>\n", qPrintable(n)); continue; }
                const QVariant x = o->property("x"), y = o->property("y");
                const QVariant w = o->property("width"), h = o->property("height");
                std::printf("geom %s x=%s y=%s w=%s h=%s\n", qPrintable(n),
                            qPrintable(x.toString()), qPrintable(y.toString()),
                            qPrintable(w.toString()), qPrintable(h.toString()));
            }
            return has_warnings ? 1 : 0;
        }
        QImage image = window->grabWindow();
        image.save(out);
    }

    return has_warnings ? 1 : 0;
}
