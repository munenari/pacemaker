/*
 * Copyright 2010-2019 Andrew Beekhof <andrew@beekhof.net>
 *
 * This source code is licensed under the GNU Lesser General Public License
 * version 2.1 or later (LGPLv2.1+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <grp.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>

#ifdef HAVE_SYS_SIGNALFD_H
#include <sys/signalfd.h>
#endif

#include "crm/crm.h"
#include "crm/common/mainloop.h"
#include "crm/services.h"

#include "services_private.h"

#if SUPPORT_CIBSECRETS
#  include "crm/common/cib_secrets.h"
#endif

static gboolean
svc_read_output(int fd, svc_action_t * op, bool is_stderr)
{
    char *data = NULL;
    int rc = 0, len = 0;
    char buf[500];
    static const size_t buf_read_len = sizeof(buf) - 1;


    if (fd < 0) {
        crm_trace("No fd for %s", op->id);
        return FALSE;
    }

    if (is_stderr && op->stderr_data) {
        len = strlen(op->stderr_data);
        data = op->stderr_data;
        crm_trace("Reading %s stderr into offset %d", op->id, len);

    } else if (is_stderr == FALSE && op->stdout_data) {
        len = strlen(op->stdout_data);
        data = op->stdout_data;
        crm_trace("Reading %s stdout into offset %d", op->id, len);

    } else {
        crm_trace("Reading %s %s into offset %d", op->id, is_stderr?"stderr":"stdout", len);
    }

    do {
        rc = read(fd, buf, buf_read_len);
        if (rc > 0) {
            buf[rc] = 0;
            crm_trace("Got %d chars: %.80s", rc, buf);
            data = realloc_safe(data, len + rc + 1);
            len += sprintf(data + len, "%s", buf);

        } else if (errno != EINTR) {
            /* error or EOF
             * Cleanup happens in pipe_done()
             */
            rc = FALSE;
            break;
        }

    } while (rc == buf_read_len || rc < 0);

    if (is_stderr) {
        op->stderr_data = data;
    } else {
        op->stdout_data = data;
    }

    return rc;
}

static int
dispatch_stdout(gpointer userdata)
{
    svc_action_t *op = (svc_action_t *) userdata;

    return svc_read_output(op->opaque->stdout_fd, op, FALSE);
}

static int
dispatch_stderr(gpointer userdata)
{
    svc_action_t *op = (svc_action_t *) userdata;

    return svc_read_output(op->opaque->stderr_fd, op, TRUE);
}

static void
pipe_out_done(gpointer user_data)
{
    svc_action_t *op = (svc_action_t *) user_data;

    crm_trace("%p", op);

    op->opaque->stdout_gsource = NULL;
    if (op->opaque->stdout_fd > STDOUT_FILENO) {
        close(op->opaque->stdout_fd);
    }
    op->opaque->stdout_fd = -1;
}

static void
pipe_err_done(gpointer user_data)
{
    svc_action_t *op = (svc_action_t *) user_data;

    op->opaque->stderr_gsource = NULL;
    if (op->opaque->stderr_fd > STDERR_FILENO) {
        close(op->opaque->stderr_fd);
    }
    op->opaque->stderr_fd = -1;
}

static struct mainloop_fd_callbacks stdout_callbacks = {
    .dispatch = dispatch_stdout,
    .destroy = pipe_out_done,
};

static struct mainloop_fd_callbacks stderr_callbacks = {
    .dispatch = dispatch_stderr,
    .destroy = pipe_err_done,
};

static void
set_ocf_env(const char *key, const char *value, gpointer user_data)
{
    if (setenv(key, value, 1) != 0) {
        crm_perror(LOG_ERR, "setenv failed for key:%s and value:%s", key, value);
    }
}

static void
set_ocf_env_with_prefix(gpointer key, gpointer value, gpointer user_data)
{
    char buffer[500];

    snprintf(buffer, sizeof(buffer), "OCF_RESKEY_%s", (char *)key);
    set_ocf_env(buffer, value, user_data);
}

static void
set_alert_env(gpointer key, gpointer value, gpointer user_data)
{
    int rc;

    if (value != NULL) {
        rc = setenv(key, value, 1);
    } else {
        rc = unsetenv(key);
    }

    if (rc < 0) {
        crm_perror(LOG_ERR, "setenv %s=%s",
                  (char*)key, (value? (char*)value : ""));
    } else {
        crm_trace("setenv %s=%s", (char*)key, (value? (char*)value : ""));
    }
}

