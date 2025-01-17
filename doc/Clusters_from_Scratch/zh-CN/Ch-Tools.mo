��    
      l      �       �   ,  �       |   /    �  �   �  �   �  ;   ~  �  �     v    �  ,  �    �  d   �%  �   A&  �   �&  �   �'  3   }(  I  �(     �)              
                     	          
[root@pcmk-1 ~]# <userinput>crm --help</userinput>

usage:
    crm [-D display_type]
    crm [-D display_type] args
    crm [-D display_type] [-f file]

    Use crm without arguments for an interactive session.
    Supply one or more arguments for a "single-shot" use.
    Specify with -f a file which contains a script. Use '-' for
    standard input or use pipe/redirection.

    crm displays cli format configurations using a color scheme
    and/or in uppercase. Pick one of "color" or "uppercase", or
    use "-D color,uppercase" if you want colorful uppercase.
    Get plain output by "-D plain". The default may be set in
    user preferences (options).

Examples:

    # crm -f stopapp2.cli
    # crm &lt; stopapp2.cli
    # crm resource stop global_www
    # crm status
 
[root@pcmk-1 ~]# <userinput>crm_mon --version</userinput>
crm_mon 1.0.5 for OpenAIS and Heartbeat (Build: 462f1569a43740667daf7b0f6b521742e9eb8fa7)

Written by Andrew Beekhof
[root@pcmk-1 ~]# <userinput>crm_mon --help</userinput>
crm_mon - Provides a summary of cluster's current state.

Outputs varying levels of detail in a number of different formats.

Usage: crm_mon mode [options]
Options:
 -?, --help                 This text
 -$, --version              Version information
 -V, --verbose              Increase debug output

Modes:
 -h, --as-html=value        Write cluster status to the named file
 -w, --web-cgi              Web mode with output suitable for cgi
 -s, --simple-status        Display the cluster status once as a simple one line output (suitable for nagios)
 -S, --snmp-traps=value     Send SNMP traps to this station
 -T, --mail-to=value        Send Mail alerts to this user.  See also --mail-from, --mail-host, --mail-prefix

Display Options:
 -n, --group-by-node        Group resources by node
 -r, --inactive             Display inactive resources
 -f, --failcounts           Display resource fail counts
 -o, --operations           Display resource operation history
 -t, --timing-details       Display resource operation history with timing details


Additional Options:
 -i, --interval=value           Update frequency in seconds
 -1, --one-shot                 Display the cluster status once on the console and exit
 -N, --disable-ncurses          Disable the use of ncurses
 -d, --daemonize                Run in the background as a daemon
 -p, --pid-file=value           (Advanced) Daemon pid file location
 -F, --mail-from=value          Mail alerts should come from the named user
 -H, --mail-host=value          Mail alerts should be sent via the named host
 -P, --mail-prefix=value        Subjects for mail alerts should start with this string
 -E, --external-agent=value     A program to run when resource operations take place.
 -e, --external-recipient=value A recipient for your program (assuming you want the program to send something to someone).

Examples:

Display the cluster´s status on the console with updates as they occur:
        # crm_mon

Display the cluster´s status on the console just once then exit:
        # crm_mon

Display your cluster´s status, group resources by node, and include inactive resources in the list:
        # crm_mon --group-by-node --inactive

Start crm_mon as a background daemon and have it write the cluster´s status to an HTML file:
        # crm_mon --daemonize --as-html /path/to/docroot/filename.html

Start crm_mon as a background daemon and have it send email alerts:
        # crm_mon --daemonize --mail-to user@example.com --mail-host mail.example.com

Start crm_mon as a background daemon and have it send SNMP alerts:
        # crm_mon --daemonize --snmp-traps snmptrapd.example.com

Report bugs to pacemaker@oss.clusterlabs.org
 Additionally, the Pacemaker version and supported cluster stack(s) is available via the <command>--version</command> option. If the SNMP and/or email options are not listed, then Pacemaker was not built to support them. This may be by the choice of your distribution or the required libraries may not have been available. Please contact whoever supplied you with the packages for more details. In the dark past, configuring Pacemaker required the administrator to read and write XML. In true UNIX style, there were also a number of different commands that specialized in different aspects of querying and updating the cluster. Since Pacemaker 1.0, this has all changed and we have an integrated, scriptable, cluster shell that hides all the messy XML scaffolding. It even allows you to queue up several changes at once and commit them atomically. Take some time to familiarize yourself with what it can do. The primary tool for monitoring the status of the cluster is crm_mon (also available as crm status). It can be run in a variety of modes and has a number of output options. To find out about any of the tools that come with Pacemaker, simply invoke them with the <command>--help</command> option or consult the included man pages. Both sets of output are created from the tool, and so will always be in sync with each other and the tool itself. Using Pacemaker Tools Project-Id-Version: 0
