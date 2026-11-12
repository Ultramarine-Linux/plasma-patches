// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "installerdialog.h"

#include <KLocalizedString>
#include <KMessageWidget>
#include <KSycoca>

#include <QComboBox>
#include <QDBusConnection>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdint>
#include <iterator>

#include "flatpakinstaller.h"
#include "launch.h"
#include "packagekit.h"

using namespace Qt::StringLiterals;

namespace OfficeInstaller {

    namespace {
        enum Role : std::uint16_t {
            DescriptionRole = Qt::UserRole + 1,
            RecommendedRole,
        };

        constexpr int SuiteIconSize = 48;
        constexpr int HeaderIconSize = 48;
        constexpr int ProgressIconSize = 64;

        QString sourceName(const Source source) {
            switch (source) {
            case Source::Flatpak:
                return i18nc("@item:inlistbox install source", "Flatpak");
            case Source::Rpm:
                return i18nc("@item:inlistbox install source", "RPM");
            }
            Q_UNREACHABLE();
        }

        QString subtitle(const QList<QUrl> &urls, const QString &installedSuiteName) {
            if (!installedSuiteName.isEmpty()) {
                if (urls.isEmpty()) {
                    return i18nc("@info %1 is an office suite", "Install an office suite to use alongside %1.",
                                 installedSuiteName);
                }
                if (urls.size() == 1) {
                    return i18nc("@info %1 is an office suite, %2 a file name",
                                 "%1 can't open “%2”. Install another office suite to open it.", installedSuiteName,
                                 urls.constFirst().fileName());
                }
                // the singular is never shown but languages with several plural forms need i18np
                return i18ncp("@info %2 is an office suite",
                              "%2 can't open this document. Install another office suite to open it.",
                              "%2 can't open these %1 documents. Install another office suite to open them.",
                              urls.size(), installedSuiteName);
            }
            if (urls.isEmpty()) {
                return i18n("Install an office suite to open documents, spreadsheets and presentations.");
            }
            if (urls.size() == 1) {
                return i18n("Install an office suite to open “%1”.", urls.constFirst().fileName());
            }
            return i18np("Install an office suite to open this document.",
                         "Install an office suite to open these %1 documents.", urls.size());
        }

        QIcon documentIcon(const QList<QUrl> &urls) {
            QSet<QString> iconNames;
            for (const QUrl &url: urls) {
                const QMimeType mimeType = mimeTypeOf(url);
                iconNames.insert(QIcon::hasThemeIcon(mimeType.iconName()) ? mimeType.iconName()
                                                                          : mimeType.genericIconName());
            }
            if (iconNames.size() == 1 && QIcon::hasThemeIcon(*iconNames.cbegin())) {
                return QIcon::fromTheme(*iconNames.cbegin());
            }
            return QIcon::fromTheme(u"x-office-document"_s);
        }

        QIcon themeOrFlathubAppstreamIcon(const Suite &suite) {
            if (QIcon::hasThemeIcon(suite.flatpakId)) {
                return QIcon::fromTheme(suite.flatpakId);
            }
            const QString appstream =
                u"/flatpak/appstream/flathub/%1/active/icons"_s.arg(FlatpakInstaller::defaultArch());
            QStringList roots{u"/var/lib"_s + appstream};
            if (const QString userData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
                !userData.isEmpty()) {
                roots.prepend(userData + appstream);
            }
            QIcon icon;
            for (const QString &root: roots) {
                for (const auto size: {"128x128"_L1, "64x64"_L1}) {
                    if (const QString path = u"%1/%2/%3.png"_s.arg(root, size, suite.flatpakId);
                        QFileInfo::exists(path)) {
                        icon.addFile(path);
                    }
                }
                if (!icon.isNull()) {
                    return icon;
                }
            }
            return {};
        }

        QIcon suiteIcon(const Suite &suite) {
            const QIcon icon = themeOrFlathubAppstreamIcon(suite);
            return icon.isNull() ? QIcon::fromTheme(u"x-office-document"_s) : icon;
        }