/*!
 * \internal
 * \brief Add environment variables suitable for an action
 *
 * \param[in] op  Action to use
 */
static void
add_action_env_vars(const svc_action_t *op)
{
    void (*env_setter)(gpointer, gpointer, gpointer) = NULL;
    if (op->agent == NULL) {
        env_setter = set_alert_env;  /* we deal with alert handler */

    } else if (safe_str_eq(op->standard, PCMK_RESOURCE_CLASS_OCF)) {
        env_setter = set_ocf_env_with_prefix;
    }

    if (env_setter != NULL && op->params != NULL) {
        g_hash_table_foreach(op->params, env_setter, NULL);
    }

    if (env_setter == NULL || env_setter == set_alert_env) {
        return;
    }

    set_ocf_env("OCF_RA_VERSION_MAJOR", "1", NULL);
    set_ocf_env("OCF_RA_VERSION_MINOR", "0", NULL);
    set_ocf_env("OCF_ROOT", OCF_ROOT_DIR, NULL);
    set_ocf_env("OCF_EXIT_REASON_PREFIX", PCMK_OCF_REASON_PREFIX, NULL);

    if (op->rsc) {
        set_ocf_env("OCF_RESOURCE_INSTANCE", op->rsc, NULL);
    }

    if (op->agent != NULL) {
        set_ocf_env("OCF_RESOURCE_TYPE", op->agent, NULL);
    }

    /* Notes: this is not added to specification yet. Sept 10,2004 */
    if (op->provider != NULL) {
        set_ocf_env("OCF_RESOURCE_PROVIDER", op->provider, NULL);
    }
}

static void
pipe_in_single_parameter(gpointer key, gpointer value, gpointer user_data)
{
    svc_action_t *op = user_data;
    char *buffer = crm_strdup_printf("%s=%s\n", (char *)key, (char *) value);
    int ret, total = 0, len = strlen(buffer);

    do {
        errno = 0;
        ret = write(op->opaque->stdin_fd, buffer + total, len - total);
        if (ret > 0) {
            total += ret;
        }

    } while ((errno == EINTR) && (total < len));
    free(buffer);
}

/*!
 * \internal
 * \brief Pipe parameters in via stdin for action
 *
 * \param[in] op  Action to use
 */
static void
pipe_in_action_stdin_parameters(const svc_action_t *op)
{
    crm_debug("sending args");
    if (op->params) {
        g_hash_table_foreach(op->params, pipe_in_single_parameter, (gpointer) op);
    }
}

gboolean
recurring_action_timer(gpointer data)
{
    svc_action_t *op = data;

    crm_debug("Scheduling another invocation of %s", op->id);

    /* Clean out the old result */
    free(op->stdout_data);
    op->stdout_data = NULL;
    free(op->stderr_data);
    op->stderr_data = NULL;
    op->opaque->repeat_timer = 0;

    services_action_async(op, NULL);
    return FALSE;
}

/* Returns FALSE if 'op' should be free'd by the caller */
gboolean
operation_finalize(svc_action_t * op)
{
    int recurring = 0;

    if (op->interval_ms) {
        if (op->cancel) {
            op->status = PCMK_LRM_OP_CANCELLED;
            cancel_recurring_action(op);
        } else {
            recurring = 1;
            op->opaque->repeat_timer = g_timeout_add(op->interval_ms,
                                                     recurring_action_timer, (void *)op);
        }
    }

    if (op->opaque->callback) {
        op->opaque->callback(op);
    }

    op->pid = 0;

    services_untrack_op(op);

    if (!recurring && op->synchronous == FALSE) {
        /*
         * If this is a recurring action, do not free explicitly.
         * It will get freed whenever the action gets cancelled.
         */
        services_action_free(op);
        return TRUE;
    }

    services_action_cleanup(op);
    return FALSE;
}

