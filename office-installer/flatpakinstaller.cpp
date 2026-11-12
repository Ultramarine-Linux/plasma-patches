// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "flatpakinstaller.h"

// GLib uses signals as a struct member name, which Qt defines as a keyword
#pragma push_macro("signals")
#undef signals
#include <flatpak.h>
#pragma pop_macro("signals")

#include <KLocalizedString>

#include <QThread>

#include <memory>

using namespace Qt::StringLiterals;

namespace OfficeInstaller {

    namespace {
        struct Result {
            bool success = false;
            bool canceled = false;
            QString error;
        };

        struct ProgressContext {
            FlatpakInstaller *installer;
            QString name;
        };

        QString appIdFromRef(const QString &fullRef) { return fullRef.section(u'/', 1, 1); }

        void progressChanged(FlatpakTransactionProgress *progress, gpointer data) {
            const auto *context = static_cast<ProgressContext *>(data);
            Q_EMIT context->installer->progress(i18nc("@info:progress %1 is a Flatpak app or runtime, %2 a percentage",
                                                      "%1: %2%", context->name,
                                                      flatpak_transaction_progress_get_progress(progress)));
        }

        void newOperation(FlatpakTransaction * /*transaction*/, FlatpakTransactionOperation *operation,
                          FlatpakTransactionProgress *progress, gpointer installer) {
            const QString ref = QString::fromUtf8(flatpak_transaction_operation_get_ref(operation));
            auto *context =
                new ProgressContext{.installer = static_cast<FlatpakInstaller *>(installer), .name = appIdFromRef(ref)};
            g_signal_connect_data(
                progress, "changed", G_CALLBACK(progressChanged), context,
                [](gpointer data, GClosure * /*closure*/) { delete static_cast<ProgressContext *>(data); },
                GConnectFlags{});
            progressChanged(progress, context);
        }

        struct RunContext {
            FlatpakInstaller *installer;
            QString fatalError;
        };

        gboolean continueUnlessFatal(FlatpakTransaction * /*transaction*/, FlatpakTransactionOperation *operation,
                                     const GError *error, const FlatpakTransactionErrorDetails details, gpointer data) {
            auto *context = static_cast<RunContext *>(data);
            if ((details & FLATPAK_TRANSACTION_ERROR_DETAILS_NON_FATAL) == 0) {
                // libflatpak reports any failure as canceled once the cancellable is set, so keep the real one
                if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                    context->fatalError = QString::fromUtf8(error->message);
                }
                return FALSE;
            }
            const QString ref = QString::fromUtf8(flatpak_transaction_operation_get_ref(operation));
            const QString message = QString::fromUtf8(error->message);
            qWarning("skipping %s: %s", qPrintable(ref), qPrintable(message));
            Q_EMIT context->installer->progress(
                i18nc("@info:progress %1 is a Flatpak runtime or extension, %2 the error", "Skipped %1: %2",
                      appIdFromRef(ref), message));
            return TRUE;
        }

        int chooseFirstRemote(FlatpakTransaction * /*transaction*/, const char * /*forRef*/,
                              const char * /*runtimeRef*/, const char *const * /*remotes*/, gpointer /*data*/) {
            return 0;
        }

        auto pushThreadDefaultMainContext() {
            GMainContext *mainContext = g_main_context_new();
            g_main_context_push_thread_default(mainContext);
            return std::unique_ptr<GMainContext, void (*)(GMainContext *)>(mainContext, [](GMainContext *c) {
                g_main_context_pop_thread_default(c);
                g_main_context_unref(c);
            });
        }

