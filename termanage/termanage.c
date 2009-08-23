/* File: termanage.c
* Created: 30 Jul, 2009
* License: GNU GPLv3 
* Author: h0rr0rr_drag0n 
* Description: Управляющая программа для АРМ.
* В тестовых целях программа посылает выбранному клиенту два запроса
* данных, команду завершения работы и отключается от сервера.
*/

#include <ctype.h> /* isspace() */
#include <errno.h>
#include <getopt.h> /* getopt_long() */
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdio.h>
#include <stdlib.h> /* atoi() */
#include <string.h> /* memset(), strerror() */
#include <syslog.h>
#include <unistd.h> /* для переменных, специфичных для getopt */
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
 * serv_addr - адрес сервера вида IP:port
 * server_secret_rsa_key - путь к закрытому RSA ключу сервера
 *
 * Пример конфигурационного файла:
 * #simple config file
 * serv_addr 192.168.50.1:26000
 * server_secret_rsa_key /usr/share/rsa/secret.key
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

		if (!strncmp(preadstr, "serv_addr", 9)) {
			preadstr += 10;
			if (parse_addr(preadstr, sets->serv_ip, &sets->serv_port)) {
				fprintf(stderr, "Error parsing string %s\n", readstr);
				return -1;
			}
			continue;
		}
		if (!strncmp(preadstr, "server_secret_rsa_key", 21)) {
			preadstr += 22;
			strncpy(sets->secret_key, preadstr, strlen(preadstr) - 1);
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
	int c;
	struct settings sets;
	char config_path[MAXSTRLEN] = CONFFILE;
	char termip[IP_LENGTH] = "\0";
	unsigned short int termport = 0;
	RSA * serv_privkey = NULL;
	FILE * serv_privkey_file = NULL;

	while (1) {
		int option_index = 0;
		/* доступные опции:
		 * --term-ip x.x.x.x  IP адрес клиента
		 * --term-port ccccc  порт клиента
		 * --config /path/to/config  путь к конфигурационному файлу, взамен
		 *  						умолчального
		 * --help  выводит справку
		 */
		static struct option long_options[] = {
			{"term-ip", 1, 0, 0},
			{"term-port", 1, 0, 0},
			{"config", 1, 0, 0},
			{"help", 0, 0, 0},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "", long_options, &option_index);

		if (c == -1) {
			break;
		}

		/* Получили long_option */
		if (!c) {
			switch (option_index) {
				case 0: /* term-ip */
					memcpy(termip, optarg, strlen(optarg));
					continue;
				case 1: /* term-port */
					termport = atoi(optarg);
					continue;
				case 2: /* config */
					if (optarg) {
						memset(config_path, 0, MAXSTRLEN);
						strncpy(config_path, optarg, strlen(optarg));
					}
					continue;
				case 3: /* help */
					printf("Usage: %s [--term-ip|--term-port|--config|--help]\n\t--term-ip\t terminal IP \
address\n\t--term-port\t terminal port\n\t--config\tpath to config file\n\t--help\tprint this help message\n", argv[0]);
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

	/* Проверяем, указывал ли пользователь IP и порт клиента, которому
	 *нужно отправлять команды
	 */
	if (!strncmp(termip, "\0", 1)) {
		fprintf(stderr, "Please specify terminal IP address: --term-ip\n");
		return 1;
	}
	if (termport == 0) {
		fprintf(stderr, "Please specify terminal port: --term-port\n");
		return 1;
	}

	memset(&sets, 0, sizeof(sets));
	if (readconf(config_path, &sets)) {
		return 1;
	}

	/* Считываем закрытый ключ */
	if ((serv_privkey_file = fopen(sets.secret_key, "r")) == NULL) {
		fprintf(stderr, "Error opening private key file %s: %s\n", sets.secret_key, strerror(errno));
		return -1;
	}
	if ((serv_privkey = PEM_read_RSAPrivateKey(serv_privkey_file, NULL, NULL, NULL)) == NULL) {
		fprintf(stderr, "Error reading RSA public key: %s\n", sets.secret_key);
		return -1;
	}
	if (fclose(serv_privkey_file) == EOF) {
		fprintf(stderr, "Error closing %s: %s\n", sets.secret_key, strerror(errno));
		return -1;
	}

	/* С этого момента, для вывода сообщений об ошибках
	 * пользуемся syslog(), который пишет их в системный лог-файл.
	 */
	openlog(argv[0], LOG_PID, LOG_USER);

	if (net_initial(&sets, termip, termport, serv_privkey)) {
		syslog(LOG_ERR, "Error initial the network\n");
		return -1;
	}

	return 0;
}