static void
operation_finished(mainloop_child_t * p, pid_t pid, int core, int signo, int exitcode)
{
    svc_action_t *op = mainloop_child_userdata(p);
    char *prefix = crm_strdup_printf("%s:%d", op->id, op->pid);

    mainloop_clear_child_userdata(p);
    op->status = PCMK_LRM_OP_DONE;
    CRM_ASSERT(op->pid == pid);

    crm_trace("%s %p %p", prefix, op->opaque->stderr_gsource, op->opaque->stdout_gsource);
    if (op->opaque->stderr_gsource) {
        /* Make sure we have read everything from the buffer.
         * Depending on the priority mainloop gives the fd, operation_finished
         * could occur before all the reads are done.  Force the read now.*/
        crm_trace("%s dispatching stderr", prefix);
        dispatch_stderr(op);
        crm_trace("%s: %p", op->id, op->stderr_data);
        mainloop_del_fd(op->opaque->stderr_gsource);
        op->opaque->stderr_gsource = NULL;
    }

    if (op->opaque->stdout_gsource) {
        /* Make sure we have read everything from the buffer.
         * Depending on the priority mainloop gives the fd, operation_finished
         * could occur before all the reads are done.  Force the read now.*/
        crm_trace("%s dispatching stdout", prefix);
        dispatch_stdout(op);
        crm_trace("%s: %p", op->id, op->stdout_data);
        mainloop_del_fd(op->opaque->stdout_gsource);
        op->opaque->stdout_gsource = NULL;
    }

    if (op->opaque->stdin_fd >= 0) {
        close(op->opaque->stdin_fd);
    }

    if (signo) {
        if (mainloop_child_timeout(p)) {
            crm_warn("%s - timed out after %dms", prefix, op->timeout);
            op->status = PCMK_LRM_OP_TIMEOUT;
            op->rc = PCMK_OCF_TIMEOUT;

        } else if (op->cancel) {
            /* If an in-flight recurring operation was killed because it was
             * cancelled, don't treat that as a failure.
             */
            crm_info("%s - terminated with signal %d", prefix, signo);
            op->status = PCMK_LRM_OP_CANCELLED;
            op->rc = PCMK_OCF_OK;

        } else {
            crm_warn("%s - terminated with signal %d", prefix, signo);
            op->status = PCMK_LRM_OP_ERROR;
            op->rc = PCMK_OCF_SIGNAL;
        }

    } else {
        op->rc = exitcode;
        crm_debug("%s - exited with rc=%d", prefix, exitcode);
    }

    free(prefix);
    prefix = crm_strdup_printf("%s:%d:stderr", op->id, op->pid);
    crm_log_output(LOG_NOTICE, prefix, op->stderr_data);

    free(prefix);
    prefix = crm_strdup_printf("%s:%d:stdout", op->id, op->pid);
    crm_log_output(LOG_DEBUG, prefix, op->stdout_data);

    free(prefix);
    operation_finalize(op);
}

/*!
 * \internal
 * \brief Set operation rc and status per errno from stat(), fork() or execvp()
 *
 * \param[in,out] op     Operation to set rc and status for
 * \param[in]     error  Value of errno after system call
 *
 * \return void
 */
static void
services_handle_exec_error(svc_action_t * op, int error)
{
    int rc_not_installed, rc_insufficient_priv, rc_exec_error;

    /* Mimic the return codes for each standard as that's what we'll convert back from in get_uniform_rc() */
    if (safe_str_eq(op->standard, PCMK_RESOURCE_CLASS_LSB)
        && safe_str_eq(op->action, "status")) {

        rc_not_installed = PCMK_LSB_STATUS_NOT_INSTALLED;
        rc_insufficient_priv = PCMK_LSB_STATUS_INSUFFICIENT_PRIV;
        rc_exec_error = PCMK_LSB_STATUS_UNKNOWN;

#if SUPPORT_NAGIOS
    } else if (safe_str_eq(op->standard, PCMK_RESOURCE_CLASS_NAGIOS)) {
        rc_not_installed = NAGIOS_NOT_INSTALLED;
        rc_insufficient_priv = NAGIOS_INSUFFICIENT_PRIV;
        rc_exec_error = PCMK_OCF_EXEC_ERROR;
#endif

    } else {
        rc_not_installed = PCMK_OCF_NOT_INSTALLED;
        rc_insufficient_priv = PCMK_OCF_INSUFFICIENT_PRIV;
        rc_exec_error = PCMK_OCF_EXEC_ERROR;
    }

    switch (error) {   /* see execve(2), stat(2) and fork(2) */
        case ENOENT:   /* No such file or directory */
        case EISDIR:   /* Is a directory */
        case ENOTDIR:  /* Path component is not a directory */
        case EINVAL:   /* Invalid executable format */
        case ENOEXEC:  /* Invalid executable format */
            op->rc = rc_not_installed;
            op->status = PCMK_LRM_OP_NOT_INSTALLED;
            break;
        case EACCES:   /* permission denied (various errors) */
        case EPERM:    /* permission denied (various errors) */
            op->rc = rc_insufficient_priv;
            op->status = PCMK_LRM_OP_ERROR;
            break;
        default:
            op->rc = rc_exec_error;
            op->status = PCMK_LRM_OP_ERROR;
    }
}

