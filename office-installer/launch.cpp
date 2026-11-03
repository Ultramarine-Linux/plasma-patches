// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "launch.h"

#include <KApplicationTrader>
#include <KIO/ApplicationLauncherJob>
#include <KIO/JobUiDelegateFactory>

#include <QHash>
#include <QMimeDatabase>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace OfficeInstaller {

    namespace {
        KService::Ptr installedHandler(const QMimeType &mimeType) {
            const QMimeDatabase db;
            const KService::List services =
                KApplicationTrader::queryByMimeType(mimeType.name(), [&](const KService::Ptr &service) {
                    if (service->storageId() == QLatin1StringView(DesktopFileId)) {
                        return false;
                    }
                    const QStringList types = service->mimeTypes();
                    return std::ranges::any_of(
                        types, [&](const QString &type) { return db.mimeTypeForName(type).name() == mimeType.name(); });
                });
            return services.value(0);
        }

        struct PendingLaunches {
            qsizetype remaining = 0;
            QStringList errors;
        };
    } // namespace

    bool openWithInstalledApps(const QList<QUrl> &urls, const QObject *context,
                               const std::function<void(const QString &error)> &done) {
        if (urls.isEmpty()) {
            return false;
        }
        QHash<QString, KService::Ptr> services;
        QHash<QString, QList<QUrl>> urlsByService;
        for (const QUrl &url: urls) {
            const QMimeDatabase db;
            const QMimeType mimeType =
                url.isLocalFile() ? db.mimeTypeForFile(url.toLocalFile()) : db.mimeTypeForUrl(url);
            const KService::Ptr service = installedHandler(mimeType);
            if (!service) {
                return false;
            }
            services.insert(service->storageId(), service);
            urlsByService[service->storageId()].append(url);
        }

        auto pending = std::make_shared<PendingLaunches>();
        pending->remaining = services.size();
        for (auto it = services.cbegin(); it != services.cend(); ++it) {
            auto *job = new KIO::ApplicationLauncherJob(it.value());
            job->setUrls(urlsByService.value(it.key()));
            // asks before running untrusted desktop files. Errors are reported through done(), not
            // by the delegate: its message box would be queued after the installer has quit
            job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingDisabled, nullptr));
            QObject::connect(job, &KJob::result, context, [pending, done](KJob *job) {
                if (job->error() != 0 && job->error() != KJob::KilledJobError) {
                    pending->errors.append(job->errorString());
                }
                if (--pending->remaining == 0) {
                    done(pending->errors.join(u'\n'));
                }
            });
            job->start();
        }
        return true;
    }

    KService::Ptr findInstalledSuite() {
        const QMimeDatabase db;
        for (const auto type: {"application/vnd.openxmlformats-officedocument.wordprocessingml.document"_L1,
                               "application/vnd.oasis.opendocument.text"_L1,
                               "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"_L1,
                               "application/vnd.oasis.opendocument.spreadsheet"_L1,
                               "application/vnd.openxmlformats-officedocument.presentationml.presentation"_L1,
                               "application/vnd.oasis.opendocument.presentation"_L1}) {
            if (KService::Ptr service = installedHandler(db.mimeTypeForName(type))) {
                return service;
            }
        }
        return {};
    }

} // namespace OfficeInstaller
