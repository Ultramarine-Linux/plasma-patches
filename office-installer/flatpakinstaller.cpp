// SPDX-FileCopyrightText: 2026 Cypress Reed <cypress@fyralabs.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "flatpakinstaller.h"

// GLib's headers use "signals" as a struct member, which Qt defines as a keyword
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

        /// what the progress callbacks need: where to report and what the operation is installing
        struct ProgressContext {
            FlatpakInstaller *installer;
            QString name;
        };

        void progressChanged(FlatpakTransactionProgress *progress, gpointer data) {
            const auto *context = static_cast<ProgressContext *>(data);
            Q_EMIT context->installer->progress(i18nc("@info:progress %1 is a Flatpak app or runtime, %2 a percentage",
                                                      "%1: %2%", context->name,
                                                      flatpak_transaction_progress_get_progress(progress)));
        }

        void newOperation(FlatpakTransaction * /*transaction*/, FlatpakTransactionOperation *operation,
                          FlatpakTransactionProgress *progress, gpointer installer) {
            // app/org.example.App/x86_64/stable -> org.example.App
            const QString ref = QString::fromUtf8(flatpak_transaction_operation_get_ref(operation));
            auto *context = new ProgressContext{.installer = static_cast<FlatpakInstaller *>(installer),
                                                .name = ref.section(u'/', 1, 1)};
            g_signal_connect_data(
                progress, "changed", G_CALLBACK(progressChanged), context,
                [](gpointer data, GClosure * /*closure*/) { delete static_cast<ProgressContext *>(data); },
                GConnectFlags{});
            progressChanged(progress, context);
        }

        /**
         * keeps going after errors in related refs (translations, GL extensions),
         * which libflatpak marks non-fatal. Anything else stops the transaction
         */
        gboolean operationError(FlatpakTransaction * /*transaction*/, FlatpakTransactionOperation *operation,
                                const GError *error, const FlatpakTransactionErrorDetails details, gpointer installer) {
            if ((details & FLATPAK_TRANSACTION_ERROR_DETAILS_NON_FATAL) == 0) {
                return FALSE;
            }
            const QString ref = QString::fromUtf8(flatpak_transaction_operation_get_ref(operation));
            const QString message = QString::fromUtf8(error->message);
            qWarning("skipping %s: %s", qPrintable(ref), qPrintable(message));
            Q_EMIT static_cast<FlatpakInstaller *>(installer)->progress(
                i18nc("@info:progress %1 is a Flatpak runtime or extension, %2 the error", "Skipped %1: %2",
                      ref.section(u'/', 1, 1), message));
            return TRUE;
        }

        /// a runtime is in more than one remote: take the first, like flatpak install -y
        int chooseRemote(FlatpakTransaction * /*transaction*/, const char * /*forRef*/, const char * /*runtimeRef*/,
                         const char *const * /*remotes*/, gpointer /*data*/) {
            return 0;
        }

        Result install(FlatpakInstaller *installer, const QString &remote, const QString &ref,
                       GCancellable *cancellable) {
            // the transaction runs its callbacks on the thread's default context
            GMainContext *mainContext = g_main_context_new();
            g_main_context_push_thread_default(mainContext);
            const std::unique_ptr<GMainContext, void (*)(GMainContext *)> popContext(mainContext, [](GMainContext *c) {
                g_main_context_pop_thread_default(c);
                g_main_context_unref(c);
            });

            g_autoptr(GError) error = nullptr;
            g_autoptr(FlatpakInstallation) installation = flatpak_installation_new_system(cancellable, &error);
            g_autoptr(FlatpakTransaction) transaction = nullptr;
            if (installation) {
                transaction = flatpak_transaction_new_for_installation(installation, cancellable, &error);
            }
            if (transaction) {
                flatpak_transaction_add_default_dependency_sources(transaction);
                g_signal_connect(transaction, "new-operation", G_CALLBACK(newOperation), installer);
                g_signal_connect(transaction, "choose-remote-for-ref", G_CALLBACK(chooseRemote), nullptr);
                g_signal_connect(transaction, "operation-error", G_CALLBACK(operationError), installer);
                if (flatpak_transaction_add_install(transaction, remote.toUtf8().constData(), ref.toUtf8().constData(),
                                                    nullptr, &error)) {
                    flatpak_transaction_run(transaction, cancellable, &error);
                }
            }

            if (!error || g_error_matches(error, FLATPAK_ERROR, FLATPAK_ERROR_ALREADY_INSTALLED)) {
                return {.success = true, .canceled = false, .error = {}};
            }
            if (g_cancellable_is_cancelled(cancellable) || g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                return {.success = false, .canceled = true, .error = {}};
            }
            return {.success = false, .canceled = false, .error = QString::fromUtf8(error->message)};
        }
    } // namespace

    FlatpakInstaller::FlatpakInstaller(const QString &remote, const QString &ref, QObject *parent) :
        Installer(parent), m_remote(remote), m_ref(ref) {}

    FlatpakInstaller::~FlatpakInstaller() {
        if (m_thread) {
            g_cancellable_cancel(m_cancellable);
            m_thread->wait();
            delete m_thread;
        }
        g_clear_object(&m_cancellable);
    }

    void FlatpakInstaller::start() {
        if (m_thread) {
            return;
        }
        g_clear_object(&m_cancellable);
        m_cancellable = g_cancellable_new();
        auto result = std::make_shared<Result>();
        m_thread = QThread::create([this, result, remote = m_remote, ref = m_ref, cancellable = m_cancellable] {
            *result = install(this, remote, ref, cancellable);
        });
        connect(m_thread, &QThread::finished, this, [this, result] {
            m_thread->deleteLater();
            m_thread = nullptr;
            Q_EMIT finished(result->success, result->canceled, result->error);
        });
        m_thread->start();
    }

    void FlatpakInstaller::cancel() {
        if (m_thread) {
            g_cancellable_cancel(m_cancellable);
        }
    }

    QString FlatpakInstaller::defaultArch() { return QString::fromUtf8(flatpak_get_default_arch()); }

} // namespace OfficeInstaller