static void
action_launch_child(svc_action_t *op)
{
    int lpc;

    /* SIGPIPE is ignored (which is different from signal blocking) by the gnutls library.
     * Depending on the libqb version in use, libqb may set SIGPIPE to be ignored as well. 
     * We do not want this to be inherited by the child process. By resetting this the signal
     * to the default behavior, we avoid some potential odd problems that occur during OCF
     * scripts when SIGPIPE is ignored by the environment. */
    signal(SIGPIPE, SIG_DFL);

#if defined(HAVE_SCHED_SETSCHEDULER)
    if (sched_getscheduler(0) != SCHED_OTHER) {
        struct sched_param sp;

        memset(&sp, 0, sizeof(sp));
        sp.sched_priority = 0;

        if (sched_setscheduler(0, SCHED_OTHER, &sp) == -1) {
            crm_perror(LOG_ERR, "Could not reset scheduling policy to SCHED_OTHER for %s", op->id);
        }
    }
#endif
    if (setpriority(PRIO_PROCESS, 0, 0) == -1) {
        crm_perror(LOG_ERR, "Could not reset process priority to 0 for %s", op->id);
    }

    /* Man: The call setpgrp() is equivalent to setpgid(0,0)
     * _and_ compiles on BSD variants too
     * need to investigate if it works the same too.
     */
    setpgid(0, 0);

    // Close all file descriptors except stdin/stdout/stderr
    for (lpc = getdtablesize() - 1; lpc > STDERR_FILENO; lpc--) {
        close(lpc);
    }

#if SUPPORT_CIBSECRETS
    if (replace_secret_params(op->rsc, op->params) < 0) {
        /* replacing secrets failed! */
        if (safe_str_eq(op->action,"stop")) {
            /* don't fail on stop! */
            crm_info("proceeding with the stop operation for %s", op->rsc);

        } else {
            crm_err("failed to get secrets for %s, "
                    "considering resource not configured", op->rsc);
            _exit(PCMK_OCF_NOT_CONFIGURED);
        }
    }
#endif

    add_action_env_vars(op);

    /* Become the desired user */
    if (op->opaque->uid && (geteuid() == 0)) {

        // If requested, set effective group
        if (op->opaque->gid && (setgid(op->opaque->gid) < 0)) {
            crm_perror(LOG_ERR, "Could not set child group to %d", op->opaque->gid);
            _exit(PCMK_OCF_NOT_CONFIGURED);
        }

        // Erase supplementary group list
        // (We could do initgroups() if we kept a copy of the username)
        if (setgroups(0, NULL) < 0) {
            crm_perror(LOG_ERR, "Could not set child groups");
            _exit(PCMK_OCF_NOT_CONFIGURED);
        }

        // Set effective user
        if (setuid(op->opaque->uid) < 0) {
            crm_perror(LOG_ERR, "setting user to %d", op->opaque->uid);
            _exit(PCMK_OCF_NOT_CONFIGURED);
        }
    }

    /* execute the RA */
    execvp(op->opaque->exec, op->opaque->args);

    /* Most cases should have been already handled by stat() */
    services_handle_exec_error(op, errno);

    _exit(op->rc);
}

#ifndef HAVE_SYS_SIGNALFD_H
static int sigchld_pipe[2] = { -1, -1 };

static void
sigchld_handler()
{
    if ((sigchld_pipe[1] >= 0) && (write(sigchld_pipe[1], "", 1) == -1)) {
        crm_perror(LOG_TRACE, "Could not poke SIGCHLD self-pipe");
    }
}
#endif

