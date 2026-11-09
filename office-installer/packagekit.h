// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDBusObjectPath>
#include <QStringList>

#include <cstdint>

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

        bool startTransaction(const QString &method, const QVariantList &arguments);
        void fail(const QString &error);

        Step m_step = Step::Idle;
        QDBusObjectPath m_transaction;
        QStringList m_packages;
        QList<ResolvedPackage> m_resolved;
        /// the current transaction's last ErrorCode
        QString m_error;
        /// the repository whose signing key the current transaction is missing
        QString m_unsignedRepository;
        bool m_canceled = false;
    };

} // namespace OfficeInstaller
