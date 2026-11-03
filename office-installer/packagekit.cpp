// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "packagekit.h"

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QLocale>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace OfficeInstaller {

    namespace {
        const auto Service = u"org.freedesktop.PackageKit"_s;
        const auto TransactionInterface = u"org.freedesktop.PackageKit.Transaction"_s;

        // the numbers below are PackageKit 1.x's. Its main branch renumbers them (bit 1 is SIMULATE
        // there), so anything but 1.x is refused instead of being sent the wrong flags
        constexpr uint SupportedVersionMajor = 1;
        // PkInfoEnum
        constexpr std::uint32_t InfoInstalled = 1;
        constexpr std::uint32_t InfoAvailable = 2;
        // PkExitEnum
        constexpr uint ExitSuccess = 1;
        constexpr uint ExitCancelled = 3;
        // PkFilterEnum, as bit numbers: native arch, no source packages, newest version only
        constexpr qulonglong FilterNotSource = 1ULL << 21U;
        constexpr qulonglong FilterArch = 1ULL << 18U;
        constexpr qulonglong FilterNewest = 1ULL << 16U;
        // PkTransactionFlagEnum, as a bit number
        constexpr qulonglong FlagOnlyTrusted = 1ULL << 1U;

        // polkit can keep InstallPackages waiting while the user types their password
        constexpr int CallTimeout = 10 * 60 * 1000;

        QString packageName(const QString &packageId) { return packageId.section(u';', 0, 0); }

        /// the daemon's major version, which also starts it if it isn't running
        std::optional<uint> daemonVersionMajor(QString *error) {
            QDBusMessage call = QDBusMessage::createMethodCall(Service, u"/org/freedesktop/PackageKit"_s,
                                                               u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
            call << Service << u"VersionMajor"_s;
            const QDBusReply<QDBusVariant> reply = QDBusConnection::systemBus().call(call);
            if (!reply.isValid()) {
                *error = reply.error().message();
                return std::nullopt;
            }
            return reply.value().variant().toUInt();
        }
    } // namespace

    QStringList packageIdsToInstall(const QList<ResolvedPackage> &resolved, const QStringList &names,
                                    QStringList *missing) {
        QStringList ids;
        for (const QString &name: names) {
            const auto matches = [&](const ResolvedPackage &package, const std::uint32_t info) {
                return package.info == info && packageName(package.id) == name;
            };
            if (const auto available = std::ranges::find_if(
                    resolved, [&](const ResolvedPackage &package) { return matches(package, InfoAvailable); });
                available != resolved.end()) {
                ids.append(available->id);
            } else if (std::ranges::none_of(
                           resolved, [&](const ResolvedPackage &package) { return matches(package, InfoInstalled); })) {
                missing->append(name);
            }
        }
        return ids;
    }

    PackageKitInstaller::PackageKitInstaller(const QStringList &packages, QObject *parent) :
        Installer(parent), m_packages(packages) {}

    void PackageKitInstaller::start() {
        m_resolved.clear();
        m_error.clear();
        m_canceled = false;
        m_step = Step::Resolving;
        QString error;
        const std::optional<uint> version = daemonVersionMajor(&error);
        if (!version) {
            fail(error);
            return;
        }
        if (*version != SupportedVersionMajor) {
            fail(i18nc("@info %1 is a version number", "PackageKit %1 isn't supported. Only PackageKit 1 is.",
                       *version));
            return;
        }
        startTransaction(u"Resolve"_s,
                         {QVariant::fromValue(FilterArch | FilterNotSource | FilterNewest), QVariant(m_packages)});
    }

    void PackageKitInstaller::cancel() {
        if (m_step == Step::Idle) {
            return;
        }
        m_canceled = true;
        if (m_transaction.path().isEmpty()) {
            return;
        }
        QDBusConnection::systemBus().asyncCall(
            QDBusMessage::createMethodCall(Service, m_transaction.path(), TransactionInterface, u"Cancel"_s));
    }

    bool PackageKitInstaller::startTransaction(const QString &method, const QVariantList &arguments) {
        QDBusConnection bus = QDBusConnection::systemBus();
        if (!m_transaction.path().isEmpty()) {
            for (const auto &[name, slot]: {std::pair{"Package", SLOT(onPackage(uint, QString, QString))},
                                            std::pair{"ErrorCode", SLOT(onErrorCode(uint, QString))},
                                            std::pair{"Finished", SLOT(onFinished(uint, uint))}}) {
                bus.disconnect(Service, m_transaction.path(), TransactionInterface, QString::fromLatin1(name), this,
                               slot);
            }
            m_transaction = {};
        }

        const QDBusReply<QDBusObjectPath> transaction = bus.call(
            QDBusMessage::createMethodCall(Service, u"/org/freedesktop/PackageKit"_s, Service, u"CreateTransaction"_s));
        if (!transaction.isValid()) {
            fail(transaction.error().message());
            return false;
        }
        m_transaction = transaction.value();
        const QString path = m_transaction.path();
        bus.connect(Service, path, TransactionInterface, u"Package"_s, this, SLOT(onPackage(uint, QString, QString)));
        bus.connect(Service, path, TransactionInterface, u"ErrorCode"_s, this, SLOT(onErrorCode(uint, QString)));
        bus.connect(Service, path, TransactionInterface, u"Finished"_s, this, SLOT(onFinished(uint, uint)));

        // interactive lets polkit ask for a password instead of refusing
        QDBusMessage hints = QDBusMessage::createMethodCall(Service, path, TransactionInterface, u"SetHints"_s);
        hints << QStringList{u"interactive=true"_s, u"locale=%1"_s.arg(QLocale().name())};
        if (const QDBusMessage reply = bus.call(hints); reply.type() == QDBusMessage::ErrorMessage) {
            fail(reply.errorMessage());
            return false;
        }

        QDBusMessage call = QDBusMessage::createMethodCall(Service, path, TransactionInterface, method);
        call.setArguments(arguments);
        auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call, CallTimeout), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
            watcher->deleteLater();
            if (const QDBusPendingReply<> reply = *watcher; reply.isError()) {
                fail(reply.error().message());
            }
        });
        return true;
    }

    void PackageKitInstaller::onPackage(const uint info, const QString &packageId, const QString &summary) {
        Q_UNUSED(summary)
        if (m_step == Step::Resolving) {
            m_resolved.append({.info = info, .id = packageId});
        } else if (m_step == Step::Installing) {
            Q_EMIT progress(packageName(packageId));
        }
    }

    void PackageKitInstaller::onErrorCode(const uint code, const QString &details) {
        Q_UNUSED(code)
        m_error = details;
    }

    void PackageKitInstaller::onFinished(const uint exit, const uint runtime) {
        Q_UNUSED(runtime)
        if (m_step == Step::Idle) {
            return;
        }
        if (exit != ExitSuccess) {
            if (exit == ExitCancelled) {
                m_canceled = true;
            }
            fail(m_error);
            return;
        }
        if (m_step == Step::Installing) {
            m_step = Step::Idle;
            Q_EMIT finished(true, false, {});
            return;
        }

        if (m_canceled) {
            fail({});
            return;
        }
        QStringList missing;
        const QStringList ids = packageIdsToInstall(m_resolved, m_packages, &missing);
        if (!missing.isEmpty()) {
            fail(i18nc("@info %1 is a list of package names", "Packages not found: %1",
                       QLocale().createSeparatedList(missing)));
            return;
        }
        if (ids.isEmpty()) {
            m_step = Step::Idle;
            Q_EMIT finished(true, false, {});
            return;
        }
        m_step = Step::Installing;
        startTransaction(u"InstallPackages"_s, {QVariant::fromValue(FlagOnlyTrusted), QVariant(ids)});
    }

    void PackageKitInstaller::fail(const QString &error) {
        if (m_step == Step::Idle) {
            return;
        }
        m_step = Step::Idle;
        Q_EMIT finished(false, m_canceled, error);
    }

} // namespace OfficeInstaller
