// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "apps.h"

#include <QDir>
#include <QFile>
#include <QXmlStreamReader>

#include <algorithm>
#include <iterator>

#include "process.h"
#include "rpm.h"

using namespace Qt::StringLiterals;

namespace UltramarineUpdates {

    namespace {
        const auto applicationsDir = u"/usr/share/applications"_s;

        bool isMetainfoDir(const QString &dir) {
            return dir == "/usr/share/metainfo"_L1 || dir == "/usr/share/appdata"_L1;
        }

        QString directoryOf(const QString &path) {
            const qsizetype slash = path.lastIndexOf(u'/');
            if (slash < 0) {
                return u"."_s;
            }
            return slash == 0 ? u"/"_s : path.left(slash);
        }

        QString fileNameOf(const QString &path) { return path.mid(path.lastIndexOf(u'/') + 1); }

        bool isNamedAfterPackage(const QString &packageName, const QString &path) {
            QString base = fileNameOf(path);
            for (const auto suffix: {".desktop"_L1, ".metainfo.xml"_L1, ".appdata.xml"_L1, ".xml"_L1}) {
                if (base.endsWith(suffix)) {
                    base.chop(suffix.size());
                    break;
                }
            }
            return base == packageName || base.endsWith(u'.' + packageName);
        }

        QStringList preferPackageMatch(const QString &packageName, QStringList paths) {
            std::ranges::stable_sort(paths, [&packageName](const QString &a, const QString &b) {
                return isNamedAfterPackage(packageName, a) && !isNamedAfterPackage(packageName, b);
            });
            return paths;
        }

        QHash<QString, AppFiles> rpmAppFilesFor(const QStringList &nameArchs) {
            if (nameArchs.isEmpty()) {
                return {};
            }
            const QStringList arguments =
                QStringList{u"-q"_s, u"--qf"_s, u"[%{=NAME}.%{=ARCH}\t%{FILENAMES}\n]"_s} + nameArchs;
            return parseRpmAppFiles(QString::fromUtf8(capture(u"rpm"_s, arguments).out));
        }
    } // namespace

