// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "nix.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

#include <unistd.h>

#include "process.h"

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    namespace {
        /// Where the Determinate installer puts nix. It isn't always on PATH: GUI
        /// sessions may not source /etc/profile.d/nix.sh, and pkexec resets the environment.
        const auto nixDefaultProfileBin = u"/nix/var/nix/profiles/default/bin/nix"_s;
        /// Where the Determinate installer puts determinate-nixd.
        const auto determinateNixdPath = u"/usr/local/bin/determinate-nixd"_s;

        /**
         * Evaluates the packages `nix profile upgrade` would install, without
         * building or changing anything. Elements are passed through the
         * environment so nothing has to be escaped into Nix syntax.
         */
        const auto nixUpdatesExpr = uR"nix(
let
  elements = builtins.fromJSON (builtins.getEnv "ULTRAMARINE_UPDATES_NIX_ELEMENTS");
  # Missing attributes can't be caught with tryEval, so walk the path safely.
  getPath = root: path: builtins.foldl' (a: k: if a != null && builtins.isAttrs a && a ? ${k} then a.${k} else null) root path;
  resolve = e:
    let
      flake = builtins.tryEval (builtins.getFlake e.flake);
      pkg = if flake.success then getPath flake.value e.attrPath else null;
      outputs = if e.outputs != null then e.outputs else (pkg.meta.outputsToInstall or [ "out" ]);
      outPaths = builtins.tryEval (map (o: pkg.${o}.outPath) outputs);
    in
    if pkg == null || !outPaths.success then { error = true; }
    else { error = false; name = pkg.name or ""; version = pkg.version or ""; outPaths = outPaths.value; };
in
map resolve elements
)nix"_s;

        QStringList toStringList(const QJsonValue &value) {
            QStringList list;
            for (const QJsonArray array = value.toArray(); const auto &item: array) {
                list.append(item.toString());
            }
            return list;
        }

        NixProfileElement parseElement(const QJsonObject &object) {
            NixProfileElement element;
            if (const QJsonValue active = object.value("active"_L1); active.isBool()) {
                element.active = active.toBool();
            }
            element.attrPath = object.value("attrPath"_L1).toString();
            element.originalUrl = object.value("originalUrl"_L1).toString();
            element.url = object.value("url"_L1).toString();
            if (const QJsonValue outputs = object.value("outputs"_L1); outputs.isArray()) {
                element.outputs = toStringList(outputs);
            }
            element.storePaths = toStringList(object.value("storePaths"_L1));
            return element;
        }

        bool sameStorePaths(const QStringList &a, const QStringList &b) {
            return a.size() == b.size() && QSet(a.cbegin(), a.cend()) == QSet(b.cbegin(), b.cend());
        }
    } // namespace

    std::optional<QList<NixProfileElement>> parseNixProfileList(const QByteArray &output, QString *error) {
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error) {
                *error = u"unexpected nix profile output: %1"_s.arg(parseError.errorString());
            }
            return std::nullopt;
        }

        QList<NixProfileElement> elements;
        if (const QJsonValue value = document.object().value("elements"_L1); value.isObject()) {
            const QJsonObject byName = value.toObject();
            for (auto it = byName.constBegin(); it != byName.constEnd(); ++it) {
                NixProfileElement element = parseElement(it.value().toObject());
                element.name = it.key();
                elements.append(element);
            }
        } else if (value.isArray()) {
            for (const QJsonArray array = value.toArray(); const auto &item: array) {
                NixProfileElement element = parseElement(item.toObject());
                // Older profiles have no names; nix named them after the attribute.
                element.name = element.attrPath.split(u'.').constLast();
                elements.append(element);
            }
        } else if (!value.isNull() && !value.isUndefined()) {
            if (error) {
                *error = u"unexpected nix profile elements"_s;
            }
            return std::nullopt;
        }

        // Packages installed from a store path have no flake to upgrade from.
        elements.removeIf([](const NixProfileElement &e) {
            return e.originalUrl.isEmpty() || e.attrPath.isEmpty() || (e.active.has_value() && !*e.active);
        });
        std::ranges::sort(elements,
                          [](const NixProfileElement &a, const NixProfileElement &b) { return a.name < b.name; });
        return elements;
    }

    QString storePathName(const QString &path) {
        QString base = path.mid(path.lastIndexOf(u'/') + 1);
        if (const qsizetype dash = base.indexOf(u'-'); dash >= 0) {
            return base.mid(dash + 1);
        }
        return base;
    }

    QString drvVersion(const QString &name) {
        for (qsizetype i = 0; i + 1 < name.size(); ++i) {
            if (name[i] == u'-' && !name[i + 1].isLetter()) {
                return name.mid(i + 1);
            }
        }
        return {};
    }

    std::optional<PackageUpdate> nixUpdateFor(const NixProfileElement &element, const NixEvalResult &result) {
        if (result.error || result.outPaths.isEmpty() || sameStorePaths(element.storePaths, result.outPaths)) {
            return std::nullopt;
        }
        PackageUpdate update;
        update.id = u"nix:"_s + element.name;
        update.name = element.name;
        update.source = u"Nix (%1)"_s.arg(element.originalUrl);
        update.backend = u"nix"_s;
        update.kind = QString::fromLatin1(Kind::Package);
        update.icon = u"package-x-generic"_s;
        if (!element.storePaths.isEmpty()) {
            update.oldVersion = drvVersion(storePathName(element.storePaths.constFirst()));
        }
        update.newVersion = result.version.isEmpty() ? drvVersion(result.name) : result.version;
        return update;
    }

    AppFiles nixAppFiles(const QStringList &storePaths) {
        AppFiles files;
        const auto glob = [](const QString &dir, const QString &pattern) {
            QStringList paths;
            for (const QStringList names = QDir(dir).entryList({pattern}, QDir::Files, QDir::Name);
                 const QString &name: names) {
                paths.append(dir + u'/' + name);
            }
            return paths;
        };
        for (const QString &storePath: storePaths) {
            const QString share = storePath + "/share"_L1;
            files.metainfo += glob(share + "/metainfo"_L1, u"*.xml"_s);
            files.metainfo += glob(share + "/appdata"_L1, u"*.xml"_s);
            const QStringList desktop = glob(share + "/applications"_L1, u"*.desktop"_s);
            files.desktop += desktop;
            if (files.applicationsDir.isEmpty() && !desktop.isEmpty()) {
                files.applicationsDir = share + "/applications"_L1;
            }
        }
        return files;
    }

    std::pair<QString, QString> determinateNixVersions(const QString &output) {
        QString installed;
        QString latest;
        for (const QStringList lines = output.split(u'\n'); const QString &line: lines) {
            const qsizetype colon = line.indexOf(u':');
            if (colon < 0) {
                continue;
            }
            const QString key = line.left(colon).trimmed();
            const QString value = line.mid(colon + 1).trimmed();
            if (key == "Determinate Nixd client version"_L1) {
                installed = value;
            } else if (key == "Latest version"_L1) {
                latest = value;
            }
        }
        return {installed, latest};
    }

    QString nixBinary() { return findExecutable(u"nix"_s, {nixDefaultProfileBin}); }

    QString determinateNixdBinary() { return findExecutable(u"determinate-nixd"_s, {determinateNixdPath}); }

    bool canManageNixProfile() { return !nixBinary().isEmpty() && geteuid() != 0; }

    QList<PackageUpdate> nixPackageUpdates(QString *error) {
        const QString nix = nixBinary();
        const CommandResult list = capture(nix, {u"profile"_s, u"list"_s, u"--json"_s});
        if (!list.succeeded()) {
            *error = u"nix profile list failed: %1"_s.arg(QString::fromUtf8(list.err).trimmed());
            return {};
        }
        const std::optional<QList<NixProfileElement>> elements = parseNixProfileList(list.out, error);
        if (!elements || elements->isEmpty()) {
            return {};
        }

        QJsonArray inputs;
        for (const NixProfileElement &element: *elements) {
            inputs.append(QJsonObject{
                {u"flake"_s, element.originalUrl},
                {u"attrPath"_s, QJsonArray::fromStringList(element.attrPath.split(u'.'))},
                {u"outputs"_s,
                 element.outputs ? QJsonValue(QJsonArray::fromStringList(*element.outputs)) : QJsonValue()},
            });
        }

        // --impure is needed to resolve unlocked flake references; nix's
        // tarball-ttl decides when they are fetched again, like dnf's metadata expiry.
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(u"ULTRAMARINE_UPDATES_NIX_ELEMENTS"_s,
                           QString::fromUtf8(QJsonDocument(inputs).toJson(QJsonDocument::Compact)));
        const CommandResult eval =
            capture(nix, {u"eval"_s, u"--json"_s, u"--impure"_s, u"--expr"_s, nixUpdatesExpr}, environment);
        if (!eval.succeeded()) {
            *error = u"evaluating nix profile upgrades failed: %1"_s.arg(lastLine(QString::fromUtf8(eval.err)));
            return {};
        }
        const QJsonDocument results = QJsonDocument::fromJson(eval.out);
        if (!results.isArray() || results.array().size() != elements->size()) {
            *error = u"unexpected nix eval output"_s;
            return {};
        }

        QList<PackageUpdate> updates;
        const QJsonArray resultArray = results.array();
        for (qsizetype i = 0; i < elements->size(); ++i) {
            const NixProfileElement &element = elements->at(i);
            const QJsonObject object = resultArray.at(i).toObject();
            const NixEvalResult result{
                .error = object.value("error"_L1).toBool(),
                .name = object.value("name"_L1).toString(),
                .version = object.value("version"_L1).toString(),
                .outPaths = toStringList(object.value("outPaths"_L1)),
            };
            std::optional<PackageUpdate> update = nixUpdateFor(element, result);
            if (!update) {
                continue;
            }
            const AppFiles files = nixAppFiles(element.storePaths);
            if (const std::optional<AppInfo> app = resolveApp(element.name, &files, readFile)) {
                applyAppInfo(*update, *app);
            }
            updates.append(*update);
        }
        return updates;
    }

    QList<PackageUpdate> determinateNixUpdate(QString *error) {
        const QString nixd = determinateNixdBinary();
        if (nixd.isEmpty()) {
            return {};
        }
        const CommandResult result = capture(nixd, {u"version"_s});
        if (!result.succeeded()) {
            *error = u"determinate-nixd version failed: %1"_s.arg(QString::fromUtf8(result.err).trimmed());
            return {};
        }
        const auto [installed, latest] = determinateNixVersions(QString::fromUtf8(result.out));
        if (installed.isEmpty() || latest.isEmpty() || installed == latest) {
            return {};
        }
        PackageUpdate update;
        update.id = u"nix:determinate-nix"_s;
        update.name = u"Determinate Nix"_s;
        update.source = u"Nix (Determinate Systems)"_s;
        update.backend = u"nix"_s;
        update.kind = QString::fromLatin1(Kind::Package);
        update.icon = u"package-x-generic"_s;
        update.oldVersion = installed;
        update.newVersion = latest;
        return {update};
    }

    int upgradeNixProfile() {
        // --impure like the listing, so unlocked flake references resolve the same
        // way (topgrade does the same since Nix 2.21). Flake nixConfig (such as
        // extra substituters) is not accepted automatically, same as nix itself.
        return runForwarded(nixBinary(), {u"profile"_s, u"upgrade"_s, u"--all"_s, u"--impure"_s});
    }

    int upgradeDeterminateNix() {
        const QString nixd = determinateNixdBinary();
        return nixd.isEmpty() ? 0 : runForwarded(nixd, {u"upgrade"_s});
    }

} // namespace UltramarineUpdates