POT-Creation-Date: 2010-12-15T23:32:37
PO-Revision-Date: 2010-12-15 23:43+0800
Last-Translator: Charlie Chen <laneovcc@gmail.com>
Language-Team: None
Language: 
MIME-Version: 1.0
Content-Type: text/plain; charset=UTF-8
Content-Transfer-Encoding: 8bit
 
[root@pcmk-1 ~]# <userinput>crm --help</userinput>

usage:
    crm [-D display_type]
    crm [-D display_type] args
    crm [-D display_type] [-f file]

    Use crm without arguments for an interactive session.
    Supply one or more arguments for a "single-shot" use.
    Specify with -f a file which contains a script. Use '-' for
    standard input or use pipe/redirection.

    crm displays cli format configurations using a color scheme
    and/or in uppercase. Pick one of "color" or "uppercase", or
    use "-D color,uppercase" if you want colorful uppercase.
    Get plain output by "-D plain". The default may be set in
    user preferences (options).

Examples:

    # crm -f stopapp2.cli
    # crm &lt; stopapp2.cli
    # crm resource stop global_www
    # crm status
 
[root@pcmk-1 ~]# <userinput>crm_mon --version</userinput>
crm_mon 1.0.5 for OpenAIS and Heartbeat (Build: 462f1569a43740667daf7b0f6b521742e9eb8fa7)

Written by Andrew Beekhof
[root@pcmk-1 ~]# <userinput>crm_mon --help</userinput>
crm_mon - Provides a summary of cluster's current state.

Outputs varying levels of detail in a number of different formats.

Usage: crm_mon mode [options]
Options:
 -?, --help                 This text
 -$, --version              Version information
 -V, --verbose              Increase debug output

Modes:
 -h, --as-html=value        Write cluster status to the named file
 -w, --web-cgi              Web mode with output suitable for cgi
 -s, --simple-status        Display the cluster status once as a simple one line output (suitable for nagios)
 -S, --snmp-traps=value     Send SNMP traps to this station
 -T, --mail-to=value        Send Mail alerts to this user.  See also --mail-from, --mail-host, --mail-prefix

Display Options:
 -n, --group-by-node        Group resources by node
 -r, --inactive             Display inactive resources
 -f, --failcounts           Display resource fail counts
 -o, --operations           Display resource operation history
 -t, --timing-details       Display resource operation history with timing details


Additional Options:
 -i, --interval=value           Update frequency in seconds
 -1, --one-shot                 Display the cluster status once on the console and exit
 -N, --disable-ncurses          Disable the use of ncurses
 -d, --daemonize                Run in the background as a daemon
 -p, --pid-file=value           (Advanced) Daemon pid file location
 -F, --mail-from=value          Mail alerts should come from the named user
 -H, --mail-host=value          Mail alerts should be sent via the named host
 -P, --mail-prefix=value        Subjects for mail alerts should start with this string
 -E, --external-agent=value     A program to run when resource operations take place.
 -e, --external-recipient=value A recipient for your program (assuming you want the program to send something to someone).

Examples:

Display the cluster´s status on the console with updates as they occur:
        # crm_mon

Display the cluster´s status on the console just once then exit:
        # crm_mon

Display your cluster´s status, group resources by node, and include inactive resources in the list:
        # crm_mon --group-by-node --inactive

Start crm_mon as a background daemon and have it write the cluster´s status to an HTML file:
        # crm_mon --daemonize --as-html /path/to/docroot/filename.html

Start crm_mon as a background daemon and have it send email alerts:
        # crm_mon --daemonize --mail-to user@example.com --mail-host mail.example.com

Start crm_mon as a background daemon and have it send SNMP alerts:
        # crm_mon --daemonize --snmp-traps snmptrapd.example.com

Report bugs to pacemaker@oss.clusterlabs.org
 此外，Pacemaker的版本和支持的stack(本文中是corosync)可以通过 --version选项看到 如果SNMP或者email选项没有出现在选项中，说明pacemaker编译的时候没有打开对他们的支持，你需要联系提供这个发行版本的人，或者自己编译。 在万恶的旧社会，配置Pacemaker需要管理员具备读写XML的能力。 根据UNIX精神，也有许多不同的查询和配置集群的命令。 自从Pacemaker 1.0，这一切都改变了，我们有了一个集成的脚本化的集群控制shell,它把麻烦的XML配置隐藏了起来。它甚至允许你一次做出许多修改并自动提交(并检测是否合法)。 让我们花点时间熟悉一下它能做什么。 监控集群状态的主要命令是 crm_mon(跟crm status是一样的效果)。它可以运行在很多模式下并且有许多输出选项。如果要查看Pacemaker相应的工具，可以通过--help或者man pages来查看。这些输出都是靠命令来生成的，所以它总是会在各个节点和工具之间同步。 使用Pacemaker工具 