        class SuiteDelegate : public QStyledItemDelegate {
        public:
            explicit SuiteDelegate(QListView *view) : QStyledItemDelegate(view), m_view(view) {}

            void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
                QStyleOptionViewItem opt = option;
                initStyleOption(&opt, index);
                const Geometry geometry = layout(opt, index, opt.rect.width());
                const QPoint origin = opt.rect.topLeft();
                const QPalette::ColorGroup group =
                    (opt.state & QStyle::State_Active) ? QPalette::Active : QPalette::Inactive;
                const QColor accentEvenWhenUnfocused = opt.palette.color(QPalette::Active, QPalette::Highlight);

                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);

                const auto translucentAccent = [&accentEvenWhenUnfocused](const qreal alpha) {
                    QColor color = accentEvenWhenUnfocused;
                    color.setAlphaF(alpha);
                    return color;
                };

                const QRectF card = QRectF(opt.rect).adjusted(0.5, 0.5, -0.5, -0.5);
                const qreal cardRadius = opt.fontMetrics.height() / 3.0;
                if (opt.state & QStyle::State_Selected) {
                    painter->setPen(QPen(accentEvenWhenUnfocused, 1));
                    painter->setBrush(translucentAccent(0.12));
                    painter->drawRoundedRect(card, cardRadius, cardRadius);
                } else if (opt.state & QStyle::State_MouseOver) {
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(translucentAccent(0.06));
                    painter->drawRoundedRect(card, cardRadius, cardRadius);
                }

                // some clangd versions reject QVariant::value<QIcon>(), so the icon comes from initStyleOption()
                opt.icon.paint(painter, geometry.icon.translated(origin));

                painter->setPen(opt.palette.color(group, QPalette::Text));
                painter->setFont(geometry.nameFont);
                painter->drawText(geometry.name.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                                  index.data(Qt::DisplayRole).toString());

                if (geometry.badge.isValid()) {
                    const QRectF badge = QRectF(geometry.badge.translated(origin)).adjusted(0.5, 0.5, -0.5, -0.5);
                    painter->setPen(QPen(accentEvenWhenUnfocused, 1));
                    painter->setBrush(translucentAccent(0.15));
                    const qreal radius = badge.height() / 2.0;
                    painter->drawRoundedRect(badge, radius, radius);
                    painter->setPen(opt.palette.color(group, QPalette::Text));
                    painter->setFont(geometry.badgeFont);
                    painter->drawText(badge, Qt::AlignCenter, recommendedText());
                }

                painter->setPen(opt.palette.color(group, QPalette::PlaceholderText));
                painter->setFont(opt.font);
                painter->drawText(geometry.description.translated(origin), Qt::AlignLeft | Qt::TextWordWrap,
                                  index.data(DescriptionRole).toString());
                painter->restore();
            }

            [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
                QStyleOptionViewItem opt = option;
                initStyleOption(&opt, index);
                const int spacingOnBothSides = 2 * m_view->spacing();
                const int width = m_view->viewport()->width() - spacingOnBothSides;
                return {width, layout(opt, index, width).height};
            }

            [[nodiscard]] int heightForWidth(const QModelIndex &index, const int width) const {
                QStyleOptionViewItem opt;
                opt.initFrom(m_view);
                initStyleOption(&opt, index);
                return layout(opt, index, width).height;
            }

        private:
            struct Geometry {
                QFont nameFont;
                QFont badgeFont;
                QRect icon;
                QRect name;
                QRect badge;
                QRect description;
                int height = 0;
            };

            static QString recommendedText() { return i18nc("@info:status office suite", "Recommended"); }

