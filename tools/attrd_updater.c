/*
 * Copyright 2004-2019 the Pacemaker project contributors
 *
 * The version control history for this file may have further details.
 *
 * This source code is licensed under the GNU General Public License version 2
 * or later (GPLv2+) WITHOUT ANY WARRANTY.
 */

#include <crm_internal.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>

#include <sys/param.h>
#include <sys/types.h>

#include <crm/crm.h>
#include <crm/msg_xml.h>
#include <crm/common/ipc.h>

#include <crm/attrd.h>

/* *INDENT-OFF* */
static struct crm_option long_options[] = {
    /* Top-level Options */
    {"help",    0, 0, '?', "\tThis text"},
    {"version", 0, 0, '$', "\tVersion information"  },
    {"verbose", 0, 0, 'V', "\tIncrease debug output\n"},

    {"name",    1, 0, 'n', "The attribute's name"},

    {"-spacer-",1, 0, '-', "\nCommands:"},
    {"update",  1, 0, 'U', "Update the attribute's value in pacemaker-attrd. If this causes the value to change, it will also be updated in the cluster configuration"},
    {"update-both", 1, 0, 'B', "Update the attribute's value and time to wait (dampening) in pacemaker-attrd. If this causes the value or dampening to change, the attribute will also be written to the cluster configuration, so be aware that repeatedly changing the dampening reduces its effectiveness."},
    {"update-delay", 0, 0, 'Y', "Update the attribute's dampening in pacemaker-attrd (requires -d/--delay). If this causes the dampening to change, the attribute will also be written to the cluster configuration, so be aware that repeatedly changing the dampening reduces its effectiveness."},
    {"query",   0, 0, 'Q', "\tQuery the attribute's value from pacemaker-attrd"},
    {"delete",  0, 0, 'D', "\tDelete the attribute in pacemaker-attrd.  If a value was previously set, it will also be removed from the cluster configuration"},
    {"refresh", 0, 0, 'R', "\t(Advanced) Force the pacemaker-attrd daemon to resend all current values to the CIB\n"},

    {"-spacer-",1, 0, '-', "\nAdditional options:"},
    {"delay",   1, 0, 'd', "The time to wait (dampening) in seconds for further changes before writing"},
    {"set",     1, 0, 's', "(Advanced) The attribute set in which to place the value"},
    {"node",    1, 0, 'N', "Set the attribute for the named node (instead of the local one)"},
    {"all",     0, 0, 'A', "Show values of the attribute for all nodes (query only)"},
    /* lifetime could be implemented if there is sufficient user demand */
    {"lifetime",1, 0, 'l', "(Deprecated) Lifetime of the node attribute (silently ignored by cluster)"},
    {"private", 0, 0, 'p', "\tIf this creates a new attribute, never write the attribute to the CIB"},

    /* Legacy options */
    {"quiet",   0, 0, 'q', NULL, pcmk_option_hidden},
    {"update",  1, 0, 'v', NULL, pcmk_option_hidden},
    {"section", 1, 0, 'S', NULL, pcmk_option_hidden},
    {0, 0, 0, 0}
};
/* *INDENT-ON* */

static int do_query(const char *attr_name, const char *attr_node, gboolean query_all);
static int do_update(char command, const char *attr_node, const char *attr_name,
                     const char *attr_value, const char *attr_section,
                     const char *attr_set, const char *attr_dampen, int attr_options);

// Free memory at exit to make analyzers happy
#define cleanup_memory() \
    free(attr_dampen); \
    free(attr_name); \
    free(attr_node); \
    free(attr_section); \
    free(attr_set);

#define set_option(option_var) \
    if (option_var) { \
        free(option_var); \
    } \
    option_var = strdup(optarg);

