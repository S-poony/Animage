// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QSlider;
class QSpinBox;
class QTimer;

// Everything that belongs to the scene rather than to a track or a drawing:
// the framerate, and the canvas.
//
// The canvas is expressed twice over, because those are the two ways people
// think about it. An aspect ratio and a resolution is how you decide what you
// are making; a width and a height in pixels is what you have to hand when
// someone tells you what to deliver. Both are editable and each keeps the other
// true, so neither is the "real" one.
class SceneSettingsDialog : public QDialog {
    Q_OBJECT

public:
    SceneSettingsDialog(int framerate, int width, int height, QWidget* parent = nullptr);

    int framerate() const;
    int canvasWidth() const;
    int canvasHeight() const;

Q_SIGNALS:
    // The settings as they stand, for showing on the canvas while the dialog is
    // still open. Emitted on a short delay: dragging the resolution slider would
    // otherwise ask for a colour solve per tick.
    void previewed(int framerate, int width, int height);

protected:
    // Return in a field commits that field rather than accepting the dialog.
    // The buttons are still reachable by Tab, and Escape still cancels; what is
    // gone is typing a number, pressing Return to enter it, and having the
    // window shut on you.
    void keyPressEvent(QKeyEvent* event) override;
    // Clicking the background takes the keyboard off whichever field was being
    // typed into, so its value is committed and can be seen.
    void mousePressEvent(QMouseEvent* event) override;

private:
    void schedulePreview();
    // Ratio and slider -> pixels. The slider is the height, so the number on it
    // is one an animator already recognises rather than an abstract multiplier.
    void applyRatioToPixels();
    // Pixels -> ratio and slider, after the width or height was typed in.
    void readPixels();
    void chooseAspect(int index);
    void ratioEdited();
    // Selects the named ratio if the numbers are one, and Custom if they are
    // not. The menu follows the numbers rather than constraining them.
    void syncAspectToRatio();

    QSpinBox* framerate_ = nullptr;
    QComboBox* aspect_ = nullptr;
    QDoubleSpinBox* ratio_w_ = nullptr;
    QDoubleSpinBox* ratio_h_ = nullptr;
    QSlider* resolution_ = nullptr;
    QSpinBox* pixels_w_ = nullptr;
    QSpinBox* pixels_h_ = nullptr;
    QTimer* preview_ = nullptr;

    // Every control writes to the others, so each of them would answer back.
    bool updating_ = false;
};
