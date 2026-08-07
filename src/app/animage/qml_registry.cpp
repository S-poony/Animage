// SPDX-License-Identifier: GPL-3.0-or-later
#include "qml_registry.h"

// GCC's -Wsfinae-incomplete misfires on Qt's registration templates when the
// class being registered is a QQuickItem: the candidate probe in qmetatype.h
// is recorded against the type before its definition is parsed, and the
// definition then "fails" a check it never ran. Every Qt QML application with
// a custom item hits this; the warning is a false positive and it is silenced
// here, for this file only, with the reason written next to it.
#if defined(__GNUC__) && !defined(__clang__) && defined(ANIMAGE_HAVE_W_SFINAE_INCOMPLETE)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsfinae-incomplete"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QStyleHints>
#include <QtQml/qqml.h>
#include <QtQuick/qquickitem.h>
#include <QUrl>

#include "app_controller.h"
#include "canvas_view.h"
#include "layers_model.h"
#include "scene_settings_model.h"
#include "timeline_model.h"

inline void initQmlResources() {
    Q_INIT_RESOURCE(qmake_Animage);
    Q_INIT_RESOURCE(animage_ui_raw_qml_0);
}

namespace {

// The two design tokens are QML files, and each declares itself a singleton
// with `pragma Singleton`. Registering by URL makes the engine load the file
// once and hand out the one object to every import, which is what `Theme`
// and `Icons` are: shared, read-only state.
void registerQmlSingletons() {
    const auto singleton = [](const char* name, const char* file) {
        qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/Animage/animage/qml/") + file),
                                 "Animage", 1, 0, name);
    };
    singleton("Theme", "Theme.qml");
    singleton("Icons", "Icons.qml");
}

}  // namespace

void registerAnimageQmlTypes() {
    initQmlResources();

    qmlRegisterType<CanvasView>("Animage", 1, 0, "CanvasView");
    qmlRegisterType<LayersModel>("Animage", 1, 0, "LayersModel");
    qmlRegisterType<TimelineModel>("Animage", 1, 0, "TimelineModel");
    qmlRegisterType<CtgSourcesModel>("Animage", 1, 0, "CtgSourcesModel");
    qmlRegisterType<SceneSettingsModel>("Animage", 1, 0, "SceneSettingsModel");
    qmlRegisterType<AppController>("Animage", 1, 0, "AppController");

    registerQmlSingletons();
}

QPalette animagePalette(Qt::ColorScheme scheme) {
    // The mirror of Theme.qml: Window is the surface, Base is the raised
    // surface, Highlight is the accent, text and disabled roles are the
    // theme's text colours. Keep the two in step by hand; there is no way
    // for C++ to read QML singleton properties. Dark is the scheme the app
    // shipped with; light is the twin palette Theme.qml switches to.
    const bool dark = scheme != Qt::ColorScheme::Light;
    const QColor surface(dark ? "#17181e" : "#f7f8fa");
    const QColor surfaceHigh(dark ? "#20222b" : "#ffffff");
    const QColor text(dark ? "#e8eaf0" : "#1c2028");
    const QColor textTertiary(dark ? "#676e80" : "#7d8494");
    const QColor textDisabled(dark ? "#4a4f5c" : "#a8aebd");
    const QColor accent(dark ? "#b07a62" : "#a06a52");
    const QColor textOnAccent(dark ? "#1c120c" : "#f7efea");

    QPalette palette;
    palette.setColor(QPalette::Window, surface);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, surfaceHigh);
    palette.setColor(QPalette::AlternateBase, surface);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, surfaceHigh);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, accent);
    palette.setColor(QPalette::Highlight, accent);
    palette.setColor(QPalette::HighlightedText, textOnAccent);
    palette.setColor(QPalette::ToolTipBase, surfaceHigh);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, textTertiary);
    palette.setColor(QPalette::Link, accent);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, textDisabled);
    return palette;
}

Qt::ColorScheme resolveColorScheme() {
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        const Qt::ColorScheme scheme = app->styleHints()->colorScheme();
        if (scheme == Qt::ColorScheme::Light || scheme == Qt::ColorScheme::Dark) {
            // The OS has reported a real scheme; that is authoritative.
            return scheme;
        }
    }
    // Unknown (async portal still resolving; offscreen never resolves): guess
    // from the base of the resolved system palette. Qt always has a palette
    // here, and a dark theme has a dark base, so its luminance picks the
    // right side on every platform that a real OS palette is available for;
    // the default light palette resolves to Light.
    const QColor base = QGuiApplication::palette().color(QPalette::Base);
    const double luminance = 0.299 * base.red() + 0.587 * base.green() + 0.114 * base.blue();
    return luminance >= 128.0 ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