static void
action_synced_wait(svc_action_t * op, sigset_t *mask)
{
    int status = 0;
    int timeout = op->timeout;
    int sfd = -1;
    time_t start = -1;
    struct pollfd fds[3];
    int wait_rc = 0;

#ifdef HAVE_SYS_SIGNALFD_H
    sfd = signalfd(-1, mask, SFD_NONBLOCK);
    if (sfd < 0) {
        crm_perror(LOG_ERR, "signalfd() failed");
    }
#else
    sfd = sigchld_pipe[0];
#endif

    fds[0].fd = op->opaque->stdout_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    fds[1].fd = op->opaque->stderr_fd;
    fds[1].events = POLLIN;
    fds[1].revents = 0;

    fds[2].fd = sfd;
    fds[2].events = POLLIN;
    fds[2].revents = 0;

    crm_trace("Waiting for %d", op->pid);
    start = time(NULL);
    do {
        int poll_rc = poll(fds, 3, timeout);

        if (poll_rc > 0) {
            if (fds[0].revents & POLLIN) {
                svc_read_output(op->opaque->stdout_fd, op, FALSE);
            }

            if (fds[1].revents & POLLIN) {
                svc_read_output(op->opaque->stderr_fd, op, TRUE);
            }

            if (fds[2].revents & POLLIN) {
#ifdef HAVE_SYS_SIGNALFD_H
                struct signalfd_siginfo fdsi;
                ssize_t s;

                s = read(sfd, &fdsi, sizeof(struct signalfd_siginfo));
                if (s != sizeof(struct signalfd_siginfo)) {
                    crm_perror(LOG_ERR, "Read from signal fd %d failed", sfd);

                } else if (fdsi.ssi_signo == SIGCHLD) {
#else
                if (1) {
                    /* Clear out the sigchld pipe. */
                    char ch;
                    while (read(sfd, &ch, 1) == 1) /*omit*/;
#endif
                    wait_rc = waitpid(op->pid, &status, WNOHANG);

                    if (wait_rc > 0) {
                        break;

                    } else if (wait_rc < 0){
                        if (errno == ECHILD) {
                                /* Here, don't dare to kill and bail out... */
                                break;

                        } else {
                                /* ...otherwise pretend process still runs. */
                                wait_rc = 0;
                        }
                        crm_perror(LOG_ERR, "waitpid() for %d failed", op->pid);
                    }
                }
            }

        } else if (poll_rc == 0) {
            timeout = 0;
            break;

        } else if (poll_rc < 0) {
            if (errno != EINTR) {
                crm_perror(LOG_ERR, "poll() failed");
                break;
            }
        }

        timeout = op->timeout - (time(NULL) - start) * 1000;

    } while ((op->timeout < 0 || timeout > 0));

    crm_trace("Child done: %d", op->pid);
    if (wait_rc <= 0) {
        op->rc = PCMK_OCF_UNKNOWN_ERROR;

        if (op->timeout > 0 && timeout <= 0) {
            op->status = PCMK_LRM_OP_TIMEOUT;
            crm_warn("%s:%d - timed out after %dms", op->id, op->pid, op->timeout);

        } else {
            op->status = PCMK_LRM_OP_ERROR;
        }

        /* If only child hasn't been successfully waited for, yet.
           This is to limit killing wrong target a bit more. */
        if (wait_rc == 0 && waitpid(op->pid, &status, WNOHANG) == 0) {
            if (kill(op->pid, SIGKILL)) {
                crm_err("kill(%d, KILL) failed: %d", op->pid, errno);
            }
            /* Safe to skip WNOHANG here as we sent non-ignorable signal. */
            while (waitpid(op->pid, &status, 0) == (pid_t) -1 && errno == EINTR) /*omit*/;
        }

    } else if (WIFEXITED(status)) {
        op->status = PCMK_LRM_OP_DONE;
        op->rc = WEXITSTATUS(status);
        crm_info("Managed %s process %d exited with rc=%d", op->id, op->pid, op->rc);

    } else if (WIFSIGNALED(status)) {
        int signo = WTERMSIG(status);

        op->status = PCMK_LRM_OP_ERROR;
        crm_err("Managed %s process %d exited with signal=%d", op->id, op->pid, signo);
    }
#ifdef WCOREDUMP
    if (WCOREDUMP(status)) {
        crm_err("Managed %s process %d dumped core", op->id, op->pid);
    }
#endif

    svc_read_output(op->opaque->stdout_fd, op, FALSE);
    svc_read_output(op->opaque->stderr_fd, op, TRUE);

    close(op->opaque->stdout_fd);
    close(op->opaque->stderr_fd);
    if (op->opaque->stdin_fd >= 0) {
        close(op->opaque->stdin_fd);
    }

#ifdef HAVE_SYS_SIGNALFD_H
    close(sfd);
#endif
}

/* For an asynchronous 'op', returns FALSE if 'op' should be free'd by the caller */
/* For a synchronous 'op', returns FALSE if 'op' fails */
gboolean
services_os_action_execute(svc_action_t * op)
{
    int stdout_fd[2];
    int stderr_fd[2];
    int stdin_fd[2] = {-1, -1};
    int rc;
    struct stat st;
    sigset_t *pmask = NULL;

#ifdef HAVE_SYS_SIGNALFD_H
    sigset_t mask;
    sigset_t old_mask;
#define sigchld_cleanup() do {                                                \
    if (sigismember(&old_mask, SIGCHLD) == 0) {                               \
        if (sigprocmask(SIG_UNBLOCK, &mask, NULL) < 0) {                      \
            crm_perror(LOG_ERR, "sigprocmask() failed to unblock sigchld");   \
        }                                                                     \
    }                                                                         \
} while (0)
#else
    struct sigaction sa;
    struct sigaction old_sa;
#define sigchld_cleanup() do {                                                \
    if (sigaction(SIGCHLD, &old_sa, NULL) < 0) {                              \
        crm_perror(LOG_ERR, "sigaction() failed to remove sigchld handler");  \
    }                                                                         \
    close(sigchld_pipe[0]);                                                   \
    close(sigchld_pipe[1]);                                                   \
    sigchld_pipe[0] = sigchld_pipe[1] = -1;                                   \
} while(0)
#endif

    /* Fail fast */
    if(stat(op->opaque->exec, &st) != 0) {
        rc = errno;
        crm_warn("Cannot execute '%s': %s (%d)", op->opaque->exec, pcmk_strerror(rc), rc);
        services_handle_exec_error(op, rc);
        if (!op->synchronous) {
            return operation_finalize(op);
        }
        return FALSE;
    }

    if (pipe(stdout_fd) < 0) {
        rc = errno;

        crm_err("pipe(stdout_fd) failed. '%s': %s (%d)", op->opaque->exec, pcmk_strerror(rc), rc);

        services_handle_exec_error(op, rc);
        if (!op->synchronous) {
            return operation_finalize(op);
        }
        return FALSE;
    }

    if (pipe(stderr_fd) < 0) {
        rc = errno;

        close(stdout_fd[0]);
        close(stdout_fd[1]);

        crm_err("pipe(stderr_fd) failed. '%s': %s (%d)", op->opaque->exec, pcmk_strerror(rc), rc);

        services_handle_exec_error(op, rc);
        if (!op->synchronous) {
            return operation_finalize(op);
        }
        return FALSE;
    }

    if (safe_str_eq(op->standard, PCMK_RESOURCE_CLASS_STONITH)) {
        if (pipe(stdin_fd) < 0) {
            rc = errno;

            close(stdout_fd[0]);
            close(stdout_fd[1]);
            close(stderr_fd[0]);
            close(stderr_fd[1]);

            crm_err("pipe(stdin_fd) failed. '%s': %s (%d)", op->opaque->exec, pcmk_strerror(rc), rc);

            services_handle_exec_error(op, rc);
            if (!op->synchronous) {
                return operation_finalize(op);
            }
            return FALSE;
        }
    }

    if (op->synchronous) {
#ifdef HAVE_SYS_SIGNALFD_H
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigemptyset(&old_mask);

        if (sigprocmask(SIG_BLOCK, &mask, &old_mask) < 0) {
            crm_perror(LOG_ERR, "sigprocmask() failed to block sigchld");
        }

        pmask = &mask;
#else
        if(pipe(sigchld_pipe) == -1) {
            crm_perror(LOG_ERR, "pipe() failed");
        }

        rc = crm_set_nonblocking(sigchld_pipe[0]);
        if (rc < 0) {
            crm_warn("Could not set pipe input non-blocking: %s " CRM_XS " rc=%d",
                     pcmk_strerror(rc), rc);
        }
        rc = crm_set_nonblocking(sigchld_pipe[1]);
        if (rc < 0) {
            crm_warn("Could not set pipe output non-blocking: %s " CRM_XS " rc=%d",
                     pcmk_strerror(rc), rc);
        }

        sa.sa_handler = sigchld_handler;
        sa.sa_flags = 0;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGCHLD, &sa, &old_sa) < 0) {
            crm_perror(LOG_ERR, "sigaction() failed to set sigchld handler");
        }

        pmask = NULL;
