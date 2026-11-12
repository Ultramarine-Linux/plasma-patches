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
        KService::Ptr handlerListingExactMimeType(const QMimeType &mimeType) {
            const QMimeDatabase db;
            const KService::List services =
                KApplicationTrader::queryByMimeType(mimeType.name(), [&](const KService::Ptr &service) {
                    if (service->storageId() == QLatin1StringView(OwnDesktopFileId)) {
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
            QList<QUrl> failed;
        };
    } // namespace

    bool openWithInstalledApps(const QList<QUrl> &urls, const QObject *context, const LaunchesFinished &done) {
        if (urls.isEmpty()) {
            return false;
        }
        QHash<QString, KService::Ptr> services;
        QHash<QString, QList<QUrl>> urlsByService;
        for (const QUrl &url: urls) {
            const KService::Ptr service = handlerListingExactMimeType(mimeTypeOf(url));
            if (!service) {
                return false;
            }
            services.insert(service->storageId(), service);
            urlsByService[service->storageId()].append(url);
        }

        auto pending = std::make_shared<PendingLaunches>();
        pending->remaining = services.size();
        for (auto it = services.cbegin(); it != services.cend(); ++it) {
            const QList<QUrl> serviceUrls = urlsByService.value(it.key());
            auto *job = new KIO::ApplicationLauncherJob(it.value());
            job->setUrls(serviceUrls);
            // the delegate's message box would only show after the installer quit, so errors go through done()
            job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingDisabled, nullptr));
            QObject::connect(job, &KJob::result, context, [pending, done, serviceUrls](const KJob *job1) {
                if (job1->error() != 0 && job1->error() != KJob::KilledJobError) {
                    pending->errors.append(job1->errorString());
                    pending->failed.append(serviceUrls);
                }
                if (--pending->remaining == 0) {
                    done(pending->errors.join(u'\n'), pending->failed);
                }
            });
            job->start();
        }
        return true;
    }

    QMimeType mimeTypeOf(const QUrl &url) {
        const QMimeDatabase db;
        return url.isLocalFile() ? db.mimeTypeForFile(url.toLocalFile()) : db.mimeTypeForUrl(url);
    }

    KService::Ptr findInstalledSuite() {
        for (const auto type: {"application/vnd.openxmlformats-officedocument.wordprocessingml.document"_L1,
                               "application/vnd.oasis.opendocument.text"_L1,
                               "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"_L1,
                               "application/vnd.oasis.opendocument.spreadsheet"_L1,
                               "application/vnd.openxmlformats-officedocument.presentationml.presentation"_L1,
                               "application/vnd.oasis.opendocument.presentation"_L1}) {
            const QMimeDatabase db;
            if (KService::Ptr service = handlerListingExactMimeType(db.mimeTypeForName(type))) {
                return service;
            }
        }
        return {};
    }

} // namespace OfficeInstaller
