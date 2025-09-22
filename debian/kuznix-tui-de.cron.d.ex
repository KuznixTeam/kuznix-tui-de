#
# Regular cron jobs for the kuznix-tui-de package.
#
0 4	* * *	root	[ -x /usr/bin/kuznix-tui-de_maintenance ] && /usr/bin/kuznix-tui-de_maintenance
