// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QStringList>

#include <cstdint>
#include <functional>

#include "installer.h"

namespace OfficeInstaller {

    /// a package from PackageKit's Package signal
    struct ResolvedPackage {
        /// PkInfoEnum: 1 is installed, 2 is available
        std::uint32_t info = 0;
        /// name;version;arch;repo
        QString id;
    };

    /**
     * the package IDs to install for @p names, from what Resolve found. Names
     * that are already installed are left out. Names that weren't found at all
     * are added to @p missing.
     */
    QStringList packageIdsToInstall(const QList<ResolvedPackage> &resolved, const QStringList &names,
                                    QStringList *missing);

    /**
     * why a transaction that finished with PkExitEnum @p exit failed, for exits other than
     * success and cancel. @p error is the details of the transaction's last ErrorCode and
     * @p unsignedRepository the repository named by RepoSignatureRequired. Both may be empty,
     * and so may the result.
     */
    QString failureReason(uint exit, const QString &error, const QString &unsignedRepository);

    /// how a transaction that didn't succeed is reported
    struct TransactionFailure {
        /// set only when PackageKit says the transaction was canceled
        bool canceled = false;
        /// why it failed, empty when canceled. May be empty otherwise too
        QString error;
    };

    /**
     * how to report a transaction that finished with PkExitEnum @p exit, other than
     * success. @p errorCode and @p error are the PkErrorEnum and details of its last
     * ErrorCode (0 and empty if there was none), @p unsignedRepository as for
     * failureReason(). Asking to cancel doesn't count: PackageKit can refuse, and a
     * transaction that then fails for another reason reports that reason. Neither does
     * being canceled for a higher priority transaction.
     */
    TransactionFailure transactionFailure(uint exit, uint errorCode, const QString &error,
                                          const QString &unsignedRepository);

    /// installs packages through PackageKit's D-Bus API, with the interactive hint set
    class PackageKitInstaller : public Installer {
        Q_OBJECT

    public:
        explicit PackageKitInstaller(const QStringList &packages, QObject *parent = nullptr);

        void start() override;
        void cancel() override;

    private Q_SLOTS:
        // PackageKit's transaction signals, connected by name
        void onPackage(uint info, const QString &packageId, const QString &summary);
        void onErrorCode(uint code, const QString &details);
        void onRepoSignatureRequired(const QString &packageId, const QString &repositoryName, const QString &keyUrl,
                                     const QString &keyUserId, const QString &keyId, const QString &keyFingerprint,
                                     const QString &keyTimestamp, uint type);
        void onFinished(uint exit, uint runtime);

    private:
        enum class Step : std::uint8_t { Idle, Resolving, Installing };

        /// creates a transaction, sets its hints and calls @p method on it, all without blocking
        void startTransaction(const QString &method, const QVariantList &arguments);
        /**
         * calls @p handle with the reply to @p call, unless the install has stopped or started
         * another transaction since
         */
        void watch(const QDBusPendingCall &call, const std::function<void(const QDBusMessage &reply)> &handle);
        void disconnectTransaction();
        void finish(bool success, bool canceled, const QString &error);
        void fail(const QString &error) { finish(false, false, error); }

        Step m_step = Step::Idle;
        /**
         * bumped by each step and when the install stops, so replies to calls that were
         * pending then are dropped instead of acting on a stopped or newer install
         */
        quint64 m_call = 0;
        QDBusObjectPath m_transaction;
        /// the method was called on m_transaction, so its Finished signal will follow
        bool m_methodCalled = false;
        QStringList m_packages;
        QList<ResolvedPackage> m_resolved;
        /// the current transaction's last ErrorCode
        uint m_errorCode = 0;
        QString m_error;
        /// the repository whose signing key the current transaction is missing
        QString m_unsignedRepository;
        bool m_canceled = false;
    };

} // namespace OfficeInstaller
