/* File: termserv.c
* Created: 02 Aug, 2009
* License: GNU GPLv3 
* Author: h0rr0rr_drag0n 
* Description: Сервер, обеспечивающий совместную работу терминалов
* и АРМ.
*/

#include <ctype.h> /* isdigit() */
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h> /* atoi() */
#include <string.h>
#include <syslog.h>
#include <unistd.h> /* daemon() */
#include "defines.h"
#include "net.h"

/* Функция разбирает строку вида "192.168.50.1:26001" на строку - IP адрес 
 * и число - порт.
 */
int parse_addr(char * string, char * ip_addr, unsigned short int * port) {
	while ((isdigit(*string)) || (*string == '.')) {
		*ip_addr = *string;
		string++;
		ip_addr++;
	}
	if (*string != ':') {
		return -1;
	}
	string++;
	*port = (unsigned short int)atoi(string);

	return 0;
}

/* Функция читает конфигурационный файл и возвращает ноль,
 * если все успешно или число меньше нуля если произошла ошибка.
 * Код ошибки возвращается в errno.
 *
 * Формат конфигурационного файла:
 * '#' - начало комментария, комментарии должны начинаться с новой строки
 * aws_cli - адрес для подключения АРМ, вида IP:port
 * terminal_cli - адрес для подключения терминалов, вида IP:port
 *
 * Пример конфигурационного файла:
 * #simple config file
 * aws_cli 192.168.50.1:26000
 * terminal_cli 192.168.50.1:26001
 *
 */
int readconf(char * path, struct settings * sets) {
	FILE * conffd = NULL;
	char readstr[MAXSTRLEN] = "\0";
	char * preadstr = NULL;

	if ((conffd = fopen(path, "r")) == NULL) {
		fprintf(stderr, "Error opening config file %s: %s\n",
				path, strerror(errno));
		return -1;
	}

	while (fgets(readstr, MAXSTRLEN, conffd) != NULL) {
		preadstr = readstr;
		/* Убираем пробелы в начале строки */
		while (isspace(*preadstr)) {
			preadstr++;
		}
		/* Пропускаем пустые строки и комментарии */
		if ((*preadstr == '\n') || (*preadstr == '\0') || (*preadstr == '#')) {
			continue;
		}

		if (!strncmp(preadstr, "aws_cli", 7)) {
			preadstr += 8;
			if (parse_addr(preadstr, sets->aws_ip, &sets->aws_port)) {
				fprintf(stderr, "Error parsing string %s\n", readstr);
				return -1;
			}
			continue;
		}
		if (!strncmp(preadstr, "terminal_cli", 12)) {
			preadstr += 13;
			if (parse_addr(preadstr, sets->terminal_ip, &sets->terminal_port)) {
				fprintf(stderr, "Error parsing string %s\n", readstr);
				return -1;
			}
			continue;
		} else {
			fprintf(stderr, "Undefined string in %s: ?? %s ??\n", path, readstr);
			return -1;
		}
	} /* while (fgets(readstr, MAXSTRLEN, conffd) != NULL) */

	if (fclose(conffd) != 0) {
		fprintf(stderr, "Error closing file %s: %s\n", path, strerror(errno));
		return -1;
	}

	return 0;
}

int main(int argc, char * argv[]) {
	char conf_path[MAXPATHLEN] = CONFFILE;
	int c;
	struct settings sets;

	while (1) {
		int option_index = 0;
		/* доступные опции:
		 * --config /path/to/config  путь к конфигурационному файлу взамен умолчального
		 * --help  выводит справку
		 */
		static struct option long_options[] = {
			{"config", 1, 0, 0},
			{"help", 0, 0, 0},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "", long_options, &option_index);

		if (c == -1) {
			break;
		}

		/* получили long option */
		if (!c) {
			switch (option_index) {
				case 0: /* config */
					if (optarg) {
						memset(conf_path, 0, MAXPATHLEN);
						strncpy(conf_path, optarg, strlen(optarg));
					}
					continue;
				case 1: /* help */
					printf("Usage: %s [--config|--help]\n\t--config\tpath to config file\n\t--help\t\
print this help message\n", argv[0]);
					return 0;
				default:
					return 1;
			}
		}
		switch (c) {
			case '?':
				break;
			default:
				printf("?? getopt returned character code %d ??\n", c);
				return 1;
		}
	} /* while (1) */

	memset(&sets, 0, sizeof(sets));
	if (readconf(conf_path, &sets)) {
		return 1;
	}
#ifdef DEBUG
	printf("AWS ip and port: %s:%d\nTerminal ip and port: %s:%d\n",
			sets.aws_ip, sets.aws_port,
			sets.terminal_ip, sets.terminal_port);
#endif /* DEBUG */

	/* С этого момента, для вывода сообщений об ошибках
	 * пользуемся syslog(), который пишет их в системный лог-файл.
	 */
	openlog(argv[0], LOG_PID, LOG_DAEMON);

#ifndef DEBUG
	if (daemon(0, 1) == -1) {
		syslog(LOG_ERR, "Error starting daemon: %s\n", strerror(errno));
		return -1;
	}
#endif /* DEBUG */

	if (net_initial(&sets)) {
		syslog(LOG_ERR, "Error initial the network\n");
		return -1;
	}

	return 0;
}

