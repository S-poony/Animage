// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QProxyStyle>
#include <QRect>
#include <QSlider>
#include <QStyle>
#include <QStyleOption>

// Room for a slider handle that is drawn bigger than the space reserved for it.
//
// **Issue #75, and it is the windows11 style rather than anything here.** That
// style publishes `PM_SliderLength` = 16 and returns a 16-pixel
// `SC_SliderHandle` rect, and then paints a circle about twenty pixels across
// centred on it. In the middle of the travel the two spare pixels either side
// land on the groove and nobody sees them. At the ends they land outside the
// widget -- a horizontal `QSlider` is laid out at its size hint, so the handle
// rect at the minimum is flush with the widget's left edge and at the maximum
// flush with its right -- and `QWidget` clips a widget's painting to its own
// rect. So the handle is sliced flat on the outer side at 0% and at 100%.
//
// Nothing about the panel causes it and no width fixes it: the handle rect hugs
// the widget's edge whatever the slider's length is.
//
// The cure is to make the style reserve what it draws. `kRoom` is added to the
// handle's length, which moves its centre in from each end by half that, and the
// groove is pulled in by the same half at each end.
//
// **Both halves are needed, and both numbers are measured** -- with
// `shots -platform windows the-opacity-slider-at-both-ends`, which is the
// picture this was chosen from. Offscreen falls back to the fusion style, whose
// handle fits its rect and which shows none of this.
//
//   - 2 is not enough: the circle is still flattened on the outer side.
//   - 4 clears it exactly.
//   - 6 also clears it, and shortens the travel for nothing.
//   - Without pulling the groove in, 4 leaves a sliver of groove showing past
//     the circle that used to cover it -- teal at 0%, grey at 100%. Trading one
//     visible defect for another.
//
// Harmless on a style that does not need it. Fusion's handle already fits, so
// this costs it four pixels of travel out of a couple of hundred and nothing
// else, which is worth not having a second code path that has to guess which
// style is running.
class SliderHandleRoom : public QProxyStyle {
public:
    // No base style handed in. QProxyStyle *takes ownership* of one that is, so
    // `new SliderHandleRoom(QApplication::style())` makes this own the
    // application's style and destroying it takes the whole program's painting
    // down with it. Left unset, the base is the application style and is not
    // owned.
    SliderHandleRoom() = default;

    static constexpr int kRoom = 4;

    int pixelMetric(PixelMetric metric, const QStyleOption* option,
                    const QWidget* widget) const override {
        const int base = QProxyStyle::pixelMetric(metric, option, widget);
        return metric == PM_SliderLength ? base + kRoom : base;
    }

    QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option,
                         SubControl which, const QWidget* widget) const override {
        QRect rect = QProxyStyle::subControlRect(control, option, which, widget);
        if (control == CC_Slider && which == SC_SliderGroove) {
            rect.adjust(kRoom / 2, 0, -kRoom / 2, 0);
        }
        return rect;
    }
};

// Installs it, and keeps it alive as long as the slider is. `setStyle` does not
// take ownership, so the style is parented to the slider instead of leaked.
inline void giveTheHandleRoom(QSlider* slider) {
    if (!slider) return;
    auto* room = new SliderHandleRoom;
    room->setParent(slider);
    slider->setStyle(room);
}
