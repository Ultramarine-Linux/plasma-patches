// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "inhibit.h"

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>

#include <cstdio>

#include <fcntl.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    ShutdownInhibitor::ShutdownInhibitor(const QString &why) {
        QDBusConnection bus = QDBusConnection::systemBus();
        if (!bus.isConnected()) {
            std::fprintf(stderr, "%s\n",
                         qPrintable(i18nc("@info:status", "Warning: could not keep the device awake: no system bus")));
            return;
        }
        QDBusMessage call = QDBusMessage::createMethodCall(u"org.freedesktop.login1"_s, u"/org/freedesktop/login1"_s,
                                                           u"org.freedesktop.login1.Manager"_s, u"Inhibit"_s);
        call << u"shutdown:sleep"_s << QString::fromLatin1(inhibitorName) << why << u"block"_s;
        call.setInteractiveAuthorizationAllowed(false);
        const QDBusReply<QDBusUnixFileDescriptor> reply = bus.call(call);
        if (!reply.isValid() || !reply.value().isValid()) {
            std::fprintf(stderr, "%s\n",
                         qPrintable(i18nc("@info:status %1 is an error message",
                                          "Warning: could not keep the device awake while installing: %1",
                                          reply.error().message())));
            return;
        }
        m_fd = fcntl(reply.value().fileDescriptor(), F_DUPFD_CLOEXEC, 0);
    }

    ShutdownInhibitor::~ShutdownInhibitor() {
        if (m_fd >= 0) {
            close(m_fd);
        }
    }

} // namespace UltramarineUpdates