#endif
    }

    op->pid = fork();
    switch (op->pid) {
        case -1:
            rc = errno;

            close(stdout_fd[0]);
            close(stdout_fd[1]);
            close(stderr_fd[0]);
            close(stderr_fd[1]);
            if (stdin_fd[0] >= 0) {
                close(stdin_fd[0]);
                close(stdin_fd[1]);
            }

            crm_err("Could not execute '%s': %s (%d)", op->opaque->exec, pcmk_strerror(rc), rc);
            services_handle_exec_error(op, rc);
            if (!op->synchronous) {
                return operation_finalize(op);
            }

            sigchld_cleanup();
            return FALSE;

        case 0:                /* Child */
            close(stdout_fd[0]);
            close(stderr_fd[0]);
            if (stdin_fd[1] >= 0) {
                close(stdin_fd[1]);
            }
            if (STDOUT_FILENO != stdout_fd[1]) {
                if (dup2(stdout_fd[1], STDOUT_FILENO) != STDOUT_FILENO) {
                    crm_err("dup2() failed (stdout)");
                }
                close(stdout_fd[1]);
            }
            if (STDERR_FILENO != stderr_fd[1]) {
                if (dup2(stderr_fd[1], STDERR_FILENO) != STDERR_FILENO) {
                    crm_err("dup2() failed (stderr)");
                }
                close(stderr_fd[1]);
            }
            if ((stdin_fd[0] >= 0) &&
                (STDIN_FILENO != stdin_fd[0])) {
                if (dup2(stdin_fd[0], STDIN_FILENO) != STDIN_FILENO) {
                    crm_err("dup2() failed (stdin)");
                }
                close(stdin_fd[0]);
            }

            if (op->synchronous) {
                sigchld_cleanup();
            }

            action_launch_child(op);
            CRM_ASSERT(0);  /* action_launch_child is effectively noreturn */
    }

    /* Only the parent reaches here */
    close(stdout_fd[1]);
    close(stderr_fd[1]);
    if (stdin_fd[0] >= 0) {
        close(stdin_fd[0]);
    }

    op->opaque->stdout_fd = stdout_fd[0];
    rc = crm_set_nonblocking(op->opaque->stdout_fd);
    if (rc < 0) {
        crm_warn("Could not set child output non-blocking: %s "
                 CRM_XS " rc=%d",
                 pcmk_strerror(rc), rc);
    }

    op->opaque->stderr_fd = stderr_fd[0];
    rc = crm_set_nonblocking(op->opaque->stderr_fd);
    if (rc < 0) {
        crm_warn("Could not set child error output non-blocking: %s "
                 CRM_XS " rc=%d",
                 pcmk_strerror(rc), rc);
    }

    op->opaque->stdin_fd = stdin_fd[1];
    if (op->opaque->stdin_fd >= 0) {
        // using buffer behind non-blocking-fd here - that could be improved
        // as long as no other standard uses stdin_fd assume stonith
        rc = crm_set_nonblocking(op->opaque->stdin_fd);
        if (rc < 0) {
            crm_warn("Could not set child input non-blocking: %s "
                    CRM_XS " fd=%d,rc=%d",
                    pcmk_strerror(rc), op->opaque->stdin_fd, rc);
        }
        pipe_in_action_stdin_parameters(op);
        // as long as we are handling parameters directly in here just close
        close(op->opaque->stdin_fd);
        op->opaque->stdin_fd = -1;
    }

    // after fds are setup properly and before we plug anything into mainloop
    if (op->opaque->fork_callback) {
        op->opaque->fork_callback(op);
    }

    if (op->synchronous) {
        action_synced_wait(op, pmask);
        sigchld_cleanup();
    } else {

        crm_trace("Async waiting for %d - %s", op->pid, op->opaque->exec);
        mainloop_child_add_with_flags(op->pid,
                                      op->timeout,
                                      op->id,
                                      op,
                                      (op->flags & SVC_ACTION_LEAVE_GROUP) ? mainloop_leave_pid_group : 0,
                                      operation_finished);


        op->opaque->stdout_gsource = mainloop_add_fd(op->id,
                                                     G_PRIORITY_LOW,
                                                     op->opaque->stdout_fd, op, &stdout_callbacks);

        op->opaque->stderr_gsource = mainloop_add_fd(op->id,
                                                     G_PRIORITY_LOW,
                                                     op->opaque->stderr_fd, op, &stderr_callbacks);

        services_add_inflight_op(op);
    }

    return TRUE;
}

