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
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <cstdio>

#include "qml_registry.h"

int main(int argc, char** argv) {
    // Same style contract as main.cpp: the QML paints itself from Theme.qml,
    // the few raw Qt Quick Controls underneath are Basic, and the palette is
    // the dark mirror of Theme.qml so nothing renders as a light OS widget.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);
    app.setPalette(animageDarkPalette());
    QCoreApplication::setApplicationName(QStringLiteral("Animage"));

    registerAnimageQmlTypes();

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/"));

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
        } else if (mode == QLatin1String("geometry")) {
            const QStringList names = {
                "brushSizePopup", "sizeTrigger", "brushToolButton",
                "toolOptionsBar", "sizeValueField", "sizeValueEditor"
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
