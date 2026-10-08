// SPDX-License-Identifier: MIT
#pragma once

#include <QFlexDock/Global.h>

#if defined(QFLEXDOCKQUICK_STATIC)
#  define QFLEXDOCKQUICK_EXPORT
#elif defined(QFLEXDOCKQUICK_BUILD)
#  define QFLEXDOCKQUICK_EXPORT Q_DECL_EXPORT
#else
#  define QFLEXDOCKQUICK_EXPORT Q_DECL_IMPORT
#endif