GList *
services_os_get_directory_list(const char *root, gboolean files, gboolean executable)
{
    GList *list = NULL;
    struct dirent **namelist;
    int entries = 0, lpc = 0;
    char buffer[PATH_MAX];

    entries = scandir(root, &namelist, NULL, alphasort);
    if (entries <= 0) {
        return list;
    }

    for (lpc = 0; lpc < entries; lpc++) {
        struct stat sb;

        if ('.' == namelist[lpc]->d_name[0]) {
            free(namelist[lpc]);
            continue;
        }

        snprintf(buffer, sizeof(buffer), "%s/%s", root, namelist[lpc]->d_name);

        if (stat(buffer, &sb)) {
            continue;
        }

        if (S_ISDIR(sb.st_mode)) {
            if (files) {
                free(namelist[lpc]);
                continue;
            }

        } else if (S_ISREG(sb.st_mode)) {
            if (files == FALSE) {
                free(namelist[lpc]);
                continue;

            } else if (executable
                       && (sb.st_mode & S_IXUSR) == 0
                       && (sb.st_mode & S_IXGRP) == 0 && (sb.st_mode & S_IXOTH) == 0) {
                free(namelist[lpc]);
                continue;
            }
        }

        list = g_list_append(list, strdup(namelist[lpc]->d_name));

        free(namelist[lpc]);
    }

    free(namelist);
    return list;
}

