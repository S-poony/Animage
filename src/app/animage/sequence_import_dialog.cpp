// SPDX-License-Identifier: GPL-3.0-or-later
#include "sequence_import_dialog.h"

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

// What the frames will cost, in the units somebody can act on.
//
// **One frame and not all of them**, which is the difference from the still's
// recap and is not a rounding of the truth. A reference layer's pixels are
// derived and held in a bounded cache, so a two-hundred-frame sequence does not
// weigh two hundred frames: it weighs what is being looked at, and the rest are
// decoded again when somebody scrubs back to them.
QString costOfFrames(int count, int width, int height, bool half) {
    const int shown_w = half ? std::max(1, width / 2) : width;
    const int shown_h = half ? std::max(1, height / 2) : height;
    const std::size_t tiles = image_import::tileCountFor(shown_w, shown_h);
    // A tile is 128x128 RGBA half = exactly 128 KB, so tiles/8 is megabytes.
    const double megabytes = static_cast<double>(tiles) / 8.0;
    return QStringLiteral("%1 %2, %3 x %4 pixels, about %5 MB each while shown")
        .arg(count)
        .arg(count == 1 ? QStringLiteral("picture") : QStringLiteral("pictures"))
        .arg(shown_w)
        .arg(shown_h)
        .arg(megabytes, 0, 'f', megabytes < 10.0 ? 1 : 0);
}

// What the ordering rule did, in sentences rather than in numbers.
//
// Everything here is an account and nothing is a warning: the import happens
// either way. The house rule is to let the input in and explain it, and this is
// the explaining half -- without it, "numeric and not correctable" would be a
// rule somebody meets by noticing their frames are in the wrong order.
QString accountOfOrder(const image_import::Ordering& order) {
    QStringList said;

    if (order.numbered > 0 && order.schemes == 1) {
        said.append(order.gaps
                        ? QStringLiteral("Numbered %1 to %2, with gaps -- %3 files for %4 numbers. "
                                         "A missing number is a frame the sequence does not have; "
                                         "the frames after it keep their place rather than sliding "
                                         "back to close it.")
                              .arg(order.first)
                              .arg(order.last)
                              .arg(order.numbered)
                              .arg(order.last - order.first + 1)
                        : QStringLiteral("Numbered %1 to %2, in order.")
                              .arg(order.first)
                              .arg(order.last));
    } else if (order.numbered > 0) {
        said.append(QStringLiteral("These files are named %1 different ways. Each way is kept "
                                   "together and put in its own numeric order, one group after "
                                   "another, rather than the numbers being mixed.")
                        .arg(order.schemes));
    }

    if (order.unnumbered == 1) {
        said.append(QStringLiteral("One of them has no number in its name, so there is nothing to "
                                   "order it by. It is put last."));
    } else if (order.unnumbered > 1) {
        said.append(QStringLiteral("%1 of them have no number in the name, so there is nothing to "
                                   "order them by. They are put last, in alphabetical order.")
                        .arg(order.unnumbered));
    }

    if (said.isEmpty()) said.append(QStringLiteral("In order."));
    return said.join(QStringLiteral(" "));
}

}  // namespace

SequenceImportDialog::SequenceImportDialog(const Found& found, int playhead_frame, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Import image sequence"));

    auto* layout = new QVBoxLayout(this);

    const int count = static_cast<int>(found.ordering.paths.size());

    auto* what = new QGroupBox(QStringLiteral("What was found"), this);
    auto* what_layout = new QVBoxLayout(what);

    auto* cost = new QLabel(costOfFrames(count, found.width, found.height, false), what);
    cost->setWordWrap(true);
    what_layout->addWidget(cost);

    auto* order = new QLabel(accountOfOrder(found.ordering), what);
    order->setWordWrap(true);
    what_layout->addWidget(order);

    if (found.unreadable > 0) {
        // Kept rather than dropped, and the reason is the same one that puts a
        // gap where a number is missing: the position in the list is what each
        // drawing points at, so removing one would move every frame after it.
        auto* bad = new QLabel(
            found.unreadable == 1
                ? QStringLiteral("One of them cannot be read, and that frame will be blank. It "
                                 "keeps its place, so the rest are not moved.")
                : QStringLiteral("%1 of them cannot be read, and those frames will be blank. "
                                 "They keep their place, so the rest are not moved.")
                      .arg(found.unreadable),
            what);
        bad->setWordWrap(true);
        what_layout->addWidget(bad);
    }

    // Said rather than offered, because neither is a choice here. Exposure is
    // on 1s because an image sequence has no frame rate of its own and
    // inventing one is inventing information; the track ends rather than holds
    // because that is what an animation does, and it is the track's own setting
    // afterwards.
    auto* rules = new QLabel(
        QStringLiteral("They land on a new track, one drawing each, and the track shows nothing "
                       "past the last one. Nothing is added to what the project saves: the "
                       "pictures are shown from their files."),
        what);
    rules->setWordWrap(true);
    what_layout->addWidget(rules);
    layout->addWidget(what);

    auto* choices = new QGroupBox(QStringLiteral("How to bring them in"), this);
    auto* form = new QFormLayout(choices);

    start_ = new QSpinBox(choices);
    // Up to the length of a long shot and then some. Not bounded by the scene,
    // because a sequence may be what makes the scene longer.
    start_->setRange(1, 100000);
    start_->setValue(1);
    // The playhead is where somebody is working, so it is worth being able to
    // reach in one move -- but it is not the default. Frame 1 is what an
    // animatic or a reference for the whole shot wants, and defaulting to
    // wherever the playhead happens to be left would put it there by accident.
    start_->setToolTip(
        QStringLiteral("Which frame the first picture lands on. The playhead is at %1.")
            .arg(std::max(1, playhead_frame)));
    form->addRow(QStringLiteral("Start at frame"), start_);

    half_ = new QCheckBox(QStringLiteral("Import at half size"), choices);
    half_->setToolTip(
        QStringLiteral("A quarter of the memory, for a reference you look at rather than export. "
                       "It is a placement of 50%% and not a separate kind of import, so it can be "
                       "changed afterwards with \"Transform layer through time\" in the layer "
                       "panel, which writes nothing on an import."));
    form->addRow(QString(), half_);
    layout->addWidget(choices);

    // The cost line follows the checkbox, because half size is exactly the
    // thing that changes it and a number that did not move would read as a
    // control that does nothing.
    // No `this` in the capture: costOfFrames is a free function above, so Apple
    // Clang's -Wunused-lambda-capture makes capturing it a build failure under
    // -Werror while GCC and MSVC say nothing. The `this` two arguments along is
    // the context object and is a different thing -- it is what disconnects this
    // when the dialog goes.
    connect(half_, &QCheckBox::toggled, this, [cost, count, found](bool on) {
        cost->setText(costOfFrames(count, found.width, found.height, on));
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Import"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

SequenceImportDialog::Answer SequenceImportDialog::answer() const {
    Answer out;
    out.start_frame = start_ ? start_->value() : 1;
    out.half_size = half_ && half_->isChecked();
    return out;
}
