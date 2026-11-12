// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "plasmoid.h"

#include <KNSCore/EngineBase>
#include <KNSCore/Entry>
#include <KNSCore/ErrorCode>
#include <KNSCore/Question>
#include <KNSCore/QuestionManager>
#include <KNSCore/ResultsStream>
#include <KNSCore/SearchRequest>
#include <KNSCore/Transaction>

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QStandardPaths>
#include <QTimer>
#include <QXmlStreamReader>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <utility>

#include <unistd.h>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace UltramarineUpdates {

    namespace {
        const auto knsrc = u"plasmoids.knsrc"_s;

        constexpr auto listTimeout = 2min;
        constexpr auto installTimeoutPerWidget = 10min;

        void prepareKNewStuff() {
            static bool prepared = false;
            if (std::exchange(prepared, true)) {
                return;
            }
            QCoreApplication::setQuitLockEnabled(false);
            QObject::connect(
                KNSCore::QuestionManager::instance(), &KNSCore::QuestionManager::askQuestion,
                [](KNSCore::Question *question) { question->setResponse(KNSCore::Question::CancelResponse); });
        }

        void quitOnError(const KNSCore::EngineBase &engine, QEventLoop &loop, QString &failure) {
            QObject::connect(&engine, &KNSCore::EngineBase::signalErrorCode, &loop,
                             [&loop, &failure](const KNSCore::ErrorCode::ErrorCode, const QString &message) {
                                 failure = message;
                                 loop.quit();
                             });
        }

        bool runLoop(QEventLoop &loop, const std::chrono::milliseconds timeout) {
            bool timedOut = false;
            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, &loop, [&loop, &timedOut] {
                timedOut = true;
                loop.quit();
            });
            timer.start(timeout);
            loop.exec();
            return !timedOut;
        }

        bool runLoopWithoutError(QEventLoop &loop, QString &failure, const QString &timeoutMessage, QString *error) {
            if (!runLoop(loop, listTimeout)) {
                *error = timeoutMessage;
                return false;
            }
            if (!failure.isEmpty()) {
                *error = std::move(failure);
                return false;
            }
            return true;
        }

        bool initEngine(KNSCore::EngineBase &engine, QString *error) {
            prepareKNewStuff();
            QEventLoop loop;
            QString failure;
            QObject::connect(&engine, &KNSCore::EngineBase::signalProvidersLoaded, &loop, &QEventLoop::quit);
            quitOnError(engine, loop, failure);
            if (!engine.init(knsrc)) {
                *error =
                    failure.isEmpty() ? i18nc("@info:status %1 is a file name", "Could not load %1", knsrc) : failure;
                return false;
            }
            return runLoopWithoutError(loop, failure, i18nc("@info:status", "Timed out loading the KDE Store"), error);
        }

        std::optional<KNSCore::Entry::List> updateableEntries(KNSCore::EngineBase &engine, QString *error) {
            QEventLoop loop;
            QString failure;
            QHash<QString, KNSCore::Entry> found;
            KNSCore::ResultsStream *stream =
                engine.search(KNSCore::SearchRequest(KNSCore::SortMode::Newest, KNSCore::Filter::Updates));
            QObject::connect(stream, &KNSCore::ResultsStream::entriesFound, &loop,
                             [&found](const KNSCore::Entry::List &entries) {
                                 for (const KNSCore::Entry &entry: entries) {
                                     if (entry.status() == KNSCore::Entry::Updateable) {
                                         found.insert(entry.uniqueId(), entry);
                                     }
                                 }
                             });
            QObject::connect(stream, &KNSCore::ResultsStream::finished, &loop, &QEventLoop::quit);
            quitOnError(engine, loop, failure);
            stream->fetch();
            if (!runLoopWithoutError(loop, failure,
                                     i18nc("@info:status", "Timed out checking the KDE Store for updates"), error)) {
                return std::nullopt;
            }
            return found.values();
        }

        PlasmoidEntry plasmoidEntry(const KNSCore::Entry &entry) {
            return {
                .uniqueId = entry.uniqueId(),
                .name = entry.name(),
                .version = entry.version(),
                .updateVersion = entry.updateVersion(),
                .installedFiles = entry.installedFiles(),
            };
        }

        void say(const QString &message) {
            std::puts(qPrintable(message));
            std::fflush(stdout);
        }
    } // namespace

    QString plasmoidRegistryPath() {
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
            "/knewstuff3/plasmoids.knsregistry"_L1;
    }

    bool registryHasInstalledEntries(const QByteArray &registry) {
        QXmlStreamReader xml(registry);
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == "status"_L1) {
                if (const QString status = xml.readElementText();
                    status == "installed"_L1 || status == "updateable"_L1) {
                    return true;
                }
            }
        }
        return false;
    }

    PlasmoidMetadata parsePlasmoidMetadata(const QByteArray &json) {
        const QJsonObject plugin = QJsonDocument::fromJson(json).object().value("KPlugin"_L1).toObject();
        return {
            .id = plugin.value("Id"_L1).toString(),
            .icon = plugin.value("Icon"_L1).toString(),
        };
    }

    PackageUpdate plasmoidPackageUpdate(const PlasmoidEntry &entry, const FileReader &read) {
        PackageUpdate update;
        update.id = u"plasmoid:"_s + entry.uniqueId;
        update.name = entry.name;
        update.source = u"KDE Store"_s;
        update.backend = u"plasmoid"_s;
        update.kind = QString::fromLatin1(Kind::Addon);
        update.oldVersion = entry.version;
        update.newVersion = entry.updateVersion;
        update.icon = u"plasma"_s;
        for (const QString &file: entry.installedFiles) {
            QString directory = file;
            if (constexpr auto directoryWildcard = "/*"_L1; directory.endsWith(directoryWildcard)) {
                directory.chop(directoryWildcard.size());
            }
            if (const std::optional<QByteArray> json = read(QDir::cleanPath(directory) + "/metadata.json"_L1)) {
                if (const PlasmoidMetadata metadata = parsePlasmoidMetadata(*json); !metadata.icon.isEmpty()) {
                    update.icon = metadata.icon;
                }
                break;
            }
        }
        return update;
    }

    bool canManagePlasmoids() {
        if (geteuid() == 0) {
            return false;
        }
        const std::optional<QByteArray> registry = readFile(plasmoidRegistryPath());
        return registry && registryHasInstalledEntries(*registry);
    }

    QList<PackageUpdate> plasmoidUpdates(QString *error) {
        KNSCore::EngineBase engine;
        if (!initEngine(engine, error)) {
            return {};
        }
        const std::optional<KNSCore::Entry::List> entries = updateableEntries(engine, error);
        if (!entries) {
            return {};
        }
        QList<PackageUpdate> updates;
        updates.reserve(entries->size());
        std::ranges::transform(*entries, std::back_inserter(updates), [](const KNSCore::Entry &entry) {
            return plasmoidPackageUpdate(plasmoidEntry(entry), readFile);
        });
        return updates;
    }

    bool upgradePlasmoids(QString *error) {
        KNSCore::EngineBase engine;
        if (!initEngine(engine, error)) {
            return false;
        }
        const std::optional<KNSCore::Entry::List> entries = updateableEntries(engine, error);
        if (!entries) {
            return false;
        }
        QStringList failed;
        for (qsizetype index = 0; index < entries->size(); ++index) {
            const KNSCore::Entry &entry = entries->at(index);
            say(i18nc("@info:progress %1 is a widget name, %2 a version", "Updating widget %1 to %2", entry.name(),
                      entry.updateVersion()));
            QEventLoop loop;
            QString failure;
            const KNSCore::Transaction *transaction = KNSCore::Transaction::installLatest(&engine, entry);
            QObject::connect(transaction, &KNSCore::Transaction::signalErrorCode, &loop,
                             [&failure](const KNSCore::ErrorCode::ErrorCode, const QString &message) {
                                 if (failure.isEmpty()) {
                                     failure = message;
                                 }
                             });
            QObject::connect(transaction, &KNSCore::Transaction::finished, &loop, &QEventLoop::quit);
            const bool timedOut = !runLoop(loop, installTimeoutPerWidget);
            if (timedOut) {
                failure = i18nc("@info:status", "timed out");
            }
            if (!failure.isEmpty()) {
                std::fprintf(stderr, "%s\n",
                             qPrintable(i18nc("@info:status %1 is a widget name, %2 an error message",
                                              "Warning: could not update %1: %2", entry.name(), failure)));
                failed.append(entry.name());
            }
            if (timedOut) {
                std::ranges::transform(entries->sliced(index + 1), std::back_inserter(failed), &KNSCore::Entry::name);
                break;
            }
        }
        if (!failed.isEmpty()) {
            *error = i18nc("@info:status %1 is a list of widget names", "Could not update %1",
                           QLocale().createSeparatedList(failed));
            return false;
        }
        return true;
    }

} // namespace UltramarineUpdates