GList *
resources_os_list_ocf_providers(void)
{
    return get_directory_list(OCF_ROOT_DIR "/resource.d", FALSE, TRUE);
}

GList *
resources_os_list_ocf_agents(const char *provider)
{
    GList *gIter = NULL;
    GList *result = NULL;
    GList *providers = NULL;

    if (provider) {
        char buffer[500];

        snprintf(buffer, sizeof(buffer), "%s/resource.d/%s", OCF_ROOT_DIR, provider);
        return get_directory_list(buffer, TRUE, TRUE);
    }

    providers = resources_os_list_ocf_providers();
    for (gIter = providers; gIter != NULL; gIter = gIter->next) {
        GList *tmp1 = result;
        GList *tmp2 = resources_os_list_ocf_agents(gIter->data);

        if (tmp2) {
            result = g_list_concat(tmp1, tmp2);
        }
    }
    g_list_free_full(providers, free);
    return result;
}

gboolean
services__ocf_agent_exists(const char *provider, const char *agent)
{
    char *buf = NULL;
    gboolean rc = FALSE;
    struct stat st;

    if (provider == NULL || agent == NULL) {
        return rc;
    }

    buf = crm_strdup_printf(OCF_ROOT_DIR "/resource.d/%s/%s", provider, agent);
    if (stat(buf, &st) == 0) {
        rc = TRUE;
    }

    free(buf);
    return rc;
}

#if SUPPORT_NAGIOS
GList *
resources_os_list_nagios_agents(void)
{
    GList *plugin_list = NULL;
    GList *result = NULL;
    GList *gIter = NULL;

    plugin_list = get_directory_list(NAGIOS_PLUGIN_DIR, TRUE, TRUE);

    /* Make sure both the plugin and its metadata exist */
    for (gIter = plugin_list; gIter != NULL; gIter = gIter->next) {
        const char *plugin = gIter->data;
        char *metadata = crm_strdup_printf(NAGIOS_METADATA_DIR "/%s.xml", plugin);
        struct stat st;

        if (stat(metadata, &st) == 0) {
            result = g_list_append(result, strdup(plugin));
        }

        free(metadata);
    }
    g_list_free_full(plugin_list, free);
    return result;
}

gboolean
services__nagios_agent_exists(const char *name)
{
    char *buf = NULL;
    gboolean rc = FALSE;
    struct stat st;

    if (name == NULL) {
        return rc;
    }

    buf = crm_strdup_printf(NAGIOS_PLUGIN_DIR "/%s", name);
    if (stat(buf, &st) == 0) {
        rc = TRUE;
    }

    free(buf);
    return rc;
}
#endif
