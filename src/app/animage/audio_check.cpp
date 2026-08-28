// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_check.h"

#include <QList>
#include <QStringList>

#ifdef ANIMAGE_HAVE_AUDIO
#include <QAudioDevice>
#include <QLibraryInfo>
#include <QMediaDevices>
#include <QtGlobal>

#include <utility>
#endif

namespace audio_check {

bool built() {
#ifdef ANIMAGE_HAVE_AUDIO
    return true;
#else
    return false;
#endif
}

#ifdef ANIMAGE_HAVE_AUDIO
namespace {

// Qt says what it loaded, and what it failed to load, in messages and nowhere
// else -- then carries on returning empty lists either way. **So the messages
// are the reading and swallowing them would lose all of it.** A machine with no
// sound card and a package with no backend in it both come back with no
// devices; only the messages tell them apart, and the second is the one the
// spike exists to catch.
//
// A message handler rather than QT_LOGGING_RULES, because this has to work on a
// downloaded package that somebody double-clicked, where there is no
// opportunity to set an environment variable first.
//
// **The type is kept with the text and the two are not the same fact.** Qt
// announces a backend that loaded correctly at info level -- "Using Qt
// multimedia with FFmpeg version ..." -- which is the line most worth having
// here and reads exactly like a complaint if only the string is kept. The first
// version of this did that and called a healthy machine a failure.
struct Said {
    QtMsgType type;
    QString text;
};

QList<Said>* g_captured = nullptr;
QtMessageHandler g_previous = nullptr;

void capture(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    if (g_captured) g_captured->append({type, msg});
    if (g_previous) g_previous(type, ctx, msg);
}

}  // namespace
#endif

QString report() {
#ifndef ANIMAGE_HAVE_AUDIO
    return QStringLiteral(
        "audio: not built -- Qt Multimedia was not found when this was configured.\n");
#else
    QStringList lines;
    lines << QStringLiteral("audio: built against Qt Multimedia");
    lines << QStringLiteral("  Qt %1, plugins at %2")
                 .arg(QLatin1String(qVersion()),
                      QLibraryInfo::path(QLibraryInfo::PluginsPath));

    QList<Said> said;
    g_captured = &said;
    g_previous = qInstallMessageHandler(capture);
    const QList<QAudioDevice> outs = QMediaDevices::audioOutputs();
    const bool default_null = QMediaDevices::defaultAudioOutput().isNull();
    qInstallMessageHandler(g_previous);
    g_captured = nullptr;

    lines << QStringLiteral("  outputs: %1").arg(outs.size());
    for (const QAudioDevice& d : outs)
        lines << QStringLiteral("    %1").arg(d.description());
    lines << QStringLiteral("  default output is null: %1")
                 .arg(default_null ? QStringLiteral("yes") : QStringLiteral("no"));

    bool complained = false;
    for (const Said& m : std::as_const(said)) {
        const bool bad = m.type == QtWarningMsg || m.type == QtCriticalMsg
                         || m.type == QtFatalMsg;
        complained = complained || bad;
        lines << QStringLiteral("  qt %1: %2")
                     .arg(bad ? QStringLiteral("warned") : QStringLiteral("said"), m.text);
    }

    // The four outcomes, named, because a CI log is read by somebody who was not
    // here and a device count does not say which one this is. The order matters:
    // a warning outranks a device list, since a backend that half-loaded is the
    // outcome worth stopping on.
    if (complained)
        lines << QStringLiteral(
            "  VERDICT: Qt warned. Read the lines above -- a backend that did not load "
            "is what this spike is looking for.");
    else if (said.isEmpty())
        lines << QStringLiteral(
            "  VERDICT: Qt named no backend at all. Either logging is off here, or "
            "nothing was loaded. Re-run with QT_LOGGING_RULES=qt.multimedia*=true.");
    else if (outs.isEmpty())
        lines << QStringLiteral(
            "  VERDICT: a backend loaded and this machine has no audio output. That is "
            "what a CI runner looks like and it is a pass.");
    else
        lines << QStringLiteral(
            "  VERDICT: a backend loaded and found outputs. Audio works here.");

    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
#endif
}

}  // namespace audio_check
