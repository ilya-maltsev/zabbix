/*
** Zabbix
** Copyright (C) 2001-2014 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "common.h"

#include "threads.h"
#include "comms.h"
#include "cfg.h"
#include "log.h"
#include "zbxgetopt.h"

const char	*progname = NULL;
const char	title_message[] = "zabbix_get";
const char	syslog_app_name[] = "zabbix_get";
const char	usage_message[] = "[-hV] -s <host name or IP> [-p <port>] [-I <IP address>] -k <key>";

const char	*help_message[] = {
	"Options:",
	"  -s --host <host name or IP>          Specify host name or IP address of a host",
	"  -p --port <port number>              Specify port number of agent running on the host. Default is " ZBX_DEFAULT_AGENT_PORT_STR,
	"  -I --source-address <IP address>     Specify source IP address",
	"",
	"  -k --key <key of metric>             Specify key of item to retrieve value for",
	"",
	"  -h --help                            Give this help",
	"  -V --version                         Display version number",
	"",
	"Example: zabbix_get -s 127.0.0.1 -p " ZBX_DEFAULT_AGENT_PORT_STR " -k \"system.cpu.load[all,avg1]\"",
	NULL	/* end of text */
};

/* COMMAND LINE OPTIONS */

/* long options */
struct zbx_option	longopts[] =
{
	{"host",		1,	NULL,	's'},
	{"port",		1,	NULL,	'p'},
	{"key",			1,	NULL,	'k'},
	{"source-address",	1,	NULL,	'I'},
	{"help",		0,	NULL,	'h'},
	{"version",		0,	NULL,	'V'},
	{NULL}
};

/* short options */
static char     shortopts[] = "s:p:k:I:hV";

/* end of COMMAND LINE OPTIONS */

#if !defined(_WINDOWS)

/******************************************************************************
 *                                                                            *
 * Function: get_signal_handler                                               *
 *                                                                            *
 * Purpose: process signals                                                   *
 *                                                                            *
 * Parameters: sig - signal ID                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
static void	get_signal_handler(int sig)
{
	if (SIGALRM == sig)
		zbx_error("Timeout while executing operation");

	exit(EXIT_FAILURE);
}

#endif /* not WINDOWS */

/******************************************************************************
 *                                                                            *
 * Function: get_value                                                        *
 *                                                                            *
 * Purpose: connect to Zabbix agent, receive and print value                  *
 *                                                                            *
 * Parameters: host - server name or IP address                               *
 *             port - port number                                             *
 *             key  - item's key                                              *
 *                                                                            *
 ******************************************************************************/
static void	get_value(const char *source_ip, const char *host, unsigned short port, const char *key)
{
	zbx_sock_t	s;
	int		ret;
	char		request[1024];

	if (SUCCEED == (ret = zbx_tcp_connect(&s, source_ip, host, port, GET_SENDER_TIMEOUT)))
	{
		zbx_snprintf(request, sizeof(request), "%s\n", key);

		if (SUCCEED == (ret = zbx_tcp_send(&s, request)))
		{
			if (SUCCEED == (ret = SUCCEED_OR_FAIL(zbx_tcp_recv_ext(&s, ZBX_TCP_READ_UNTIL_CLOSE, 0))))
			{
				if (0 == strcmp(s.buffer, ZBX_NOTSUPPORTED) && sizeof(ZBX_NOTSUPPORTED) < s.read_bytes)
				{
					zbx_rtrim(s.buffer + sizeof(ZBX_NOTSUPPORTED), "\r\n");
					printf("%s: %s\n", s.buffer, s.buffer + sizeof(ZBX_NOTSUPPORTED));
				}
				else
				{
					zbx_rtrim(s.buffer, "\r\n");
					printf("%s\n", s.buffer);
				}
			}
		}

		zbx_tcp_close(&s);
	}

	if (FAIL == ret)
		zbx_error("Get value error: %s", zbx_tcp_strerror());
}

/******************************************************************************
 *                                                                            *
 * Function: main                                                             *
 *                                                                            *
 * Purpose: main function                                                     *
 *                                                                            *
 * Parameters:                                                                *
 *                                                                            *
 * Return value:                                                              *
 *                                                                            *
 * Comments:                                                                  *
 *                                                                            *
 ******************************************************************************/
int	main(int argc, char **argv)
{
	unsigned short	port = ZBX_DEFAULT_AGENT_PORT;
	int		ret = SUCCEED, opt_k = 0, opt_p = 0, opt_s = 0, opt_i = 0;
	char		*host = NULL, *key = NULL, *source_ip = NULL, ch;

	progname = get_program_name(argv[0]);

	/* parse the command-line */
	while ((char)EOF != (ch = (char)zbx_getopt_long(argc, argv, shortopts, longopts, NULL)))
	{
		switch (ch)
		{
			case 'k':
				opt_k++;

				if (NULL == key)
					key = strdup(zbx_optarg);
				break;
			case 'p':
				opt_p++;
				port = (unsigned short)atoi(zbx_optarg);
				break;
			case 's':
				opt_s++;

				if (NULL == host)
					host = strdup(zbx_optarg);
				break;
			case 'I':
				opt_i++;

				if (NULL == source_ip)
					source_ip = strdup(zbx_optarg);
				break;
			case 'h':
				help();
				exit(EXIT_SUCCESS);
				break;
			case 'V':
				version();
				exit(EXIT_SUCCESS);
				break;
			default:
				usage();
				exit(EXIT_FAILURE);
				break;
		}
	}

	if (NULL == host || NULL == key)
	{
		usage();
		ret = FAIL;
	}

	/* every option may be specified only once */
	if (1 < opt_k || 1 < opt_p || 1 < opt_s || 1 < opt_i)
	{
		if (1 < opt_k)
			zbx_error("option \"-k\" specified multiple times");
		if (1 < opt_p)
			zbx_error("option \"-p\" specified multiple times");
		if (1 < opt_s)
			zbx_error("option \"-s\" specified multiple times");
		if (1 < opt_i)
			zbx_error("option \"-I\" specified multiple times");

		ret = FAIL;
	}

	/* Parameters which are not option values are not allowed. The check relies on zbx_getopt_internal() which */
	/* always permutes command line arguments regardless of POSIXLY_CORRECT environment variable. */
	if (argc > zbx_optind)
	{
		int	i;

		for (i = zbx_optind; i < argc; i++)
			zbx_error("invalid parameter \"%s\"", argv[i]);

		ret = FAIL;
	}

	if (SUCCEED == ret)
	{
#if !defined(_WINDOWS)
		signal(SIGINT,  get_signal_handler);
		signal(SIGTERM, get_signal_handler);
		signal(SIGQUIT, get_signal_handler);
		signal(SIGALRM, get_signal_handler);
#endif
		get_value(source_ip, host, port, key);
	}

	zbx_free(host);
	zbx_free(key);

	return ret;
}
