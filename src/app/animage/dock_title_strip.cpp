// SPDX-License-Identifier: GPL-3.0-or-later
#include "dock_title_strip.h"

#include <QDockWidget>
#include <QStyle>
#include <QStylePainter>
#include <QAbstractButton>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QStyleOptionToolButton>

#include <algorithm>

namespace {

// Qt's own title-bar button, because nothing short of it matched.
//
// `QDockWidgetTitleButton` is private, and a `QToolButton` set up to be
// identical to it is not identical to it: with the same box, the same icon, the
// same icon size and the same strip height -- each measured off the real thing
// rather than derived -- the cross still came out visibly bolder than the one on
// the docked panel. What differs is the painting, so the painting is what is
// copied here, from `QDockWidgetTitleButton::paintEvent`.
//
// The `PM_SmallIconSize` on the last line is the point of the whole class: Qt
// draws the glyph at that metric and **ignores the button's own `iconSize()`**,
// which is why every attempt to fix this by setting `iconSize` changed nothing.
class TitleButton : public QAbstractButton {
public:
    explicit TitleButton(QWidget* parent) : QAbstractButton(parent) {
        setFocusPolicy(Qt::NoFocus);  // a button that takes the keyboard takes the pen
        setCursor(Qt::ArrowCursor);
    }

    QSize sizeHint() const override {
        ensurePolished();
        int size = 2 * style()->pixelMetric(QStyle::PM_DockWidgetTitleBarButtonMargin, nullptr, this);
        if (!icon().isNull()) {
            const QSize actual = icon().actualSize(iconSize());
            size += std::max(actual.width(), actual.height());
        }
        return QSize(size, size);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void enterEvent(QEnterEvent* event) override {
        if (isEnabled()) update();
        QAbstractButton::enterEvent(event);
    }
    void leaveEvent(QEvent* event) override {
        if (isEnabled()) update();
        QAbstractButton::leaveEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        QStyleOptionToolButton option;
        option.initFrom(this);
        option.state |= QStyle::State_AutoRaise;

        if (style()->styleHint(QStyle::SH_DockWidget_ButtonsHaveFrame, nullptr, this)) {
            if (isEnabled() && underMouse() && !isChecked() && !isDown()) {
                option.state |= QStyle::State_Raised;
            }
            if (isChecked()) option.state |= QStyle::State_On;
            if (isDown()) option.state |= QStyle::State_Sunken;
            style()->drawPrimitive(QStyle::PE_PanelButtonTool, &option, &painter, this);
        }

        option.icon = icon();
        option.subControls = {};
        option.activeSubControls = {};
        option.features = QStyleOptionToolButton::None;
        option.arrowType = Qt::NoArrow;
        // The button's own icon size, which has been measured to match what Qt
        // paints. `QDockWidgetTitleButton` uses PM_SmallIconSize here and gets a
        // smaller cross than that metric describes -- copying the line copied
        // the wrong answer.
        option.iconSize = iconSize();
        style()->drawComplexControl(QStyle::CC_ToolButton, &option, &painter, this);
    }
};

}  // namespace

DockTitleStrip::DockTitleStrip(QDockWidget* dock, int height, const QIcon& close_icon,
                               QSize close_size)
    : QWidget(dock), dock_(dock), height_(height) {
    close_ = new TitleButton(this);
    // Qt's own glyph, not a standardIcon asked for here: the two are different
    // pixmaps, and asking for one by name got a bigger cross three times.
    close_->setIcon(close_icon.isNull()
                        ? style()->standardIcon(QStyle::SP_TitleBarCloseButton, nullptr, this)
                        : close_icon);
    // The box first, because the glyph's size is worked out from it below.
    if (close_size.isValid()) close_->setFixedSize(close_size);
    // Only for sizeHint's arithmetic; the glyph itself is drawn at
    // PM_SmallIconSize whatever this says. See TitleButton::paintEvent.
    // **The glyph is drawn to fit the button's *inner* area, not to `iconSize`.**
    //
    // This is the whole of what five earlier attempts got wrong, so it is worth
    // stating plainly. `iconSize()` on a dock title button is an upper bound --
    // it reads 16 px -- and the cross Qt actually paints is 9 px across. The
    // button is 20 px with `PM_DockWidgetTitleBarButtonMargin` of 5 on each
    // side, which leaves 10 px of inside; a 10 px render of that same icon has
    // 9 px of ink. That is the number, and it is arrived at rather than
    // measured off a screenshot.
    //
    // Every metric named instead of this one -- PM_SmallIconSize,
    // PM_ButtonIconSize, sizeHint's formula, the sub-element rect -- reports the
    // bound and so drew a visibly larger cross on a floating panel than the same
    // panel wore docked.
    const int gap = style()->pixelMetric(QStyle::PM_DockWidgetTitleBarButtonMargin, nullptr, this);
    const int box = close_->width() > 0
                        ? close_->width()
                        : style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this) + 2 * gap;
    const int glyph = std::max(1, box - 2 * gap);
    close_->setIconSize(QSize(glyph, glyph));
    close_->setToolTip(QStringLiteral("Hide this panel (View > %1)").arg(dock->windowTitle()));
    connect(close_, &QAbstractButton::clicked, dock_, &QDockWidget::close);

