// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Writes a report next to the executable when the process dies of something it
// cannot handle. A crash that only happens on someone else's machine is
// otherwise a guessing game, and guessing has already cost more time here than
// this file did.
void installCrashHandler();
