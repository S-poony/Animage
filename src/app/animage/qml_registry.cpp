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
#ifdef ANIMAGE_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#endif

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

// Theme is the spacing/type singleton; Icons has been removed (native style).
void registerQmlSingletons() {
    const auto singleton = [](const char* name, const char* file) {
        qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/Animage/animage/qml/") + file),
                                 "Animage", 1, 0, name);
    };
    singleton("Theme", "Theme.qml");
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
    // Platform native dark/light — no custom brown. Uses the application
    // style's standard palette tinted to the requested scheme. The highlight
    // (accent) stays the system highlight (blue on Windows, grey on macOS, etc.).
    const bool dark = scheme != Qt::ColorScheme::Light;
    if (!dark)
        return QPalette(); // light: system default (Fusion/Windows/macOS light)

    // Dark: use Fusion/Windows/macOS native dark window/base while keeping
    // the system highlight as accent. Values are the platform's standard dark
    // greys (Fusion #31363b, Windows #202020) — dark enough for SystemPalette
    // to report dark, light enough for native controls to remain legible.
    const QColor winDark(0x30, 0x30, 0x30);
    const QColor baseDark(0x23, 0x23, 0x23);
    const QColor btnDark(0x3a, 0x3a, 0x3a);
    const QColor textLight(0xf0, 0xf0, 0xf0);
    const QPalette sys = QGuiApplication::palette();
    QPalette p = sys;
    p.setColor(QPalette::Window, winDark);
    p.setColor(QPalette::WindowText, textLight);
    p.setColor(QPalette::Base, baseDark);
    p.setColor(QPalette::AlternateBase, winDark);
    p.setColor(QPalette::Text, textLight);
    p.setColor(QPalette::Button, btnDark);
    p.setColor(QPalette::ButtonText, textLight);
    p.setColor(QPalette::ToolTipBase, winDark);
    p.setColor(QPalette::ToolTipText, textLight);
    p.setColor(QPalette::PlaceholderText, QColor(0x9a, 0xa1, 0xb0));
    // Shading roles — these are what SystemPalette mid/dark/light expose as
    // Theme.border / borderStrong. Without them the dark palette inherits the
    // light system's mid (#b8b8b8) which paints as a stark white separator on
    // the dark window. Keep the native ordering Light > Midlight > Window >
    // Mid > Dark > Shadow.
    p.setColor(QPalette::Light, QColor(0x4a, 0x4a, 0x4a));
    p.setColor(QPalette::Midlight, QColor(0x3c, 0x3c, 0x3c));
    p.setColor(QPalette::Mid, QColor(0x28, 0x28, 0x28));
    p.setColor(QPalette::Dark, QColor(0x1e, 0x1e, 0x1e));
    p.setColor(QPalette::Shadow, QColor(0x11, 0x11, 0x11));
    // Keep system highlight (accent) — native blue/grey, not custom brown.
    p.setColor(QPalette::Highlight, sys.color(QPalette::Highlight));
    p.setColor(QPalette::HighlightedText, sys.color(QPalette::HighlightedText));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x7a, 0x7a, 0x7a));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x7a, 0x7a, 0x7a));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x7a, 0x7a, 0x7a));
    return p;
}

Qt::ColorScheme resolveColorScheme() {
#ifdef ANIMAGE_HAVE_DBUS
    // Try the Freedesktop portal first: it is available synchronously even
    // when QStyleHints::colorScheme is still Unknown or stale Light (GNOME
    // reports the preference via async DBus, so styleHints can be wrong for
    // the first ~100 ms). The portal value is 0 = default, 1 = prefer-dark,
    // 2 = prefer-light. A short timeout keeps startup fast on platforms with
    // no portal (Windows/macOS/CI).
    if (QDBusConnection::sessionBus().isConnected()) {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"),
            QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.Settings"),
            QStringLiteral("Read"));
        msg.setArguments({QStringLiteral("org.freedesktop.appearance"),
                          QStringLiteral("color-scheme")});
        // 200 ms is enough on a local session bus; longer would block startup.
        QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 200);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            QVariant outer = reply.arguments().first();
            // Reply is variant(variant(uint32)): outer QDBusVariant -> inner QDBusVariant -> uint.
            uint schemeVal = 0;
            bool ok = false;
            if (outer.canConvert<QDBusVariant>()) {
                QVariant inner1 = qvariant_cast<QDBusVariant>(outer).variant();
                if (inner1.canConvert<QDBusVariant>()) {
                    QVariant inner2 = qvariant_cast<QDBusVariant>(inner1).variant();
                    schemeVal = inner2.toUInt(&ok);
                } else {
                    schemeVal = inner1.toUInt(&ok);
                }
            } else {
                schemeVal = outer.toUInt(&ok);
            }
            if (ok) {
                if (schemeVal == 1) return Qt::ColorScheme::Dark;
                if (schemeVal == 2) return Qt::ColorScheme::Light;
                // 0 = no preference -> fall through to other signals.
            }
        }
    }
#endif
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        const Qt::ColorScheme scheme = app->styleHints()->colorScheme();
        if (scheme == Qt::ColorScheme::Light || scheme == Qt::ColorScheme::Dark) {
            // The OS has reported a real scheme; that is authoritative once the
            // portal is not giving a direct answer (e.g. Windows/macOS where the
            // portal does not exist, or value 0 = no preference).
            return scheme;
        }
    }
    // Fallback: guess from the base of the resolved system palette. Qt always
    // has a palette here, and a dark theme has a dark base, so its luminance
    // picks the right side on every platform that a real OS palette is
    // available for; the default light palette resolves to Light.
    const QColor base = QGuiApplication::palette().color(QPalette::Base);
    const double luminance = 0.299 * base.red() + 0.587 * base.green() + 0.114 * base.blue();
    return luminance >= 128.0 ? Qt::ColorScheme::Light : Qt::ColorScheme::Dark;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
