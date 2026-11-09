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
#include <array>
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
        constexpr uint ExitKeyRequired = 4;
        constexpr uint ExitNeedUntrusted = 8;
        constexpr uint ExitCancelledPriority = 9;
        // PkFilterEnum, as bit numbers: native arch, no source packages, newest version only
        constexpr qulonglong FilterNotSource = 1ULL << 21U;
        constexpr qulonglong FilterArch = 1ULL << 18U;
        constexpr qulonglong FilterNewest = 1ULL << 16U;
        // PkTransactionFlagEnum, as a bit number
        constexpr qulonglong FlagOnlyTrusted = 1ULL << 1U;

        // polkit can keep InstallPackages waiting while the user types their password
        constexpr int CallTimeout = 10 * 60 * 1000;

        QString packageName(const QString &packageId) { return packageId.section(u';', 0, 0); }

        /// the transaction signals we follow, with the slots they're connected to by name
        struct TransactionSignal {
            const char *name;
            const char *slot;
        };
        std::array<TransactionSignal, 4> transactionSignals() {
            return {{
                {.name = "Package", .slot = SLOT(onPackage(uint, QString, QString))},
                {.name = "ErrorCode", .slot = SLOT(onErrorCode(uint, QString))},
                {.name = "RepoSignatureRequired",
                 .slot = SLOT(
                     onRepoSignatureRequired(QString, QString, QString, QString, QString, QString, QString, uint))},
                {.name = "Finished", .slot = SLOT(onFinished(uint, uint))},
            }};
        }

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

    QString failureReason(const uint exit, const QString &error, const QString &unsignedRepository) {
        switch (exit) {
        case ExitKeyRequired:
            if (!unsignedRepository.isEmpty()) {
                return i18nc("@info %1 is a package repository",
                             "The signing key of the “%1” repository isn't trusted yet.", unsignedRepository);
            }
            return i18nc("@info", "The signing key of a package repository isn't trusted yet.");
        case ExitNeedUntrusted:
            return i18nc("@info", "Some packages aren't signed with a trusted key.");
        case ExitCancelledPriority:
            return i18nc("@info", "Another software install or update took priority. Try again once it has finished.");
        default:
            return error;
        }
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
            for (const auto &[name, slot]: transactionSignals()) {
                bus.disconnect(Service, m_transaction.path(), TransactionInterface, QString::fromLatin1(name), this,
                               slot);
            }
            m_transaction = {};
        }
        // what went wrong is told per transaction, so don't carry it over from the last one
        m_error.clear();
        m_unsignedRepository.clear();

        const QDBusReply<QDBusObjectPath> transaction = bus.call(
            QDBusMessage::createMethodCall(Service, u"/org/freedesktop/PackageKit"_s, Service, u"CreateTransaction"_s));
        if (!transaction.isValid()) {
            fail(transaction.error().message());
            return false;
        }
        m_transaction = transaction.value();
        const QString path = m_transaction.path();
        for (const auto &[name, slot]: transactionSignals()) {
            bus.connect(Service, path, TransactionInterface, QString::fromLatin1(name), this, slot);
        }

        // interactive lets polkit ask for a password instead of refusing
        QDBusMessage hints = QDBusMessage::createMethodCall(Service, path, TransactionInterface, u"SetHints"_s);
        hints << QStringList{u"interactive=true"_s, u"locale=%1"_s.arg(QLocale().name())};
        if (const QDBusMessage reply = bus.call(hints); reply.type() == QDBusMessage::ErrorMessage) {
            fail(reply.errorMessage());
            return false;
        }

        QDBusMessage call = QDBusMessage::createMethodCall(Service, path, TransactionInterface, method);
        call.setArguments(arguments);
        const auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call, CallTimeout), this);
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

    void PackageKitInstaller::onRepoSignatureRequired(const QString &packageId, const QString &repositoryName,
                                                      const QString &keyUrl, const QString &keyUserId,
                                                      const QString &keyId, const QString &keyFingerprint,
                                                      const QString &keyTimestamp, const uint type) {
        Q_UNUSED(packageId)
        Q_UNUSED(keyUrl)
        Q_UNUSED(keyUserId)
        Q_UNUSED(keyId)
        Q_UNUSED(keyFingerprint)
        Q_UNUSED(keyTimestamp)
        Q_UNUSED(type)
        // the transaction then finishes with KEY_REQUIRED, which failureReason() explains with this
        m_unsignedRepository = repositoryName;
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
            fail(failureReason(exit, m_error, m_unsignedRepository));
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
