// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene_settings_dialog.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <numeric>

#include "scene.h"

namespace {

struct AspectPreset {
    const char* name;
    double width;
    double height;
};

// The ratios worth having a name for. A paper is here because storyboards and
// print work in it; it is written out as a decimal rather than as a root sign,
// which is also what goes in the ratio fields when it is chosen.
const AspectPreset kPresets[] = {
    {"1:1", 1.0, 1.0},
    {"4:3", 4.0, 3.0},
    {"3:2", 3.0, 2.0},
    {"16:10", 16.0, 10.0},
    {"16:9", 16.0, 9.0},
    {"21:9", 21.0, 9.0},
    {"A paper, landscape (1.4142:1)", 1.41421356, 1.0},
    {"A paper, portrait (1:1.4142)", 1.0, 1.41421356},
};
constexpr int kPresetCount = static_cast<int>(std::size(kPresets));

// The slider is the height in pixels. 240 is a rough at postcard size, 4320 is
// 8K; between them is everything anyone will draw on a desktop.
constexpr int kMinResolution = 240;
constexpr int kMaxResolution = 4320;

// Ratios are compared proportionally, not by their numbers: 16:9 and 1.7778:1
// are the same shape and the menu should say so.
bool sameShape(double a_w, double a_h, double b_w, double b_h) {
    if (a_h <= 0.0 || b_h <= 0.0) return false;
    const double a = a_w / a_h;
    const double b = b_w / b_h;
    return std::abs(a - b) <= 0.001 * std::max(1.0, b);
}

}  // namespace

SceneSettingsDialog::SceneSettingsDialog(int framerate, int width, int height, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(QStringLiteral("Scene settings"));

    auto* layout = new QVBoxLayout(this);

    auto* timing = new QGroupBox(QStringLiteral("Timing"), this);
    auto* timing_form = new QFormLayout(timing);
    framerate_ = new QSpinBox(timing);
    framerate_->setRange(1, 120);
    framerate_->setValue(framerate);
    framerate_->setSuffix(QStringLiteral(" fps"));
    timing_form->addRow(QStringLiteral("Framerate"), framerate_);
    layout->addWidget(timing);

    auto* canvas = new QGroupBox(QStringLiteral("Canvas"), this);
    auto* canvas_layout = new QVBoxLayout(canvas);
    auto* form = new QFormLayout();
    canvas_layout->addLayout(form);

    aspect_ = new QComboBox(canvas);
    for (const AspectPreset& preset : kPresets) {
        aspect_->addItem(QString::fromUtf8(preset.name));
    }
    aspect_->addItem(QStringLiteral("Custom"));
    form->addRow(QStringLiteral("Aspect ratio"), aspect_);

    auto* ratio_row = new QWidget(canvas);
    auto* ratio_layout = new QHBoxLayout(ratio_row);
    ratio_layout->setContentsMargins(0, 0, 0, 0);
    const auto ratioBox = [&] {
        auto* box = new QDoubleSpinBox(ratio_row);
        box->setRange(0.01, 1000.0);
        box->setDecimals(4);
        box->setSingleStep(0.1);
        box->setKeyboardTracking(false);  // not on every digit typed
        return box;
    };
    ratio_w_ = ratioBox();
    ratio_h_ = ratioBox();
    ratio_layout->addWidget(ratio_w_);
    ratio_layout->addWidget(new QLabel(QStringLiteral(":"), ratio_row));
    ratio_layout->addWidget(ratio_h_);
    ratio_layout->addStretch(1);
    form->addRow(QString(), ratio_row);

    resolution_ = new QSlider(Qt::Horizontal, canvas);
    resolution_->setRange(kMinResolution, kMaxResolution);
    form->addRow(QStringLiteral("Resolution"), resolution_);

    auto* pixel_row = new QWidget(canvas);
    auto* pixel_layout = new QHBoxLayout(pixel_row);
    pixel_layout->setContentsMargins(0, 0, 0, 0);
    const auto pixelBox = [&] {
        auto* box = new QSpinBox(pixel_row);
        box->setRange(animage::kMinCanvasSide, animage::kMaxCanvasSide);
        box->setSuffix(QStringLiteral(" px"));
        box->setKeyboardTracking(false);
        return box;
    };
    pixels_w_ = pixelBox();
    pixels_h_ = pixelBox();
    pixel_layout->addWidget(pixels_w_);
    pixel_layout->addWidget(new QLabel(QStringLiteral("×"), pixel_row));
    pixel_layout->addWidget(pixels_h_);
    pixel_layout->addStretch(1);
    form->addRow(QString(), pixel_row);

    layout->addWidget(canvas);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Named so a test can drive them. Every rule here is arithmetic between
    // three controls that all write to each other, which is exactly the kind of
    // thing that looks right in a screenshot and is wrong in a corner.
    framerate_->setObjectName(QStringLiteral("framerate"));
    aspect_->setObjectName(QStringLiteral("aspect"));
    ratio_w_->setObjectName(QStringLiteral("ratioWidth"));
    ratio_h_->setObjectName(QStringLiteral("ratioHeight"));
    resolution_->setObjectName(QStringLiteral("resolution"));
    pixels_w_->setObjectName(QStringLiteral("pixelWidth"));
    pixels_h_->setObjectName(QStringLiteral("pixelHeight"));

    // Seeded from the scene before anything is connected, so filling the
    // controls in does not count as an edit.
    updating_ = true;
    pixels_w_->setValue(width);
    pixels_h_->setValue(height);
    updating_ = false;
    readPixels();

    connect(aspect_, &QComboBox::currentIndexChanged, this, &SceneSettingsDialog::chooseAspect);
    connect(ratio_w_, &QDoubleSpinBox::valueChanged, this, &SceneSettingsDialog::ratioEdited);
    connect(ratio_h_, &QDoubleSpinBox::valueChanged, this, &SceneSettingsDialog::ratioEdited);
    connect(resolution_, &QSlider::valueChanged, this, &SceneSettingsDialog::applyRatioToPixels);
    connect(pixels_w_, &QSpinBox::valueChanged, this, &SceneSettingsDialog::readPixels);
    connect(pixels_h_, &QSpinBox::valueChanged, this, &SceneSettingsDialog::readPixels);

    // Whatever moved, the canvas behind should show it. Coalesced on a short
    // timer: a slider drag emits a value per pixel of travel and each one would
    // otherwise re-solve every colour layer in the scene.
    preview_ = new QTimer(this);
    preview_->setSingleShot(true);
    preview_->setInterval(120);
    // Qualified: the constructor's `framerate` parameter would otherwise be
    // what the name finds.
    connect(preview_, &QTimer::timeout, this, [this] {
        Q_EMIT previewed(this->framerate(), canvasWidth(), canvasHeight());
    });
    for (QAbstractSpinBox* box : findChildren<QAbstractSpinBox*>()) {
        connect(box, &QAbstractSpinBox::editingFinished, this,
                &SceneSettingsDialog::schedulePreview);
    }
    connect(framerate_, &QSpinBox::valueChanged, this, &SceneSettingsDialog::schedulePreview);
    connect(pixels_w_, &QSpinBox::valueChanged, this, &SceneSettingsDialog::schedulePreview);
    connect(pixels_h_, &QSpinBox::valueChanged, this, &SceneSettingsDialog::schedulePreview);
}

