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

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimer>
#include <QXmlStreamReader>

#include <chrono>
#include <cstdio>
#include <utility>

#include <unistd.h>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace UltramarineUpdates {

    namespace {
        /// The configuration of Plasma's "Get New Widgets" dialog. Its registry is named after it.
        constexpr auto knsrc = "plasmoids.knsrc";

        /// How long to wait for the KDE Store before giving up.
        constexpr auto listTimeout = 2min;
        /// Per widget: downloading and installing a widget is quick.
        constexpr auto installTimeout = 10min;

        /**
         * Sets up KNewStuff for a command line program.
         *
         * KNewStuff's jobs hold QEventLoopLockers, and QCoreApplication quits all
         * event loops when the last one is released, ending our waits early.
         *
         * KNewStuff's questions are declined. We run unattended (or behind the
         * KCM), so nothing can answer them, and Question::ask() would wait forever.
         * The ones that come up while updating ("which file is the update?") fail
         * that widget's update instead of guessing.
         */
        void prepareKNewStuff() {
            static bool prepared = false;
            if (prepared) {
                return;
            }
            prepared = true;
            QCoreApplication::setQuitLockEnabled(false);
            QObject::connect(
                KNSCore::QuestionManager::instance(), &KNSCore::QuestionManager::askQuestion,
                [](KNSCore::Question *question) { question->setResponse(KNSCore::Question::CancelResponse); });
        }

        /// Runs @p loop until it quits or @p timeout passes. Returns false on timeout.
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

        /// Loads the KDE Store providers. KNewStuff only searches providers it has loaded.
        bool initEngine(KNSCore::EngineBase &engine, QString *error) {
            prepareKNewStuff();
            QEventLoop loop;
            QString failure;
            QObject::connect(&engine, &KNSCore::EngineBase::signalProvidersLoaded, &loop, &QEventLoop::quit);
            QObject::connect(&engine, &KNSCore::EngineBase::signalErrorCode, &loop,
                             [&loop, &failure](const KNSCore::ErrorCode::ErrorCode, const QString &message) {
                                 failure = message;
                                 loop.quit();
                             });
            if (!engine.init(QString::fromLatin1(knsrc))) {
                *error = failure.isEmpty() ? u"could not load %1"_s.arg(QString::fromLatin1(knsrc)) : failure;
                return false;
            }
            if (!runLoop(loop, listTimeout)) {
                *error = u"timed out loading the KDE Store"_s;
                return false;
            }
            if (!failure.isEmpty()) {
                *error = std::move(failure);
                return false;
            }
            return true;
        }

        /// Searches for updateable widgets. KNewStuff compares the registry with the store.
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
            QObject::connect(&engine, &KNSCore::EngineBase::signalErrorCode, &loop,
                             [&loop, &failure](const KNSCore::ErrorCode::ErrorCode, const QString &message) {
                                 failure = message;
                                 loop.quit();
                             });
            stream->fetch();
            if (!runLoop(loop, listTimeout)) {
                *error = u"timed out checking the KDE Store for updates"_s;
                return std::nullopt;
            }
            if (!failure.isEmpty()) {
                *error = std::move(failure);
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
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/knewstuff3/"_L1 +
            QString::fromLatin1(knsrc).chopped(6) + ".knsregistry"_L1;
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
        // The store has no icon names, only screenshots; the widget's own icon is
        // in its package. "plasma" stands in for widgets without one.
        update.icon = u"plasma"_s;
        for (const QString &file: entry.installedFiles) {
            // KNewStuff may record directories as "path/*".
            QString directory = file;
            if (directory.endsWith("/*"_L1)) {
                directory.chop(2);
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
        for (const KNSCore::Entry &entry: *entries) {
            updates.append(plasmoidPackageUpdate(plasmoidEntry(entry), readFile));
        }
        return updates;
    }

    bool upgradePlasmoids(QString *error) {
        // Destroying the engine writes the registry, so the updates aren't listed again.
        KNSCore::EngineBase engine;
        if (!initEngine(engine, error)) {
            return false;
        }
        const std::optional<KNSCore::Entry::List> entries = updateableEntries(engine, error);
        if (!entries) {
            return false;
        }
        QStringList failed;
        for (const KNSCore::Entry &entry: *entries) {
            say(u"Updating widget %1 to %2"_s.arg(entry.name(), entry.updateVersion()));
            QEventLoop loop;
            QString failure;
            KNSCore::Transaction *transaction = KNSCore::Transaction::installLatest(&engine, entry);
            QObject::connect(transaction, &KNSCore::Transaction::signalErrorCode, &loop,
                             [&failure](const KNSCore::ErrorCode::ErrorCode, const QString &message) {
                                 if (failure.isEmpty()) {
                                     failure = message;
                                 }
                             });
            QObject::connect(transaction, &KNSCore::Transaction::finished, &loop, &QEventLoop::quit);
            if (!runLoop(loop, installTimeout)) {
                failure = u"timed out"_s;
            }
            if (!failure.isEmpty()) {
                std::fprintf(stderr, "warning: could not update %s: %s\n", qPrintable(entry.name()),
                             qPrintable(failure));
                failed.append(entry.name());
            }
        }
        if (!failed.isEmpty()) {
            *error = u"could not update %1"_s.arg(failed.join(u", "_s));
            return false;
        }
        return true;
    }

} // namespace UltramarineUpdates
