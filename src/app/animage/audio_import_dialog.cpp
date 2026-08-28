// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_import_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// What came out of the file, in the terms somebody placing a sound thinks in.
//
// **Length in frames as well as in seconds, and frames first.** The whole of
// lipsync is deciding which frame a sound is on, so a soundtrack whose length
// is given only in seconds has been described in the wrong unit for the job it
// is here to do.
QString accountOfClip(const AudioImportDialog::Found& found) {
    const double seconds =
        found.rate > 0 ? double(found.frames) / double(found.rate) : 0.0;
    const int fps = std::max(1, found.scene_fps);
    const long long frames = found.rate > 0
                                 ? (long long)(double(found.frames) * fps / found.rate + 0.5)
                                 : 0;

    return QStringLiteral("%1 frames at %2 fps — %3 seconds. %4 Hz, %5.")
        .arg(frames)
        .arg(fps)
        .arg(seconds, 0, 'f', 2)
        .arg(found.rate)
        .arg(found.channels == 1   ? QStringLiteral("mono")
             : found.channels == 2 ? QStringLiteral("stereo")
                                   : QStringLiteral("%1 channels").arg(found.channels));
}

// Memory, said plainly -- **and said next to the size of the file**, which is
// the whole point of this sentence rather than a decoration.
//
// A bare number here invites exactly one comparison, and it is the wrong one:
// a 799 KB mp3 announcing 7.5 MB reads as the program having gone wrong. It has
// not. Decoded audio is uncompressed -- 48 kHz stereo float is about 384 KB a
// second whatever the file squeezed it to -- so the two numbers are measuring
// different things and a recap that shows one without the other is inviting a
// subtraction that means nothing. Reported from use, on the first file anybody
// imported that was not a WAV.
QString costOfClip(const AudioImportDialog::Found& found) {
    const double megabytes =
        double(found.frames) * std::max(1, found.channels) * sizeof(float) / (1024.0 * 1024.0);
    const auto shown = [](double mb) {
        return QStringLiteral("%1 MB").arg(mb, 0, 'f', mb < 10.0 ? 1 : 0);
    };

    if (found.file_bytes <= 0) {
        return QStringLiteral("About %1 in memory while the project is open. Nothing is added "
                              "to what a save writes: the sound is played from its file.")
            .arg(shown(megabytes));
    }

    const double on_disk = double(found.file_bytes) / (1024.0 * 1024.0);
    return QStringLiteral(
               "About %1 in memory while the project is open — the file is %2. Decoded sound "
               "is uncompressed, so it is always larger than the file, however small that is. "
               "Nothing is added to what a save writes: the sound is played from its file.")
        .arg(shown(megabytes), shown(on_disk));
}

}  // namespace

AudioImportDialog::AudioImportDialog(const Found& found, int playhead_frame, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Import audio"));

    auto* layout = new QVBoxLayout(this);

    auto* what = new QGroupBox(QStringLiteral("What was found"), this);
    auto* what_layout = new QVBoxLayout(what);

    auto* name = new QLabel(found.file, what);
    name->setWordWrap(true);
    what_layout->addWidget(name);

    auto* account = new QLabel(accountOfClip(found), what);
    account->setWordWrap(true);
    what_layout->addWidget(account);

    auto* cost = new QLabel(costOfClip(found), what);
    cost->setWordWrap(true);
    what_layout->addWidget(cost);

    // A decode that succeeded and still had something to say -- a file read
    // only as far as the cap, or one that ended early. Said here because the
    // import is going ahead either way and the difference is the user's to know
    // about, which is the same house rule the sequence dialog follows for a
    // frame that would not read.
    if (!found.trouble.isEmpty()) {
        auto* said = new QLabel(found.trouble, what);
        said->setWordWrap(true);
        what_layout->addWidget(said);
    }

    layout->addWidget(what);

    auto* choices = new QGroupBox(QStringLiteral("Where it goes"), this);
    auto* form = new QFormLayout(choices);

    start_ = new QSpinBox(choices);
    // **Negatives are allowed in, and that is the decision here.** A line of
    // dialogue arrives with a breath in front of the word; putting the word on
    // frame 1 means the breath falls off the start, and a box that refused
    // would be refusing an ordinary thing to want. The model stores a signed
    // offset for exactly this.
    start_->setRange(-100000, 100000);
    start_->setValue(1);
    start_->setToolTip(
        QStringLiteral("Which frame the sound starts on. Before 1 is allowed: the sound then "
                       "begins before the shot does, which is what a breath in front of a word "
                       "wants. The playhead is at %1.")
            .arg(std::max(1, playhead_frame)));
    form->addRow(QStringLiteral("Start at frame"), start_);

    // Only when it would change something. See Answer::extend_shot for the
    // three cases and why the tick is where it is.
    if (found.sound_frames > found.shot_frames) {
        extend_ = new QCheckBox(QStringLiteral("Make the shot reach the end of the sound"),
                                choices);
        extend_->setChecked(!found.length_is_fixed);
        extend_->setToolTip(
            found.length_is_fixed
                ? QStringLiteral(
                      "The shot is set to %1 frames and the sound runs to %2. Ticking this "
                      "moves the end of the shot; leaving it alone keeps the length you set, "
                      "and the sound past it can still be scrubbed over.")
                      .arg(found.shot_frames)
                      .arg(found.sound_frames)
                : QStringLiteral(
                      "Nothing has said how long this shot is, so it is as long as its "
                      "drawings — %1 frames — and the sound runs to %2. Without this, Play "
                      "would stop before the sound does.")
                      .arg(found.shot_frames)
                      .arg(found.sound_frames));
        form->addRow(QString(), extend_);
    }

    layout->addWidget(choices);

    // **The one sentence that has to be here**, because it is otherwise found
    // out much later -- after somebody has timed a shot to a track, exported
    // it, and sent it on. It stops being true the day video export ships, which
    // is why it is written once rather than assumed everywhere.
    auto* warning = new QLabel(
        QStringLiteral("Audio is not exported. It is here to animate against; an exported "
                       "sequence has no sound in it."),
        this);
    warning->setWordWrap(true);
    layout->addWidget(warning);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Import"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

AudioImportDialog::Answer AudioImportDialog::answer() const {
    Answer out;
    out.start_frame = start_ ? start_->value() : 1;
    // No box means nothing to extend, which is a false rather than a default
    // somebody has to remember.
    out.extend_shot = extend_ && extend_->isChecked();
    return out;
}
