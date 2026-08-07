// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>

// Everything that belongs to the scene rather than to a track or a drawing:
// the framerate, and the canvas.
//
// The canvas is expressed twice over, because those are the two ways people
// think about it. An aspect ratio and a resolution is how you decide what you
// are making; a width and a height in pixels is what you have to hand when
// someone tells you what to deliver. Both are editable and each keeps the other
// true, so neither is the "real" one.
//
// The class holds no document: it is the arithmetic of that agreement, in one
// place, so that a dialog can bind to it and a test can drive it. The
// application controller is what applies the result to the scene.
class SceneSettingsModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(int framerate READ framerate WRITE setFramerate NOTIFY framerateChanged)
    Q_PROPERTY(int width READ width WRITE setWidth NOTIFY pixelsChanged)
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY pixelsChanged)
    Q_PROPERTY(int aspectIndex READ aspectIndex WRITE setAspectIndex NOTIFY aspectChanged)
    Q_PROPERTY(QStringList aspectNames READ aspectNames CONSTANT)
    Q_PROPERTY(double ratioWidth READ ratioWidth WRITE setRatioWidth NOTIFY ratioChanged)
    Q_PROPERTY(double ratioHeight READ ratioHeight WRITE setRatioHeight NOTIFY ratioChanged)
    // The resolution slider. Its value is the height in pixels, which is the
    // number an animator already recognises rather than an abstract multiplier.
    Q_PROPERTY(int resolution READ resolution WRITE setResolution NOTIFY resolutionChanged)
    Q_PROPERTY(int minResolution READ minResolution CONSTANT)
    Q_PROPERTY(int maxResolution READ maxResolution CONSTANT)

public:
    explicit SceneSettingsModel(QObject* parent = nullptr);

    int framerate() const { return framerate_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int aspectIndex() const { return aspect_; }
    QStringList aspectNames() const { return names_; }
    double ratioWidth() const { return ratio_w_; }
    double ratioHeight() const { return ratio_h_; }
    int resolution() const { return height_; }
    int minResolution() const { return 16; }
    int maxResolution() const { return 16384; }

    // Whole scene settings, for the preview and for committing. Called from
    // QML when the scene-settings dialog opens, so it must be invokable.
    Q_INVOKABLE void setAll(int framerate, int width, int height);

public Q_SLOTS:
    void setFramerate(int fps);
    void setWidth(int width);
    void setHeight(int height);
    void setAspectIndex(int index);
    void setRatioWidth(double w);
    void setRatioHeight(double h);
    void setResolution(int height);

Q_SIGNALS:
    void framerateChanged();
    void pixelsChanged();
    void aspectChanged();
    void ratioChanged();
    void resolutionChanged();

private:
    void applyRatioToPixels();
    void readPixels();
    void syncAspectToRatio();

    // 16:9, 4:3, 1:1, then Custom. The index that "Custom" means nothing to is
    // the one the numbers have to name when they have a name.
    static constexpr int kCustomIndex = 3;

    int framerate_ = 24;
    int width_ = 1920;
    int height_ = 1080;
    double ratio_w_ = 16.0;
    double ratio_h_ = 9.0;
    int aspect_ = 0;

    QStringList names_{QStringLiteral("16:9"), QStringLiteral("4:3"),
                       QStringLiteral("1:1"), QStringLiteral("Custom")};

    bool updating_ = false;
};
