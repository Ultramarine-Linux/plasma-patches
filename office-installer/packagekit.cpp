// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "packagekit.h"

#include <KLocalizedString>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QLocale>

#include <algorithm>
#include <array>

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
        // PkErrorEnum
        constexpr uint ErrorTransactionCancelled = 17;
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

    TransactionFailure transactionFailure(const uint exit, const uint errorCode, const QString &error,
                                          const QString &unsignedRepository) {
        // a higher priority transaction canceling this one isn't the user's cancel
        if (exit != ExitCancelledPriority && (exit == ExitCancelled || errorCode == ErrorTransactionCancelled)) {
            return {.canceled = true, .error = {}};
        }
        return {.canceled = false, .error = failureReason(exit, error, unsignedRepository)};
    }

    PackageKitInstaller::PackageKitInstaller(const QStringList &packages, QObject *parent) :
        Installer(parent), m_packages(packages) {}

    void PackageKitInstaller::start() {
        if (m_step != Step::Idle) {
            return;
        }
        m_resolved.clear();
        m_canceled = false;
        m_step = Step::Resolving;
        ++m_call;
        // asking for the version also starts the daemon if it isn't running
        QDBusMessage version = QDBusMessage::createMethodCall(Service, u"/org/freedesktop/PackageKit"_s,
                                                              u"org.freedesktop.DBus.Properties"_s, u"Get"_s);
        version << Service << u"VersionMajor"_s;
        watch(QDBusConnection::systemBus().asyncCall(version), [this](const QDBusMessage &reply) {
            const uint major = reply.arguments().value(0).value<QDBusVariant>().variant().toUInt();
            if (major != SupportedVersionMajor) {
                fail(i18nc("@info %1 is a version number", "PackageKit %1 isn't supported. Only PackageKit 1 is.",
                           major));
                return;
            }
            startTransaction(u"Resolve"_s,
                             {QVariant::fromValue(FilterArch | FilterNotSource | FilterNewest), QVariant(m_packages)});
        });
    }

    void PackageKitInstaller::cancel() {
        if (m_step == Step::Idle) {
            return;
        }
        m_canceled = true;
        if (!m_methodCalled) {
            // nothing is running in PackageKit yet, so stop here. A transaction that was already
            // created is never started, and the daemon drops it on its own
            finish(false, true, {});
            return;
        }
        QDBusConnection::systemBus().asyncCall(
            QDBusMessage::createMethodCall(Service, m_transaction.path(), TransactionInterface, u"Cancel"_s));
    }

    void PackageKitInstaller::watch(const QDBusPendingCall &call,
                                    const std::function<void(const QDBusMessage &reply)> &handle) {
        // parented to the installer, so a reply that comes after it's gone is never handled
        auto *watcher = new QDBusPendingCallWatcher(call, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, handle, generation = m_call](QDBusPendingCallWatcher *watcher) {
                    watcher->deleteLater();
                    if (generation != m_call || m_step == Step::Idle) {
                        return;
                    }
                    if (const QDBusMessage reply = watcher->reply(); reply.type() == QDBusMessage::ErrorMessage) {
                        fail(reply.errorMessage());
                    } else {
                        handle(reply);
                    }
                });
    }

    void PackageKitInstaller::disconnectTransaction() {
        if (!m_transaction.path().isEmpty()) {
            QDBusConnection bus = QDBusConnection::systemBus();
            for (const auto &[name, slot]: transactionSignals()) {
                bus.disconnect(Service, m_transaction.path(), TransactionInterface, QString::fromLatin1(name), this,
                               slot);
            }
        }
        m_transaction = {};
        m_methodCalled = false;
    }

    void PackageKitInstaller::startTransaction(const QString &method, const QVariantList &arguments) {
        disconnectTransaction();
        // the last transaction's replies don't matter anymore
        ++m_call;
        // what went wrong is told per transaction, so don't carry it over from the last one
        m_errorCode = 0;
        m_error.clear();
        m_unsignedRepository.clear();

        QDBusConnection bus = QDBusConnection::systemBus();
        watch(bus.asyncCall(QDBusMessage::createMethodCall(Service, u"/org/freedesktop/PackageKit"_s, Service,
                                                           u"CreateTransaction"_s)),
              [this, method, arguments](const QDBusMessage &reply) {
                  QDBusConnection bus = QDBusConnection::systemBus();
                  m_transaction = reply.arguments().value(0).value<QDBusObjectPath>();
                  const QString path = m_transaction.path();
                  if (path.isEmpty()) {
                      fail({});
                      return;
                  }
                  for (const auto &[name, slot]: transactionSignals()) {
                      bus.connect(Service, path, TransactionInterface, QString::fromLatin1(name), this, slot);
                  }

                  // interactive lets polkit ask for a password instead of refusing
                  QDBusMessage hints =
                      QDBusMessage::createMethodCall(Service, path, TransactionInterface, u"SetHints"_s);
                  hints << QStringList{u"interactive=true"_s, u"locale=%1"_s.arg(QLocale().name())};
                  watch(bus.asyncCall(hints), [this, path, method, arguments](const QDBusMessage &) {
                      QDBusMessage call = QDBusMessage::createMethodCall(Service, path, TransactionInterface, method);
                      call.setArguments(arguments);
                      m_methodCalled = true;
                      // only an error is handled here. The result comes through the transaction's signals
                      watch(QDBusConnection::systemBus().asyncCall(call, CallTimeout), [](const QDBusMessage &) {});
                  });
              });
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
        m_errorCode = code;
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
            // a cancel that PackageKit refused doesn't hide why the transaction failed
            const auto [canceled, error] = transactionFailure(exit, m_errorCode, m_error, m_unsignedRepository);
            finish(false, canceled, error);
            return;
        }
        if (m_step == Step::Installing) {
            finish(true, false, {});
            return;
        }

        // canceled while resolving, which finished anyway: don't start installing
        if (m_canceled) {
            finish(false, true, {});
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
            finish(true, false, {});
            return;
        }
        m_step = Step::Installing;
        startTransaction(u"InstallPackages"_s, {QVariant::fromValue(FlagOnlyTrusted), QVariant(ids)});
    }

    void PackageKitInstaller::finish(const bool success, const bool canceled, const QString &error) {
        if (m_step == Step::Idle) {
            return;
        }
        m_step = Step::Idle;
        ++m_call;
        disconnectTransaction();
        Q_EMIT finished(success, canceled, error);
    }

} // namespace OfficeInstaller
