// SPDX-License-Identifier: GPL-3.0-or-later
#include "qml_registry.h"

// GCC's -Wsfinae-incomplete misfires on Qt's registration templates when the
// class being registered is a QQuickItem: the candidate probe in qmetatype.h
// is recorded against the type before its definition is parsed, and the
// definition then "fails" a check it never ran. Every Qt QML application with
// a custom item hits this; the warning is a false positive and it is silenced
// here, for this file only, with the reason written next to it.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsfinae-incomplete"
#endif

#include <QDir>
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

QPalette animageDarkPalette() {
    // The mirror of Theme.qml: Window is the surface, Base is the raised
    // surface, Highlight is the accent, text and disabled roles are the
    // theme's text colours. Keep the two in step by hand; there is no way
    // for C++ to read QML singleton properties.
    const QColor surface("#17181e");
    const QColor surfaceHigh("#20222b");
    const QColor text("#e8eaf0");
    const QColor textTertiary("#676e80");
    const QColor textDisabled("#4a4f5c");
    const QColor accent("#d87854");
    const QColor textOnAccent("#1c120c");

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

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