            static Geometry layout(const QStyleOptionViewItem &option, const QModelIndex &index, const int width) {
                Geometry geometry;
                const QFontMetrics metrics(option.font);
                const int padding = metrics.height() * 3 / 4;
                const int spacing = metrics.height() / 4;

                geometry.icon = QRect(padding, padding, SuiteIconSize, SuiteIconSize);
                const int textX = geometry.icon.right() + 1 + padding;
                const int textWidth = std::max(1, width - textX - padding);

                geometry.nameFont = option.font;
                geometry.nameFont.setBold(true);
                const QFontMetrics nameMetrics(geometry.nameFont);
                const QString name = index.data(Qt::DisplayRole).toString();
                geometry.name = QRect(textX, padding, nameMetrics.horizontalAdvance(name), nameMetrics.height());

                if (index.data(RecommendedRole).toBool()) {
                    geometry.badgeFont = option.font;
                    geometry.badgeFont.setPointSizeF(option.font.pointSizeF() * 0.85);
                    geometry.badgeFont.setWeight(QFont::DemiBold);
                    const QFontMetrics badgeMetrics(geometry.badgeFont);
                    const int badgeHeight = badgeMetrics.height() + 2;
                    geometry.badge =
                        QRect(geometry.name.right() + 1 + spacing * 2, geometry.name.center().y() - badgeHeight / 2,
                              badgeMetrics.horizontalAdvance(recommendedText()) + badgeHeight, badgeHeight);
                }

                const QRect description =
                    metrics.boundingRect(QRect(0, 0, textWidth, 0xffff), Qt::AlignLeft | Qt::TextWordWrap,
                                         index.data(DescriptionRole).toString());
                geometry.description =
                    QRect(textX, geometry.name.bottom() + 1 + spacing, textWidth, description.height());

                geometry.height = std::max(geometry.icon.bottom(), geometry.description.bottom()) + 1 + padding;
                return geometry;
            }

