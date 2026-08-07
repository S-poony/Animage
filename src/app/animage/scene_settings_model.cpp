// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene_settings_model.h"

#include <algorithm>
#include <cmath>

// The ratio is recognised as one of the named ones when it matches closely
// enough that the numbers really mean it. 1.7778:1 is 16:9 however it was
// written.
namespace {

constexpr double kRatioTolerance = 0.002;

bool sameRatio(double w1, double h1, double w2, double h2) {
    if (h1 <= 0.0 || h2 <= 0.0) return false;
    return std::abs(w1 / h1 - w2 / h2) <= kRatioTolerance;
}

const double kNamedRatios[3][2] = {{16.0, 9.0}, {4.0, 3.0}, {1.0, 1.0}};

}  // namespace

SceneSettingsModel::SceneSettingsModel(QObject* parent) : QObject(parent) {}

int SceneSettingsModel::length() const { return length_; }
double SceneSettingsModel::seconds() const {
    return framerate_ > 0 ? static_cast<double>(length_) / framerate_ : 0.0;
}

void SceneSettingsModel::setAll(int framerate, int width, int height) {
    setAll(framerate, width, height, length_);
}

void SceneSettingsModel::setAll(int framerate, int width, int height, int length) {
    framerate_ = std::clamp(framerate, 1, 120);
    width_ = std::clamp(width, 16, 16384);
    height_ = std::clamp(height, 16, 16384);
    length_ = std::clamp(length, 0, 10000);
    readPixels();
    Q_EMIT framerateChanged();
    Q_EMIT pixelsChanged();
    Q_EMIT aspectChanged();
    Q_EMIT ratioChanged();
    Q_EMIT resolutionChanged();
    Q_EMIT lengthChanged();
}

void SceneSettingsModel::setFramerate(int fps) {
    const int clamped = std::clamp(fps, 1, 120);
    if (clamped == framerate_) return;
    framerate_ = clamped;
    Q_EMIT framerateChanged();
    Q_EMIT lengthChanged();
}

void SceneSettingsModel::setLength(int length) {
    const int clamped = std::clamp(length, 0, 10000);
    if (clamped == length_) return;
    length_ = clamped;
    Q_EMIT lengthChanged();
}

// Ratio and slider -> pixels. The slider is the height, so the number on it is
// one an animator already recognises rather than an abstract multiplier.
void SceneSettingsModel::applyRatioToPixels() {
    if (updating_) return;
    updating_ = true;
    const int old_height = height_;
    height_ = std::clamp(height_, 16, 16384);
    width_ = std::clamp(static_cast<int>(std::llround(height_ * ratio_w_ / ratio_h_)), 16, 16384);
    if (width_ == 16 && height_ * ratio_w_ / ratio_h_ < 16) height_ = width_ = 16;
    if (height_ != old_height) Q_EMIT resolutionChanged();
    Q_EMIT pixelsChanged();
    updating_ = false;
}

// Pixels -> ratio and slider, after the width or height was typed in.
void SceneSettingsModel::readPixels() {
    if (updating_) return;
    updating_ = true;
    ratio_w_ = width_;
    ratio_h_ = height_;
    syncAspectToRatio();
    Q_EMIT ratioChanged();
    Q_EMIT aspectChanged();
    Q_EMIT resolutionChanged();
    updating_ = false;
}

// Selects the named ratio if the numbers are one, and Custom if they are not.
// The menu follows the numbers rather than constraining them.
void SceneSettingsModel::syncAspectToRatio() {
    int named = kCustomIndex;
    for (int i = 0; i < kCustomIndex; ++i) {
        if (sameRatio(ratio_w_, ratio_h_, kNamedRatios[i][0], kNamedRatios[i][1])) {
            named = i;
            break;
        }
    }
    aspect_ = named;
}

void SceneSettingsModel::setAspectIndex(int index) {
    if (index < 0 || index >= kCustomIndex) return;  // Custom is not a ratio to choose
    if (updating_) return;
    updating_ = true;
    aspect_ = index;
    ratio_w_ = kNamedRatios[index][0];
    ratio_h_ = kNamedRatios[index][1];
    applyRatioToPixels();
    Q_EMIT aspectChanged();
    Q_EMIT ratioChanged();
    updating_ = false;
}

void SceneSettingsModel::setRatioWidth(double w) {
    if (updating_ || w <= 0.0) return;
    updating_ = true;
    ratio_w_ = w;
    syncAspectToRatio();
    applyRatioToPixels();
    Q_EMIT ratioChanged();
    Q_EMIT aspectChanged();
    updating_ = false;
}

void SceneSettingsModel::setRatioHeight(double h) {
    if (updating_ || h <= 0.0) return;
    updating_ = true;
    ratio_h_ = h;
    syncAspectToRatio();
    applyRatioToPixels();
    Q_EMIT ratioChanged();
    Q_EMIT aspectChanged();
    updating_ = false;
}

void SceneSettingsModel::setWidth(int width) {
    const int clamped = std::clamp(width, 16, 16384);
    if (clamped == width_) return;
    width_ = clamped;
    readPixels();
}

void SceneSettingsModel::setHeight(int height) {
    const int clamped = std::clamp(height, 16, 16384);
    if (clamped == height_) return;
    height_ = clamped;
    readPixels();
}

void SceneSettingsModel::setResolution(int height) {
    if (updating_) return;
    updating_ = true;
    const int clamped = std::clamp(height, 16, 16384);
    height_ = clamped;
    applyRatioToPixels();
    Q_EMIT resolutionChanged();
    updating_ = false;
}
