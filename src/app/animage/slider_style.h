// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QProxyStyle>
#include <QSlider>

// Keeps a slider's handle whole at both ends of its travel.
//
// **This is a hack. It works by side effect, and the side effect is the whole
// of it** — the class below overrides nothing and changes no metric. It exists
// to be *installed*, because installing it is what changes the drawing.
// [#81](https://github.com/S-poony/Animage/issues/81) is the thing to delete it
// against.
//
// **What was observed**, on Qt 6.11.1 on Windows 11, in the running application
// — [#75](https://github.com/S-poony/Animage/issues/75):
//
//   - Before this existed, the handle was cut flat on the outer side at 0% and
//     at 100%. The screenshot on that issue is it: a handle about 17×18 pixels,
//     in a groove six rows tall.
//   - With a `QProxyStyle` on the slider, Qt draws a *different* handle — a ring
//     about twelve pixels across, in a groove four rows tall — and that one is
//     not cut at either end.
//   - The maintainer confirmed both, on the same machine and the same Qt, by
//     looking at the program.
//
// **Why it happens is not known, and a plausible reason would be worse than
// none.** The reading — offered as a reading and not as a measurement — is that
// `QWindowsVistaStyle`, which `QWindows11Style` descends from, draws some
// controls through the native Windows theme, and that a widget whose style is a
// proxy no longer takes that path. Nobody has confirmed it. What is confirmed is
// the two drawings and which of them is cut.
//
// **Do not try to verify this with `shots`.** It cannot see it, with or without
// `-platform windows`. `QWidget::grab()` produces the ring — the *second*
// drawing — whether or not the proxy is installed, so `shots` reported "the
// handle is whole and nowhere near the edge" both before the fix and after, and
// measurements taken from it are measurements of a control the program has never
// put on screen. A screenshot of the running application is the only instrument
// that answers this. See "the picture `shots` cannot take" in docs/handover.md.
//
// **What this replaced, so nobody rebuilds it.** The first version added four
// pixels to `PM_SliderLength` and pulled `SC_SliderGroove` in by two at each
// end, with a confident story about a style painting outside the rect it
// reserves and bounds "measured" at 2, 4 and 6. All of that was wrong. The four
// pixels did nothing but shorten the travel; the story came from reading `shots`
// pictures of the drawing above, which never had the fault. It was already
// installed, and being installed was the entire fix.
//
// **It is fragile, and the fragility is the point of #81.** Nothing here asks
// Qt for anything, so nothing here will fail loudly. A Qt that stops changing
// the drawing when a proxy is present, or that fixes the cut handle, leaves this
// file doing nothing at all — silently, and looking exactly as it does now. If
// the handle ever comes back cut, this is the first place to look and the
// screenshot is the only way to see it.
class PlainSliderStyle : public QProxyStyle {
public:
    // No base style handed in. `QProxyStyle` *takes ownership* of one that is,
    // so `new PlainSliderStyle(QApplication::style())` makes this own the
    // application's style and destroying it takes the whole program's painting
    // down with it. Left unset, the base is the application style and is not
    // owned.
    PlainSliderStyle() = default;
};

// Installs it, and keeps it alive as long as the slider is. `setStyle` does not
// take ownership, so the style is parented to the slider rather than leaked.
inline void keepTheHandleWhole(QSlider* slider) {
    if (!slider) return;
    auto* plain = new PlainSliderStyle;
    plain->setParent(slider);
    slider->setStyle(plain);
}
