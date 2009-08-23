/* File: defines.h
* Created: 25 Jul, 2009
* License: GNU GPLv3 
* Author: h0rr0rr_drag0n 
* Description: несколько полезных
* объявлений.
*/

#ifndef _DEFINES_H_
#define _DEFINES_H_

#define IP_LENGTH 15 /* максимальная длина IP адреса */
#define MAXPATHLEN 512 /* Максимальная длина пути */
#define MAXSTRLEN 256 /* Максимальная длина строки */
#define CONFFILE "/etc/termanage" /* путь к конфигурационному файлу */
#define TIME 5 /* столько секунд ждем прихода команды */
#define NRETRY 3 /* количество попыток чтения команды */
#define MAXBUFFSIZE 128 /* Максимальное количество элементов в буфере команд для отправки */
#define INCOMING_PCKT_SIZE 17 /* Максимальный размер пакета, который мы можем принять, без выделения дополнительной памяти */
#define SERV_PACKET_SIZE 15 /* Размер сервисного пакета */
#define MANAG_PACKET_SIZE 15 /* Размер управляющего пакета */

/* Настройки, читаемые из конфигурационного файла */
struct settings {
	char serv_ip[IP_LENGTH];
	unsigned short int serv_port;
	char secret_key[MAXPATHLEN]; /* путь к закрытому RSA ключу */
};

#ifdef _SOCKADDR2PROTOFORM_

#include <arpa/inet.h> /* inet_ntop() */
#include <ctype.h> /* isalnum() */
#include <netinet/in.h> /* sockaddr_in */
#include <stdlib.h> /* atoi(), malloc() */
#include <string.h> /* memcpy() */

/* Функция переводит IP и порт из структуры sockaddr_in в 
 * форму, описанную в протоколе
 */
static void * sockaddr2protoform(struct sockaddr_in * addr) {
	char ip_addr[IP_LENGTH] = "\0";
	char * pip_addr = NULL;
	char octnum[3] = "\0";
	int i = 0, j = 0;
	unsigned short int port = addr->sin_port; /* уже в сетевом порядке байтов */
	unsigned char octet[4] = {0, 0, 0, 0};
	unsigned char * protoform = NULL;
	unsigned char * pprotoform = NULL;

	if (inet_ntop(AF_INET, &addr->sin_addr, ip_addr, IP_LENGTH) == NULL) {
		return NULL;
	}

	pip_addr = ip_addr;
	for (; i < 4; ++i) {
		j = 0;
		memset(octnum, 0, 3);
		while (isalnum(*pip_addr)) {
			octnum[j] = *pip_addr;
			pip_addr++;
			j++;
		}
		pip_addr++;
		octet[i] = atoi(octnum);
	}

	if ((protoform = (unsigned char *)malloc(6)) == NULL) {
		return NULL;
	}
	pprotoform = protoform;
	for (i = 0; i < 4; ++i) {
		*pprotoform = octet[i];
		pprotoform++;
	}
	memcpy(pprotoform, &port, sizeof(port));

	return protoform;
}

#endif /* _SOCKADDR2PROTOFORM_ */

#endif /* _DEFINES_H_ */

