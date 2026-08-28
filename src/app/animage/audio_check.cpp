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

    // **A backend that loaded says so, and that line is the verdict.** Counting
    // warnings instead was the first version of this and it cried wolf on the
    // first honest machine it met: a Linux runner has no sound server and no
    // GPU, so a perfectly bundled backend arrives with `pa_context_connect()
    // failed` and four `Couldn't load va-drm` beside it -- every one of them
    // about the machine and none about the packaging. What this spike is
    // actually asking is whether the deployment tool put a backend where the
    // program could find it, and only the announcement answers that.
    bool loaded = false;
    bool complained = false;
    for (const Said& m : std::as_const(said)) {
        const bool bad = m.type == QtWarningMsg || m.type == QtCriticalMsg
                         || m.type == QtFatalMsg;
        complained = complained || bad;
        loaded = loaded || m.text.contains(QLatin1String("Using Qt multimedia"));
        lines << QStringLiteral("  qt %1: %2")
                     .arg(bad ? QStringLiteral("warned") : QStringLiteral("said"), m.text);
    }

    // The outcomes, named, because a CI log is read by somebody who was not here
    // and a device count does not say which one this is.
    if (!loaded)
        lines << QStringLiteral(
            "  VERDICT: no backend announced itself. That is what this spike is looking "
            "for -- the deployment tool bundled nothing the program can find.");
    else if (outs.isEmpty())
        lines << QStringLiteral(
            "  VERDICT: a backend loaded; this machine has no audio output. That is what "
            "a CI runner looks like and it is a pass for the packaging.");
    else
        lines << QStringLiteral(
            "  VERDICT: a backend loaded and found outputs. Audio works here.");

    // Said after the verdict and not folded into it: warnings on a machine with
    // a working backend are about that machine -- no sound server, no VA-API --
    // and they are worth reading without being worth failing on.
    if (loaded && complained)
        lines << QStringLiteral(
            "  (The warnings above are the machine, not the package: a backend that "
            "announced itself was found and loaded.)");

    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
#endif
}

}  // namespace audio_check
