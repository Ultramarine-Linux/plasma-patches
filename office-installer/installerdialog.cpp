// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "installerdialog.h"

#include <KLocalizedString>
#include <KMessageWidget>
#include <KSycoca>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMimeDatabase>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QSysInfo>
#include <QVBoxLayout>

#include <cstdint>

#include "launch.h"

using namespace Qt::StringLiterals;

namespace OfficeInstaller {

    namespace {
        enum Role : std::uint16_t {
            DescriptionRole = Qt::UserRole + 1,
            RecommendedRole,
        };

        constexpr int SuiteIconSize = 48;
        constexpr int HeaderIconSize = 48;

        QString sourceName(const Source source) {
            switch (source) {
            case Source::Flatpak:
                return i18nc("@item:inlistbox install source", "Flatpak");
            case Source::Rpm:
                return i18nc("@item:inlistbox install source", "RPM");
            }
            Q_UNREACHABLE();
        }

        QString subtitle(const QList<QUrl> &urls) {
            if (urls.size() == 1) {
                return i18n("Install an office suite to open “%1”.", urls.constFirst().fileName());
            }
            if (urls.size() > 1) {
                return i18np("Install an office suite to open this document",
                             "Install an office suite to open these %1 documents", urls.size());
            }
            return i18n("Install an office suite to open documents, spreadsheets and presentations");
        }

        /// The documents' type icon if they're all the same type, or a generic document icon.
        QIcon documentIcon(const QList<QUrl> &urls) {
            QSet<QString> iconNames;
            for (const QUrl &url: urls) {
                const QMimeDatabase db;
                const QMimeType mimeType =
                    url.isLocalFile() ? db.mimeTypeForFile(url.toLocalFile()) : db.mimeTypeForUrl(url);
                iconNames.insert(QIcon::hasThemeIcon(mimeType.iconName()) ? mimeType.iconName()
                                                                          : mimeType.genericIconName());
            }
            if (iconNames.size() == 1 && QIcon::hasThemeIcon(*iconNames.cbegin())) {
                return QIcon::fromTheme(*iconNames.cbegin());
            }
            return QIcon::fromTheme(u"x-office-document"_s);
        }

        /// Flatpak's architecture name for this machine.
        QString flatpakArch() {
            QString arch = QSysInfo::currentCpuArchitecture();
            if (arch == "arm64"_L1) {
                return u"aarch64"_s;
            }
            return arch;
        }

        /**
         * The suite's icon. Flatpak keeps Flathub's app icons with its AppStream
         * data, so the icons are there before anything is installed. Falls back
         * to a generic document icon if the AppStream data hasn't been fetched.
         */
        QIcon suiteIcon(const Suite &suite) {
            if (QIcon::hasThemeIcon(suite.flatpakId)) {
                return QIcon::fromTheme(suite.flatpakId);
            }
            const QString appstream = u"/flatpak/appstream/flathub/%1/active/icons"_s.arg(flatpakArch());
            const QStringList roots{
                QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + appstream,
                u"/var/lib"_s + appstream,
            };
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
            return QIcon::fromTheme(u"x-office-document"_s);
        }

        /// A suite in the list: its icon, name and a "Recommended" badge, with the description under them.
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
                // The inactive highlight is grey, which would hide the selection when the dialog isn't focused.
                const QColor accent = opt.palette.color(QPalette::Active, QPalette::Highlight);

                painter->save();
                painter->setRenderHint(QPainter::Antialiasing);

                // A tinted card with an accent border, like Kirigami's, so the text keeps its own colors.
                const QRectF card = QRectF(opt.rect).adjusted(0.5, 0.5, -0.5, -0.5);
                const qreal cardRadius = opt.fontMetrics.height() / 3.0;
                if (opt.state & QStyle::State_Selected) {
                    QColor fill = accent;
                    fill.setAlphaF(0.12);
                    painter->setPen(QPen(accent, 1));
                    painter->setBrush(fill);
                    painter->drawRoundedRect(card, cardRadius, cardRadius);
                } else if (opt.state & QStyle::State_MouseOver) {
                    QColor fill = accent;
                    fill.setAlphaF(0.06);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(fill);
                    painter->drawRoundedRect(card, cardRadius, cardRadius);
                }

                // initStyleOption() has the icon: no QVariant::value<QIcon>(), which some clangd versions reject.
                opt.icon.paint(painter, geometry.icon.translated(origin));