int
main(int argc, char **argv)
{
    int index = 0;
    int argerr = 0;
    int attr_options = attrd_opt_none;
    int flag;
    crm_exit_t exit_code = CRM_EX_OK;
    char *attr_node = NULL;
    char *attr_name = NULL;
    char *attr_set = NULL;
    char *attr_section = NULL;
    char *attr_dampen = NULL;
    const char *attr_value = NULL;
    char command = 'Q';

    gboolean query_all = FALSE;

    crm_log_cli_init("attrd_updater");
    crm_set_options(NULL, "command -n attribute [options]", long_options,
                    "Tool for updating cluster node attributes");

    if (argc < 2) {
        crm_help('?', CRM_EX_USAGE);
    }

    while (1) {
        flag = crm_get_option(argc, argv, &index);
        if (flag == -1)
            break;

        switch (flag) {
            case 'V':
                crm_bump_log_level(argc, argv);
                break;
            case '?':
            case '$':
                cleanup_memory();
                crm_help(flag, CRM_EX_OK);
                break;
            case 'n':
                set_option(attr_name);
                break;
            case 's':
                set_option(attr_set);
                break;
            case 'd':
                set_option(attr_dampen);
                break;
            case 'l':
            case 'S':
                set_option(attr_section);
                break;
            case 'N':
                set_option(attr_node);
                break;
            case 'A':
                query_all = TRUE;
                break;
            case 'p':
                set_bit(attr_options, attrd_opt_private);
                break;
            case 'q':
                break;
            case 'Y':
                command = flag;
                crm_log_args(argc, argv); /* Too much? */
                break;
            case 'Q':
            case 'B':
            case 'R':
            case 'D':
            case 'U':
            case 'v':
                command = flag;
                attr_value = optarg;
                crm_log_args(argc, argv); /* Too much? */
                break;
            default:
                ++argerr;
                break;
        }
    }

    if (optind > argc) {
        ++argerr;
    }

    if (command != 'R' && attr_name == NULL) {
        ++argerr;
    }

    if (argerr) {
        cleanup_memory();
        crm_help('?', CRM_EX_USAGE);
    }

    if (command == 'Q') {
        exit_code = crm_errno2exit(do_query(attr_name, attr_node, query_all));
    } else {
        /* @TODO We don't know whether the specified node is a Pacemaker Remote
         * node or not, so we can't set attrd_opt_remote when appropriate.
         * However, it's not a big problem, because pacemaker-attrd will learn
         * and remember a node's "remoteness".
         */
        exit_code = crm_errno2exit(do_update(command,
                                   attrd_get_target(attr_node), attr_name,
                                   attr_value, attr_section, attr_set,
                                   attr_dampen, attr_options));
    }

    cleanup_memory();
    crm_exit(exit_code);
}

/*!
 * \internal
 * \brief Submit a query request to pacemaker-attrd and wait for reply
 *
 * \param[in] name    Name of attribute to query
 * \param[in] host    Query applies to this host only (or all hosts if NULL)
 * \param[out] reply  On success, will be set to new XML tree with reply
 *
 * \return pcmk_ok on success, -errno on error
 * \note On success, caller is responsible for freeing result via free_xml(*reply)
 */
static int
send_attrd_query(const char *name, const char *host, xmlNode **reply)
{
    int rc;
    crm_ipc_t *ipc;
    xmlNode *query;

    /* Build the query XML */
    query = create_xml_node(NULL, __FUNCTION__);
    if (query == NULL) {
        return -ENOMEM;
    }
    crm_xml_add(query, F_TYPE, T_ATTRD);
    crm_xml_add(query, F_ORIG, crm_system_name);
    crm_xml_add(query, F_ATTRD_HOST, host);
    crm_xml_add(query, F_ATTRD_TASK, ATTRD_OP_QUERY);
    crm_xml_add(query, F_ATTRD_ATTRIBUTE, name);

    /* Connect to pacemaker-attrd, send query XML and get reply */
    crm_debug("Sending query for value of %s on %s", name, (host? host : "all nodes"));
    ipc = crm_ipc_new(T_ATTRD, 0);
    if (crm_ipc_connect(ipc) == FALSE) {
        crm_perror(LOG_ERR, "Connection to cluster attribute manager failed");
        rc = -ENOTCONN;
    } else {
        rc = crm_ipc_send(ipc, query, crm_ipc_flags_none|crm_ipc_client_response, 0, reply);
        if (rc > 0) {
            rc = pcmk_ok;
        }
        crm_ipc_close(ipc);
    }

    free_xml(query);
    return(rc);
}

/*!
 * \brief Validate pacemaker-attrd's XML reply to an query
 *
 * param[in] reply      Root of reply XML tree to validate
 * param[in] attr_name  Name of attribute that was queried
 *
 * \return pcmk_ok on success,
 *         -errno on error (-ENXIO = requested attribute does not exist)
 */
