// SPDX-License-Identifier: GPL-3.0-or-later
//
// M0 of the prototype plan, and deliberately the first thing built. If pen to
// pixel is above 25 ms the tool will be unpleasant whatever is built around it,
// and that has to be known before the rendering architecture is baked in.

#include <QApplication>
#include <QInputDevice>
#include <QPointingDevice>
#include <QTextStream>

#include "latency_canvas.h"

namespace {

void listInputDevices() {
    QTextStream out(stdout);
    out << "Input devices Qt can see:\n";
    bool found_tablet = false;
    for (const QInputDevice* device : QInputDevice::devices()) {
        const bool is_tablet = device->type() == QInputDevice::DeviceType::Stylus ||
                               device->type() == QInputDevice::DeviceType::Airbrush ||
                               device->type() == QInputDevice::DeviceType::Puck;
        found_tablet = found_tablet || is_tablet;
        out << "  " << (is_tablet ? "* " : "  ") << device->name() << "  seat=" << device->seatName()
            << "\n";
    }
    if (!found_tablet) {
        out << "\nNo stylus device listed. Qt often only registers one on the first\n"
               "pen event, so this is not conclusive -- touch the tablet and watch\n"
               "the tablet event counter in the window.\n";
    }
    out << "\nQt uses Windows Ink on Windows by default. WinTab must be measured\n"
           "separately: professional users still run it, and the two paths do not\n"
           "have the same latency.\n\n";
    out.flush();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Animage M0 latency"));

    listInputDevices();

    LatencyCanvas canvas;
    canvas.setWindowTitle(QStringLiteral("Animage M0 - pen latency"));
    canvas.resize(1280, 800);
    canvas.show();

    const int result = app.exec();
    canvas.printReport();
    return result;
}