    std::optional<QByteArray> readFile(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return std::nullopt;
        }
        return file.readAll();
    }

    std::optional<Metainfo> parseMetainfo(const QByteArray &content) {
        QXmlStreamReader xml(content);
        if (!xml.readNextStartElement() || xml.name() != "component"_L1) {
            return std::nullopt;
        }
        const QString type = xml.attributes().value("type"_L1).toString();

        Metainfo info;
        bool hasName = false;
        while (xml.readNextStartElement()) {
            const QString element = xml.name().toString();
            const QXmlStreamAttributes attributes = xml.attributes();
            if (element == "id"_L1) {
                info.id = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            } else if (element == "name"_L1) {
                const QString name = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                const bool untranslated =
                    !attributes.hasAttribute(u"http://www.w3.org/XML/1998/namespace"_s, u"lang"_s);
                if (!hasName && untranslated) {
                    info.name = name;
                    hasName = true;
                }
            } else if (element == "icon"_L1) {
                const QString iconType = attributes.value("type"_L1).toString();
                const QString value = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                const bool usableWithoutCatalog =
                    iconType == "stock"_L1 || (iconType == "local"_L1 && QDir::isAbsolutePath(value));
                if (info.icon.isEmpty() && !value.isEmpty() && usableWithoutCatalog) {
                    info.icon = value;
                }
            } else if (element == "launchable"_L1) {
                const bool desktopId = attributes.value("type"_L1) == "desktop-id"_L1;
                if (const QString value = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                    desktopId && !value.isEmpty()) {
                    info.desktopIds.append(value);
                }
            } else {
                xml.skipCurrentElement();
            }
        }
        while (!xml.atEnd()) {
            xml.readNext();
        }
        if (xml.hasError() || (type != "desktop-application"_L1 && type != "desktop"_L1)) {
            return std::nullopt;
        }
        if (info.desktopIds.isEmpty() && !info.id.isEmpty()) {
            info.desktopIds.append(info.id.endsWith(".desktop"_L1) ? info.id : info.id + ".desktop"_L1);
        }
        if (info.id.isEmpty() || info.name.isEmpty()) {
            return std::nullopt;
        }
        return info;
    }

    std::optional<DesktopEntry> parseDesktopEntry(const QString &content) {
        DesktopEntry entry;
        bool visible = true;
        bool isApplication = false;
        const QStringList lines = content.split(u'\n');
        auto line = std::ranges::find_if(lines, [](const QString &l) { return l.trimmed() == "[Desktop Entry]"_L1; });
        if (line != lines.cend()) {
            ++line;
        }
        for (; line != lines.cend(); ++line) {
            const QString trimmed = line->trimmed();
            if (trimmed.startsWith(u'[')) {
                break;
            }
            const qsizetype equals = trimmed.indexOf(u'=');
            if (trimmed.startsWith(u'#') || equals < 0) {
                continue;
            }
            const QString key = trimmed.left(equals).trimmed();
            const QString value = trimmed.mid(equals + 1).trimmed();
            if (key == "Name"_L1) {
                entry.name = value;
            } else if (key == "Icon"_L1) {
                entry.icon = value;
            } else if (key == "Type"_L1) {
                isApplication = value == "Application"_L1;
            } else if ((key == "NoDisplay"_L1 || key == "Hidden"_L1) && value == "true"_L1) {
                visible = false;
            }
        }
        if (!visible || !isApplication || entry.name.isEmpty()) {
            return std::nullopt;
        }
        return entry;
    }

    QHash<QString, AppFiles> parseRpmAppFiles(const QString &output) {
        QHash<QString, AppFiles> files;
        for (const QStringList lines = output.split(u'\n'); const QString &line: lines) {
            const qsizetype tab = line.indexOf(u'\t');
            if (tab < 0) {
                continue;
            }
            const QString nameArch = line.left(tab);
            const QString path = line.mid(tab + 1);
            if (const QString dir = directoryOf(path); dir == applicationsDir && path.endsWith(".desktop"_L1)) {
                files[nameArch].desktop.append(path);
            } else if (isMetainfoDir(dir) && path.endsWith(".xml"_L1)) {
                files[nameArch].metainfo.append(path);
            }
        }
        for (AppFiles &f: files) {
            f.metainfo.sort();
            f.desktop.sort();
        }
        return files;
    }

    std::optional<AppInfo> resolveApp(const QString &packageName, const AppFiles *files, const FileReader &read) {
        if (!files) {
            return std::nullopt;
        }

        const QString appsDir = files->applicationsDir.isEmpty() ? applicationsDir : files->applicationsDir;
        const auto launcher = [&appsDir, &read](const QStringList &desktopIds) -> std::optional<DesktopEntry> {
            for (const QString &id: desktopIds) {
                if (const std::optional<QByteArray> content = read(appsDir + u'/' + id)) {
                    if (std::optional<DesktopEntry> entry = parseDesktopEntry(QString::fromUtf8(*content))) {
                        return entry;
                    }
                }
            }
            return std::nullopt;
        };

        for (const QStringList metainfoFiles = preferPackageMatch(packageName, files->metainfo);
             const QString &path: metainfoFiles) {
            const std::optional<QByteArray> content = read(path);
            if (!content) {
                continue;
            }
            const std::optional<Metainfo> info = parseMetainfo(*content);
            if (!info) {
                continue;
            }
            AppInfo app{.id = info->id, .name = info->name, .icon = info->icon};
            if (app.icon.isEmpty()) {
                if (const std::optional<DesktopEntry> entry = launcher(info->desktopIds)) {
                    app.icon = entry->icon;
                }
            }
            return app;
        }
        const bool hasMetainfoButNoApp = !files->metainfo.isEmpty();
        if (hasMetainfoButNoApp) {
            return std::nullopt;
        }

        for (const QStringList desktopFiles = preferPackageMatch(packageName, files->desktop);
             const QString &path: desktopFiles) {
            if (const std::optional<DesktopEntry> entry = launcher({fileNameOf(path)})) {
                return AppInfo{.id = {}, .name = entry->name, .icon = entry->icon};
            }
        }
        return std::nullopt;
    }

    void applyAppInfo(PackageUpdate &update, const AppInfo &app) {
        update.kind = QString::fromLatin1(Kind::App);
        update.appId = app.id;
        update.name = app.name;
        if (!app.icon.isEmpty()) {
            update.icon = app.icon;
        }
    }

    void annotateRpmApps(QList<PackageUpdate> &updates) {
        QStringList nameArchs;
        nameArchs.reserve(updates.size());
        std::ranges::transform(updates, std::back_inserter(nameArchs), &PackageUpdate::id);
        const QHash<QString, AppFiles> appFiles = rpmAppFilesFor(nameArchs);
        for (PackageUpdate &update: updates) {
            const auto files = appFiles.constFind(update.id);
            if (files == appFiles.constEnd()) {
                continue;
            }
            if (const std::optional<AppInfo> app = resolveApp(rpmPackageName(update.id), &*files, readFile)) {
                applyAppInfo(update, *app);
            }
        }
    }

} // namespace UltramarineUpdates