static int
validate_attrd_reply(xmlNode *reply, const char *attr_name)
{
    const char *reply_attr;

    if (reply == NULL) {
        fprintf(stderr, "Could not query value of %s: reply did not contain valid XML\n",
                attr_name);
        return -pcmk_err_schema_validation;
    }
    crm_log_xml_trace(reply, "Reply");

    reply_attr = crm_element_value(reply, F_ATTRD_ATTRIBUTE);
    if (reply_attr == NULL) {
        fprintf(stderr, "Could not query value of %s: attribute does not exist\n",
                attr_name);
        return -ENXIO;
    }

    if (safe_str_neq(crm_element_value(reply, F_TYPE), T_ATTRD)
        || (crm_element_value(reply, F_ATTRD_VERSION) == NULL)
        || strcmp(reply_attr, attr_name)) {
            fprintf(stderr,
                    "Could not query value of %s: reply did not contain expected identification\n",
                    attr_name);
            return -pcmk_err_schema_validation;
    }
    return pcmk_ok;
}

/*!
 * \brief Print the attribute values in a pacemaker-attrd XML query reply
 *
 * \param[in] reply     Root of XML tree with query reply
 * \param[in] attr_name Name of attribute that was queried
 *
 * \return TRUE if any values were printed
 */
static gboolean
print_attrd_values(xmlNode *reply, const char *attr_name)
{
    xmlNode *child;
    const char *reply_host, *reply_value;
    gboolean have_values = FALSE;

    /* Iterate through reply's XML tags (a node tag for each host-value pair) */
    for (child = __xml_first_child(reply); child != NULL; child = __xml_next(child)) {
        if (safe_str_neq((const char*)child->name, XML_CIB_TAG_NODE)) {
            crm_warn("Ignoring unexpected %s tag in query reply", child->name);
        } else {
            reply_host = crm_element_value(child, F_ATTRD_HOST);
            reply_value = crm_element_value(child, F_ATTRD_VALUE);

            if (reply_host == NULL) {
                crm_warn("Ignoring %s tag without %s attribute in query reply",
                         XML_CIB_TAG_NODE, F_ATTRD_HOST);
            } else {
                printf("name=\"%s\" host=\"%s\" value=\"%s\"\n",
                       attr_name, reply_host, (reply_value? reply_value : ""));
                have_values = TRUE;
            }
        }
    }
    return have_values;
}

/*!
 * \brief Submit a query to pacemaker-attrd and print reply
 *
 * \param[in] attr_name  Name of attribute to be affected by request
 * \param[in] attr_node  Name of host to query for (or NULL for localhost)
 * \param[in] query_all  If TRUE, ignore attr_node and query all nodes instead
 *
 * \return pcmk_ok on success, -errno on error
 */
static int
do_query(const char *attr_name, const char *attr_node, gboolean query_all)
{
    xmlNode *reply = NULL;
    int rc;

    /* Decide which node(s) to query */
    if (query_all == TRUE) {
        attr_node = NULL;
    } else {
        attr_node = attrd_get_target(attr_node);
    }

    /* Build and send pacemaker-attrd request, and get XML reply */
    rc = send_attrd_query(attr_name, attr_node, &reply);
    if (rc != pcmk_ok) {
        fprintf(stderr, "Could not query value of %s: %s (%d)\n", attr_name, pcmk_strerror(rc), rc);
        return rc;
    }

    /* Validate the XML reply */
    rc = validate_attrd_reply(reply, attr_name);
    if (rc != pcmk_ok) {
        if (reply != NULL) {
            free_xml(reply);
        }
        return rc;
    }

    /* Print the values from the reply */
    if (print_attrd_values(reply, attr_name) == FALSE) {
        fprintf(stderr,
                "Could not query value of %s: reply had attribute name but no host values\n",
                attr_name);
        free_xml(reply);
        return -pcmk_err_schema_validation;
    }

    return pcmk_ok;
}

static int
do_update(char command, const char *attr_node, const char *attr_name,
          const char *attr_value, const char *attr_section,
          const char *attr_set, const char *attr_dampen, int attr_options)
{
    int rc = attrd_update_delegate(NULL, command, attr_node, attr_name,
                                   attr_value, attr_section, attr_set,
                                   attr_dampen, NULL, attr_options);
    if (rc != pcmk_ok) {
        fprintf(stderr, "Could not update %s=%s: %s (%d)\n", attr_name, attr_value, pcmk_strerror(rc), rc);
    }
    return rc;
}