void SceneSettingsDialog::schedulePreview() {
    if (preview_) preview_->start();
}

void SceneSettingsDialog::keyPressEvent(QKeyEvent* event) {
    const bool entered = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    if (entered) {
        if (auto* box = qobject_cast<QAbstractSpinBox*>(focusWidget())) {
            box->interpretText();  // commit what was typed
            schedulePreview();
            event->accept();
            return;
        }
    }
    QDialog::keyPressEvent(event);
}

void SceneSettingsDialog::mousePressEvent(QMouseEvent* event) {
    // Taking focus is the point: a spin box commits its text on losing it, and
    // clicking the dialog background used to leave the caret sitting in the
    // field with the typed number not yet applied to anything.
    setFocus(Qt::MouseFocusReason);
    QDialog::mousePressEvent(event);
}

int SceneSettingsDialog::framerate() const { return framerate_->value(); }
int SceneSettingsDialog::canvasWidth() const { return pixels_w_->value(); }
int SceneSettingsDialog::canvasHeight() const { return pixels_h_->value(); }

void SceneSettingsDialog::applyRatioToPixels() {
    if (updating_) return;
    const double rw = ratio_w_->value();
    const double rh = ratio_h_->value();
    if (rw <= 0.0 || rh <= 0.0) return;

    const int height = resolution_->value();
    const int width = static_cast<int>(std::lround(height * rw / rh));

    updating_ = true;
    pixels_h_->setValue(std::clamp(height, animage::kMinCanvasSide, animage::kMaxCanvasSide));
    pixels_w_->setValue(std::clamp(width, animage::kMinCanvasSide, animage::kMaxCanvasSide));
    updating_ = false;
}

void SceneSettingsDialog::readPixels() {
    if (updating_) return;
    const int width = pixels_w_->value();
    const int height = pixels_h_->value();

    updating_ = true;
    resolution_->setValue(std::clamp(height, kMinResolution, kMaxResolution));

    // Show the ratio the way it is named where there is a name for it, and as
    // whole numbers otherwise. 1920x1080 reads better as 16:9 than as 1920:1080,
    // and a shape with no name reads better reduced than not.
    int match = -1;
    for (int i = 0; i < kPresetCount; ++i) {
        if (sameShape(static_cast<double>(width), static_cast<double>(height), kPresets[i].width,
                      kPresets[i].height)) {
            match = i;
            break;
        }
    }
    if (match >= 0) {
        ratio_w_->setValue(kPresets[match].width);
        ratio_h_->setValue(kPresets[match].height);
        aspect_->setCurrentIndex(match);
    } else {
        const int divisor = std::max(1, std::gcd(width, height));
        ratio_w_->setValue(static_cast<double>(width / divisor));
        ratio_h_->setValue(static_cast<double>(height / divisor));
        aspect_->setCurrentIndex(kPresetCount);  // Custom
    }
    updating_ = false;
}

void SceneSettingsDialog::chooseAspect(int index) {
    if (updating_ || index < 0 || index >= kPresetCount) return;  // Custom names no shape

    updating_ = true;
    ratio_w_->setValue(kPresets[index].width);
    ratio_h_->setValue(kPresets[index].height);
    updating_ = false;
    applyRatioToPixels();
}

void SceneSettingsDialog::ratioEdited() {
    if (updating_) return;
    syncAspectToRatio();
    applyRatioToPixels();
}

void SceneSettingsDialog::syncAspectToRatio() {
    const double rw = ratio_w_->value();
    const double rh = ratio_h_->value();

    int match = kPresetCount;  // Custom
    for (int i = 0; i < kPresetCount; ++i) {
        if (sameShape(rw, rh, kPresets[i].width, kPresets[i].height)) {
            match = i;
            break;
        }
    }

    const bool was = updating_;
    updating_ = true;
    aspect_->setCurrentIndex(match);
    updating_ = was;
}
