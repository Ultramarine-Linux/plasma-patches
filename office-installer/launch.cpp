// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "launch.h"

#include <KApplicationTrader>
#include <KIO/ApplicationLauncherJob>

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
    } // namespace

    bool openWithInstalledApps(const QList<QUrl> &urls, const QObject *context, const std::function<void()> &done) {
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

        auto pending = std::make_shared<qsizetype>(services.size());
        for (auto it = services.cbegin(); it != services.cend(); ++it) {
            auto *job = new KIO::ApplicationLauncherJob(it.value());
            job->setUrls(urlsByService.value(it.key()));
            QObject::connect(job, &KJob::result, context, [pending, done] {
                if (--*pending == 0) {
                    done();
                }
            });
            job->start();
        }
        return true;
    }

} // namespace OfficeInstaller