        bool updateAppstream(const QString &remote, GCancellable *cancellable) {
            const auto mainContext = pushThreadDefaultMainContext();
            g_autoptr(GError) error = nullptr;
            g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_system(cancellable, &error);
            if (installation &&
                flatpak_installation_update_appstream_full_sync(installation, remote.toUtf8().constData(), nullptr,
                                                                nullptr, nullptr, nullptr, cancellable, &error)) {
                return true;
            }
            if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                qWarning("couldn't update the %s AppStream data: %s", qPrintable(remote), error->message);
            }
            return false;
        }

        Result install(FlatpakInstaller *installer, const QString &remote, const QString &ref,
                       GCancellable *cancellable) {
            const auto mainContext = pushThreadDefaultMainContext();

            RunContext context{.installer = installer, .fatalError = {}};
            g_autoptr(GError) error = nullptr;
            g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_system(cancellable, &error);
            g_autoptr(FlatpakTransaction) transaction = nullptr;
            if (installation) {
                transaction = flatpak_transaction_new_for_installation(installation, cancellable, &error);
            }
            if (transaction) {
                flatpak_transaction_add_default_dependency_sources(transaction);
                g_signal_connect(transaction, "new-operation", G_CALLBACK(newOperation), installer);
                g_signal_connect(transaction, "choose-remote-for-ref", G_CALLBACK(chooseFirstRemote), nullptr);
                g_signal_connect(transaction, "operation-error", G_CALLBACK(continueUnlessFatal), &context);
                if (flatpak_transaction_add_install(transaction, remote.toUtf8().constData(), ref.toUtf8().constData(),
                                                    nullptr, &error)) {
                    flatpak_transaction_run(transaction, cancellable, &error);
                }
            }

            if (!error || g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED)) {
                return {.success = true, .canceled = false, .error = {}};
            }
            if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                const bool failedWhileCanceling = !context.fatalError.isEmpty();
                return {.success = false, .canceled = !failedWhileCanceling, .error = context.fatalError};
            }
            return {.success = false, .canceled = false, .error = QString::fromUtf8(error->message)};
        }
    } // namespace

    CancellableThread::~CancellableThread() {
        if (m_thread) {
            g_cancellable_cancel(m_cancellable);
            m_thread->wait();
            delete m_thread;
        }
        g_clear_object(&m_cancellable);
    }

    void CancellableThread::start(QObject *context, const std::function<void(GCancellable *)> &work,
                                  const std::function<void()> &finished) {
        if (m_thread) {
            return;
        }
        g_clear_object(&m_cancellable);
        m_cancellable = g_cancellable_new();
        m_thread = QThread::create([work, cancellable = m_cancellable] { work(cancellable); });
        QObject::connect(m_thread, &QThread::finished, context, [this, finished] {
            m_thread->deleteLater();
            m_thread = nullptr;
            finished();
        });
        m_thread->start();
    }

    void CancellableThread::cancel() const {
        if (m_thread) {
            g_cancellable_cancel(m_cancellable);
        }
    }

    FlatpakInstaller::FlatpakInstaller(const QString &remote, const QString &fullRef, QObject *parent) :
        Installer(parent), m_remote(remote), m_ref(fullRef) {}

    void FlatpakInstaller::start() {
        auto result = std::make_shared<Result>();
        m_thread.start(
            this,
            [this, result, remote = m_remote, ref = m_ref](GCancellable *cancellable) {
                *result = install(this, remote, ref, cancellable);
            },
            [this, result] { Q_EMIT finished(result->success, result->canceled, result->error); });
    }

    void FlatpakInstaller::cancel() { m_thread.cancel(); }

    AppstreamUpdater::AppstreamUpdater(const QString &remote, QObject *parent) : QObject(parent), m_remote(remote) {}

    void AppstreamUpdater::start() {
        auto success = std::make_shared<bool>(false);
        m_thread.start(
            this,
            [success, remote = m_remote](GCancellable *cancellable) {
                *success = updateAppstream(remote, cancellable);
            },
            [this, success] { Q_EMIT finished(*success); });
    }

    QString FlatpakInstaller::defaultArch() { return QString::fromUtf8(flatpak_get_default_arch()); }

} // namespace OfficeInstaller