                painter->setPen(opt.palette.color(group, QPalette::Text));
                painter->setFont(geometry.nameFont);
                painter->drawText(geometry.name.translated(origin), Qt::AlignLeft | Qt::AlignVCenter,
                                  index.data(Qt::DisplayRole).toString());

                if (geometry.badge.isValid()) {
                    // The accent is only the outline: accent-colored text on a tint is hard to read.
                    const QRectF badge = QRectF(geometry.badge.translated(origin)).adjusted(0.5, 0.5, -0.5, -0.5);
                    QColor fill = accent;
                    fill.setAlphaF(0.15);
                    painter->setPen(QPen(accent, 1));
                    painter->setBrush(fill);
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
                // QListView pads items by its spacing on every side.
                const int width = m_view->viewport()->width() - 2 * m_view->spacing();
                return {width, layout(opt, index, width).height};
            }

            /// The height of @p index's card when it's @p width wide.
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

        /// Grows to fit its cards, so there's no empty space under them.
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

        /// Flatpak and PackageKit redraw progress with carriage returns and colors.
        QString cleanLine(QString line) {
            static const QRegularExpression escapes(u"\x1b\\[[0-9;?]*[A-Za-z]"_s);
            line.remove(escapes);
            return line.section(u'\r', -1).trimmed();
        }
    } // namespace