    // The name is painted rather than held in a label, so it is drawn wherever
    // and however the style draws a dock title -- elided, aligned and coloured
    // to match the docked panel without any of that being restated here.
    connect(dock_, &QDockWidget::windowTitleChanged, this, [this] {
        update();
        placeButton();
    });
}

QSize DockTitleStrip::sizeHint() const { return QSize(0, height_); }

// The title, and **deliberately no background behind it**.
//
// `CE_DockWidgetTitle` was used here first, and it draws the grey bar a docked
// panel wears. That is right when the panel is in the window and wrong when it
// is floating: a floating panel is its own top-level window, and on Windows 11
// those have rounded corners, so a square grey bar inside them meets the
// rounding badly at both ends. Reported.
//
// Squaring the window off instead would mean `DwmSetWindowAttribute` and a
// Windows-only dependency for a cosmetic difference. Drawing no background at
// all is portable, is less code, and leaves the panel one colour from its title
// to its contents -- which is what a floating panel should look like anyway.
//
// The text is drawn through the style rather than by hand so it keeps the
// palette role, the alignment and the mnemonic handling a dock title has.
void DockTitleStrip::paintEvent(QPaintEvent*) {
    if (!dock_) return;
    QStylePainter painter(this);

    const int margin = style()->pixelMetric(QStyle::PM_DockWidgetTitleMargin, nullptr, this);
    // Up to the button, so a long name is elided rather than drawn under it.
    const int until = (close_ && close_->isVisible()) ? close_->x() - margin : width() - margin;
    const QRect where(margin, 0, std::max(0, until - margin), height());
    if (where.isEmpty()) return;

    const QString elided =
        fontMetrics().elidedText(dock_->windowTitle(), Qt::ElideRight, where.width());
    style()->drawItemText(&painter, where, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextHideMnemonic,
                          palette(), isEnabled(), elided, QPalette::WindowText);
}

void DockTitleStrip::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    placeButton();
}

// The button sits at the trailing edge, inset by the margin the style uses for
// a dock title.
//
// **Not `SE_DockWidgetCloseButton`**, which was tried: that sub-element rect is
// computed against the *dock widget's* geometry rather than the title bar's, so
// asking for it here returns a rectangle outside this widget entirely. Caught by
// a test asserting the button lands inside the strip, which is the kind of thing
// worth asserting precisely because it looks obviously true.
void DockTitleStrip::placeButton() {
    if (!dock_ || !close_) return;
    const bool closable = dock_->features().testFlag(QDockWidget::DockWidgetClosable);
    close_->setVisible(closable);
    if (!closable) return;

    const int margin = style()->pixelMetric(QStyle::PM_DockWidgetTitleMargin, nullptr, this);
    const QSize wanted = close_->sizeHint();
    // Clamped, because a strip that has not been laid out yet is zero wide and
    // would otherwise put the button at a negative x -- outside itself, where
    // nothing can be clicked. Offscreen tests meet this; a real window does not.
    close_->setGeometry(std::max(0, width() - wanted.width() - margin),
                        std::max(0, (height() - wanted.height()) / 2), wanted.width(),
                        wanted.height());
}
