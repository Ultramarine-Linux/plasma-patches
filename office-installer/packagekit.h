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

    struct ResolvedPackage {
        std::uint32_t pkInfo = 0;
        QString packageId;
    };

    QStringList packageIdsToInstall(const QList<ResolvedPackage> &resolved, const QStringList &names,
                                    QStringList *missing);

    QString failureReason(uint exit, const QString &error, const QString &unsignedRepository);

    struct TransactionFailure {
        bool canceled = false;
        QString error;
    };

    TransactionFailure transactionFailure(uint exit, uint errorCode, const QString &error,
                                          const QString &unsignedRepository);

    class PackageKitInstaller : public Installer {
        Q_OBJECT

    public:
        explicit PackageKitInstaller(const QStringList &packages, QObject *parent = nullptr);

        void start() override;
        void cancel() override;

    private Q_SLOTS:
        void onPackage(uint info, const QString &packageId, const QString &summary);
        void onErrorCode(uint code, const QString &details);
        void onRepoSignatureRequired(const QString &packageId, const QString &repositoryName, const QString &keyUrl,
                                     const QString &keyUserId, const QString &keyId, const QString &keyFingerprint,
                                     const QString &keyTimestamp, uint type);
        void onFinished(uint exit, uint runtime);

    private:
        enum class Step : std::uint8_t { Idle, Resolving, Installing };

        void startTransaction(const QString &method, const QVariantList &arguments);
        void handleReplyIfCurrent(const QDBusPendingCall &call,
                                  const std::function<void(const QDBusMessage &reply)> &handle);
        void disconnectTransaction();
        void finish(bool success, bool canceled, const QString &error);
        void fail(const QString &error) { finish(false, false, error); }

        Step m_step = Step::Idle;
        quint64 m_callGeneration = 0;
        QDBusObjectPath m_transaction;
        bool m_transactionMethodCalled = false;
        QStringList m_packages;
        QList<ResolvedPackage> m_resolved;
        uint m_lastErrorCode = 0;
        QString m_error;
        QString m_unsignedRepository;
        bool m_canceled = false;
    };

} // namespace OfficeInstaller