    InstallerDialog::InstallerDialog(const QList<QUrl> &urls, QWidget *parent) :
        QDialog(parent), m_urls(urls), m_suites(suites()), m_imageBased(isImageBased()),
        m_pages(new QStackedWidget(this)), m_error(new KMessageWidget(this)), m_suiteList(new SuiteList(this)),
        m_sourceRow(new QWidget(this)), m_sourceLabel(new QLabel(this)), m_sourceBox(new QComboBox(this)),
        m_progressIcon(new QLabel(this)), m_progressTitle(titleLabel({}, this)), m_progressDetail(mutedLabel({}, this)),
        m_buttons(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this)) {
        setWindowTitle(i18nc("@title:window", "Install an Office Suite"));

        auto *layout = new QVBoxLayout(this);
        m_pages->addWidget(createChoicePage());
        m_pages->addWidget(createProgressPage());
        layout->addWidget(m_pages, 1);
        layout->addSpacing(fontMetrics().height() / 2);

        // The source sits next to the buttons, out of the way: Flathub is the default.
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
        // The suite list's height depends on its width, so size the dialog for the width it opens at.
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
        headerText->addWidget(titleLabel(i18nc("@title", "No Office Suite Installed"), page));
        headerText->addWidget(mutedLabel(subtitle(m_urls), page));
        header->addLayout(headerText, 1);
        layout->addLayout(header);
        layout->addSpacing(fontMetrics().height() / 2);

        m_error->setMessageType(KMessageWidget::Error);
        m_error->setWordWrap(true);
        m_error->setCloseButtonVisible(true);
        m_error->hide();
        layout->addWidget(m_error);

        for (const Suite &suite: std::as_const(m_suites)) {
            auto *item = new QListWidgetItem(suiteIcon(suite), suite.name, m_suiteList);
            item->setData(DescriptionRole, suite.description);
            item->setData(RecommendedRole, suite.recommended);
        }
        m_suiteList->setItemDelegate(new SuiteDelegate(m_suiteList));
        m_suiteList->setSelectionMode(QAbstractItemView::SingleSelection);
        m_suiteList->setResizeMode(QListView::Adjust);
        // Cards on the window background, not rows in a white box.
        m_suiteList->setFrameShape(QFrame::NoFrame);
        m_suiteList->viewport()->setAutoFillBackground(false);
        m_suiteList->setSpacing(fontMetrics().height() / 6);
        m_suiteList->setMouseTracking(true);
        m_suiteList->viewport()->setAttribute(Qt::WA_Hover);
        m_suiteList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_suiteList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_suiteList->setCurrentRow(0);
        connect(m_suiteList, &QListWidget::currentRowChanged, this, &InstallerDialog::updateSources);
        // Ctrl+click would leave nothing selected.
        connect(m_suiteList, &QListWidget::itemSelectionChanged, this, [this] {
            if (m_suiteList->selectedItems().isEmpty() && m_suiteList->currentItem()) {
                m_suiteList->currentItem()->setSelected(true);
            }
        });
        connect(m_suiteList, &QListWidget::itemActivated, this, &InstallerDialog::install);
        layout->addWidget(m_suiteList);
        layout->addStretch();
        m_suiteList->setFocus();
        return page;
    }

    QWidget *InstallerDialog::createProgressPage() {
        auto *page = new QWidget(this);
        // The choice page sets the dialog's size; this one fits in it.
        page->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        auto *layout = new QVBoxLayout(page);
        layout->addStretch();
        m_progressIcon->setFixedSize(64, 64);
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

    void InstallerDialog::reject() {
        if (!m_process) {
            QDialog::reject();
        }
    }

    const Suite &InstallerDialog::selectedSuite() const { return m_suites.at(std::max(0, m_suiteList->currentRow())); }

    Source InstallerDialog::selectedSource() const { return static_cast<Source>(m_sourceBox->currentData().toInt()); }

    void InstallerDialog::updateSources() const {
        m_sourceBox->clear();
        for (const Source source: sources(selectedSuite(), m_imageBased)) {
            // Stored as an int: a custom enum in a QVariant needs Qt to derive its name from
            // __PRETTY_FUNCTION__, which breaks in some clangd versions.
            m_sourceBox->addItem(sourceName(source), static_cast<int>(source));
        }
        // Nothing to choose for Flathub-only suites, so just say where it comes from.
        const bool choice = m_sourceBox->count() > 1;
        m_sourceBox->setVisible(choice);
        m_sourceLabel->setVisible(!choice);
        m_sourceLabel->setText(m_sourceBox->currentText());
    }

    void InstallerDialog::install() {
        if (m_process) {
            return;
        }
        const Suite &suite = selectedSuite();
        const auto [program, arguments] = installCommand(suite, selectedSource());
        m_lastLine.clear();
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyReadStandardOutput, this, &InstallerDialog::readOutput);
        connect(m_process, &QProcess::finished, this, &InstallerDialog::installFinished);
        connect(m_process, &QProcess::errorOccurred, this, [this](const QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                m_lastLine = m_process->errorString();
                installFinished(-1, QProcess::CrashExit);
            }
        });

        m_error->animatedHide();
        m_progressIcon->setPixmap(suiteIcon(suite).pixmap(64, 64));
        m_progressTitle->setText(i18nc("@info:status %1 is an office suite", "Installing %1…", suite.name));
        m_progressDetail->clear();
        setBusy(true);
        m_process->start(program, arguments);
    }

    void InstallerDialog::setBusy(const bool busy) const {
        m_pages->setCurrentIndex(busy ? 1 : 0);
        m_sourceRow->setVisible(!busy);
        m_buttons->button(QDialogButtonBox::Ok)->setVisible(!busy);
        m_buttons->button(QDialogButtonBox::Cancel)->setEnabled(!busy);
    }

    void InstallerDialog::readOutput() {
        for (const QStringList lines = QString::fromUtf8(m_process->readAllStandardOutput()).split(u'\n');
             const QString &line: lines) {
            if (const QString cleaned = cleanLine(line); !cleaned.isEmpty()) {
                m_lastLine = cleaned;
            }
        }
        m_progressDetail->setText(
            m_progressDetail->fontMetrics().elidedText(m_lastLine, Qt::ElideMiddle, m_progressDetail->width()));
    }

    void InstallerDialog::installFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
        if (!m_process) {
            return;
        }
        const bool succeeded = exitStatus == QProcess::NormalExit && exitCode == 0;
        if (m_process->state() == QProcess::NotRunning) {
            readOutput();
        }
        m_process->deleteLater();
        m_process = nullptr;

        if (!succeeded) {
            setBusy(false);
            m_error->setText(
                m_lastLine.isEmpty()
                    ? i18nc("@info %1 is an office suite", "%1 could not be installed.", selectedSuite().name)
                    : i18nc("@info %1 is an office suite, %2 the installer's error", "%1 could not be installed: %2",
                            selectedSuite().name, m_lastLine));
            m_error->animatedShow();
            return;
        }
        openDocuments();
    }

    void InstallerDialog::openDocuments() {
        m_progressTitle->setText(i18nc("@info:status %1 is an office suite", "Opening %1…", selectedSuite().name));
        m_progressDetail->clear();
        // Pick up the new suite's desktop files, so it opens like any other app.
        KSycoca::self()->ensureCacheValid();
        if (openWithInstalledApps(m_urls, this, [this] { accept(); })) {
            return;
        }

        QStringList files;
        for (const QUrl &url: std::as_const(m_urls)) {
            files.append(url.isLocalFile() ? url.toLocalFile() : url.toString());
        }
        const auto [program, arguments] = launchCommand(selectedSuite(), selectedSource(), files);
        QProcess::startDetached(program, arguments);
        accept();
    }

} // namespace OfficeInstaller