            QListView *m_view;
        };

        class SuiteList : public QListWidget {
        public:
            explicit SuiteList(QWidget *parent) : QListWidget(parent) {
                QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
                policy.setHeightForWidth(true);
                setSizePolicy(policy);
            }

            [[nodiscard]] bool hasHeightForWidth() const override { return true; }

            [[nodiscard]] int heightForWidth(const int width) const override {
                const auto *delegate = dynamic_cast<SuiteDelegate *>(itemDelegate());
                const int cardWidth = width - 2 * frameWidth() - 2 * spacing();
                int height = 2 * frameWidth() + spacing();
                for (int row = 0; row < count(); ++row) {
                    height += delegate->heightForWidth(model()->index(row, 0), cardWidth) + spacing();
                }
                return height;
            }

            [[nodiscard]] QSize sizeHint() const override {
                const int width = QListWidget::sizeHint().width();
                return {width, heightForWidth(width)};
            }
        };

        QLabel *iconLabel(const QIcon &icon, const int size, QWidget *parent) {
            auto *label = new QLabel(parent);
            label->setPixmap(icon.pixmap(size, size));
            label->setFixedSize(size, size);
            return label;
        }

        QLabel *titleLabel(const QString &text, QWidget *parent) {
            auto *label = new QLabel(text, parent);
            QFont font = label->font();
            font.setPointSizeF(font.pointSizeF() * 1.3);
            font.setBold(true);
            label->setFont(font);
            label->setWordWrap(true);
            return label;
        }

        QLabel *mutedLabel(const QString &text, QWidget *parent) {
            auto *label = new QLabel(text, parent);
            label->setWordWrap(true);
            label->setForegroundRole(QPalette::PlaceholderText);
            return label;
        }
    } // namespace

    InstallerDialog::InstallerDialog(const QList<QUrl> &urls, const QList<Suite> &offered,
                                     const QString &incapableInstalledSuite, QWidget *parent) :
        QDialog(parent), m_urls(urls), m_incapableInstalledSuite(incapableInstalledSuite), m_suites(offered),
        m_imageBased(isBootedFromOstree()), m_pages(new QStackedWidget(this)), m_error(new KMessageWidget(this)),
        m_suiteList(new SuiteList(this)), m_sourceRow(new QWidget(this)), m_sourceLabel(new QLabel(this)),
        m_sourceBox(new QComboBox(this)), m_progressIcon(new QLabel(this)), m_progressTitle(titleLabel({}, this)),
        m_progressDetail(mutedLabel({}, this)),
        m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this)) {
        setWindowTitle(i18nc("@title:window", "Install an Office Suite"));

        auto *layout = new QVBoxLayout(this);
        m_pages->addWidget(createChoicePage());
        m_pages->addWidget(createProgressPage());
        layout->addWidget(m_pages, 1);
        layout->addSpacing(fontMetrics().height() / 2);

        auto *sourceLayout = new QHBoxLayout(m_sourceRow);
        sourceLayout->setContentsMargins({});
        auto *sourceCaption = new QLabel(i18nc("@label:listbox", "Install from:"), m_sourceRow);
        sourceCaption->setBuddy(m_sourceBox);
        sourceLayout->addWidget(sourceCaption);
        sourceLayout->addWidget(m_sourceBox);
        sourceLayout->addWidget(m_sourceLabel);
        auto *footer = new QHBoxLayout;
        footer->addWidget(m_sourceRow);
        footer->addStretch();
        footer->addWidget(m_buttons);
        layout->addLayout(footer);

        QPushButton *installButton = m_buttons->button(QDialogButtonBox::Ok);
        installButton->setText(i18nc("@action:button", "Install"));
        installButton->setIcon(QIcon::fromTheme(u"install"_s));
        installButton->setDefault(true);
        connect(m_buttons, &QDialogButtonBox::accepted, this, &InstallerDialog::install);
        connect(m_buttons, &QDialogButtonBox::rejected, this, &InstallerDialog::reject);

        updateSources();
        setMinimumWidth(fontMetrics().averageCharWidth() * 70);
        const int width = std::max(minimumWidth(), QDialog::sizeHint().width());
        resize(width, QWidget::heightForWidth(width));
    }

    QWidget *InstallerDialog::createChoicePage() {
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins({});

        auto *header = new QHBoxLayout;
        header->setSpacing(fontMetrics().height());
        header->addWidget(iconLabel(documentIcon(m_urls), HeaderIconSize, page), 0, Qt::AlignTop);
        auto *headerText = new QVBoxLayout;
        headerText->setSpacing(0);
        headerText->addWidget(titleLabel(m_incapableInstalledSuite.isEmpty()
                                             ? i18nc("@title", "No Office Suite Installed")
                                             : i18nc("@title", "Install Another Office Suite"),
                                         page));
        m_subtitle = mutedLabel(subtitle(m_urls, m_incapableInstalledSuite), page);
        headerText->addWidget(m_subtitle);
        header->addLayout(headerText, 1);
        layout->addLayout(header);
        layout->addSpacing(fontMetrics().height() / 2);

        m_error->setMessageType(KMessageWidget::Error);
        m_error->setWordWrap(true);
        m_error->setCloseButtonVisible(true);
        m_error->hide();
        layout->addWidget(m_error);

        bool missingIcons = false;
        for (const Suite &suite: std::as_const(m_suites)) {
            missingIcons = missingIcons || themeOrFlathubAppstreamIcon(suite).isNull();
            auto *item = new QListWidgetItem(suiteIcon(suite), suite.name, m_suiteList);
            item->setData(DescriptionRole, suite.description);
            item->setData(RecommendedRole, suite.recommended);
        }
        if (missingIcons) {
            fetchFlathubIcons();
        }
        m_suiteList->setItemDelegate(new SuiteDelegate(m_suiteList));
        m_suiteList->setSelectionMode(QAbstractItemView::SingleSelection);
        m_suiteList->setResizeMode(QListView::Adjust);
        m_suiteList->setFrameShape(QFrame::NoFrame);
        m_suiteList->viewport()->setAutoFillBackground(false);
        m_suiteList->setSpacing(fontMetrics().height() / 6);
        m_suiteList->setMouseTracking(true);
        m_suiteList->viewport()->setAttribute(Qt::WA_Hover);
        m_suiteList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_suiteList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_suiteList->setCurrentRow(0);
        connect(m_suiteList, &QListWidget::currentRowChanged, this, &InstallerDialog::updateSources);
        const auto reselectAfterCtrlClickDeselect = [this] {
            if (m_suiteList->selectedItems().isEmpty() && m_suiteList->currentItem()) {
                m_suiteList->currentItem()->setSelected(true);
            }
        };
        connect(m_suiteList, &QListWidget::itemSelectionChanged, this, reselectAfterCtrlClickDeselect);
        connect(m_suiteList, &QListWidget::itemActivated, this, &InstallerDialog::install);
        layout->addWidget(m_suiteList);
        layout->addStretch();
        m_suiteList->setFocus();
        return page;
    }

    void InstallerDialog::fetchFlathubIcons() {
        auto *updater = new AppstreamUpdater(u"flathub"_s, this);
        connect(updater, &AppstreamUpdater::finished, this, [this, updater](const bool success) {
            updater->deleteLater();
            if (!success) {
                return;
            }
            for (int row = 0; row < m_suiteList->count(); ++row) {
                m_suiteList->item(row)->setIcon(suiteIcon(m_suites.at(row)));
            }
            if (m_installer) {
                m_progressIcon->setPixmap(suiteIcon(selectedSuite()).pixmap(ProgressIconSize, ProgressIconSize));
            }
        });
        updater->start();
    }

    QWidget *InstallerDialog::createProgressPage() {
        auto *page = new QWidget(this);
        page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        auto *layout = new QVBoxLayout(page);
        layout->addStretch();
        m_progressIcon->setFixedSize(ProgressIconSize, ProgressIconSize);
        layout->addWidget(m_progressIcon, 0, Qt::AlignHCenter);
        layout->addSpacing(fontMetrics().height() / 2);
        m_progressTitle->setAlignment(Qt::AlignCenter);
        layout->addWidget(m_progressTitle);
        auto *hint = mutedLabel(i18n("This can take a few minutes. You may be asked to authenticate."), page);
        hint->setAlignment(Qt::AlignCenter);
        layout->addWidget(hint);
        layout->addSpacing(fontMetrics().height());

        auto *progress = new QProgressBar(page);
        progress->setRange(0, 0);
        progress->setTextVisible(false);
        layout->addWidget(progress);
        m_progressDetail->setAlignment(Qt::AlignCenter);
        m_progressDetail->setWordWrap(false);
        m_progressDetail->setMinimumWidth(1);
        layout->addWidget(m_progressDetail);
        layout->addStretch();
        return page;
    }

    bool InstallerDialog::registerOnBus() {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected()) {
            return true;
        }
        bus.registerObject(u"/"_s, this, QDBusConnection::ExportScriptableSlots);
        if (bus.registerService(QLatin1StringView(DBusService))) {
            return true;
        }
        bus.unregisterObject(u"/"_s);
        return false;
    }

    void InstallerDialog::unregisterFromBus() {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterService(QLatin1StringView(DBusService));
        bus.unregisterObject(u"/"_s);
    }

    void InstallerDialog::reject() {
        if (m_waitingForAppsToStart) {
            // quitting now would kill the launch jobs
            hide();
            return;
        }
        if (!m_installer) {
            unregisterFromBus();
            QDialog::reject();
            return;
        }
        m_buttons->button(QDialogButtonBox::Cancel)->setEnabled(false);
        m_progressTitle->setText(i18nc("@info:status", "Canceling…"));
        m_progressDetail->clear();
        m_installer->cancel();
    }

    void InstallerDialog::addDocuments(const QStringList &urls) {
        std::ranges::transform(urls, std::back_inserter(m_urls), [](const QString &url) { return QUrl(url); });
        m_subtitle->setText(subtitle(m_urls, m_incapableInstalledSuite));
        raise();
        activateWindow();
    }

    const Suite &InstallerDialog::selectedSuite() const { return m_suites.at(std::max(0, m_suiteList->currentRow())); }

    Source InstallerDialog::selectedSource() const { return static_cast<Source>(m_sourceBox->currentData().toInt()); }

    void InstallerDialog::updateSources() const {
        m_sourceBox->clear();
        for (const Source source: installSources(selectedSuite(), m_imageBased)) {
            // an int because some clangd versions choke on custom enums in a QVariant
            m_sourceBox->addItem(sourceName(source), static_cast<int>(source));
        }
        const bool hasSourceChoice = m_sourceBox->count() > 1;
        m_sourceBox->setVisible(hasSourceChoice);
        m_sourceLabel->setVisible(!hasSourceChoice);
        m_sourceLabel->setText(m_sourceBox->currentText());
    }

    void InstallerDialog::install() {
        if (m_installer) {
            return;
        }
        const Suite &suite = selectedSuite();
        if (selectedSource() == Source::Rpm) {
            m_installer = new PackageKitInstaller(suite.rpmPackages, this);
        } else {
            m_installer = new FlatpakInstaller(u"flathub"_s, flatpakRef(suite, FlatpakInstaller::defaultArch()), this);
        }
        connect(m_installer, &Installer::progress, this, [this](const QString &text) {
            m_progressDetail->setText(
                m_progressDetail->fontMetrics().elidedText(text, Qt::ElideMiddle, m_progressDetail->width()));
        });
        connect(m_installer, &Installer::finished, this, &InstallerDialog::installFinished);

        m_error->animatedHide();
        m_progressIcon->setPixmap(suiteIcon(suite).pixmap(ProgressIconSize, ProgressIconSize));
        m_progressTitle->setText(i18nc("@info:status %1 is an office suite", "Installing %1…", suite.name));
        m_progressDetail->clear();
        setBusy(true);
        m_installer->start();
    }

    void InstallerDialog::setBusy(const bool busy) const {
        m_pages->setCurrentIndex(busy ? 1 : 0);
        m_sourceRow->setVisible(!busy);
        m_buttons->button(QDialogButtonBox::Ok)->setVisible(!busy);
        m_buttons->button(QDialogButtonBox::Cancel)->setEnabled(true);
    }

    void InstallerDialog::installFinished(const bool success, const bool canceled, const QString &error) {
        m_installer->deleteLater();
        m_installer = nullptr;

        if (success) {
            openDocuments();
            return;
        }
        setBusy(false);
        if (canceled) {
            m_error->setMessageType(KMessageWidget::Information);
            m_error->setText(i18nc("@info %1 is an office suite", "Installing %1 was canceled.", selectedSuite().name));
        } else {
            m_error->setMessageType(KMessageWidget::Error);
            m_error->setText(error.isEmpty() ? i18nc("@info %1 is an office suite", "%1 could not be installed.",
                                                     selectedSuite().name)
                                             : i18nc("@info %1 is an office suite, %2 the installer's error",
                                                     "%1 could not be installed: %2", selectedSuite().name, error));
        }
        m_error->animatedShow();
    }

    void InstallerDialog::returnToChoiceWithError(const QString &error) {
        setBusy(false);
        registerOnBus();
        m_error->setMessageType(KMessageWidget::Error);
        m_error->setText(error);
        m_error->animatedShow();
    }

    void InstallerDialog::openDocuments() {
        m_progressTitle->setText(i18nc("@info:status %1 is an office suite", "Opening %1…", selectedSuite().name));
        m_progressDetail->clear();
        m_buttons->button(QDialogButtonBox::Cancel)->setEnabled(false);
        unregisterFromBus();
        KSycoca::self()->ensureCacheValid();
        m_waitingForAppsToStart = true;
        if (openWithInstalledApps(m_urls, this, [this](const QString &error, const QList<QUrl> &failed) {
                m_waitingForAppsToStart = false;
                if (error.isEmpty()) {
                    accept();
                    return;
                }
                m_urls = failed;
                m_subtitle->setText(subtitle(m_urls, m_incapableInstalledSuite));
                show();
                returnToChoiceWithError(error);
            })) {
            return;
        }
        m_waitingForAppsToStart = false;

        QStringList files;
        files.reserve(m_urls.size());
        std::ranges::transform(m_urls, std::back_inserter(files),
                               [](const QUrl &url) { return url.isLocalFile() ? url.toLocalFile() : url.toString(); });
        if (const auto [program, arguments] = openFilesCommand(selectedSuite(), selectedSource(), files);
            !QProcess::startDetached(program, arguments)) {
            returnToChoiceWithError(i18nc("@info %1 is an office suite", "%1 was installed, but could not be started.",
                                          selectedSuite().name));
            return;
        }
        accept();
    }

} // namespace OfficeInstaller
