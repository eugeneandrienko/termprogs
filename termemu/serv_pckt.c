/* File: serv_pckt.c
* Created: 27 Jul, 2009
* License: GNU GPLv3 
* Author: h0rr0rr_drag0n 
* Description: Функции, формирующие сервисный пакет.
*/

#include <netinet/in.h> /* sockaddr_in */
#include <openssl/pem.h> /* net.h */
#include <openssl/rsa.h> /* net.h */
#include <stdlib.h> /* malloc() */
#include <string.h> /* memcpy */
#include <syslog.h> /* syslog() */
#define _SOCKADDR2PROTOFORM_ /* to use curr func */
#include "defines.h"
#include "net.h"
#include "serv_pckt.h"

/* Функция формирует сервисный пакет и помещает его в
 * буфер пакетов, ожидающих отправки.
 * Возвращает ноль если все нормально и не ноль если
 * что-то не в порядке.
 */
int create_serv_pckt(unsigned char error_code, void * rcvr_addr, struct sockaddr_in * host_addr) {
	void * serv_pckt = NULL;
	unsigned char * pserv = NULL;
	void * sender = NULL;

	if ((serv_pckt = malloc(SERV_PACKET_SIZE)) == NULL) {
		syslog(LOG_ERR, "Error allocating memory for service packet\n");
		return -1;
	}

	pserv = serv_pckt;
	*pserv = 0xff; /* устанавливаем байт типа пакета */
	pserv++;
	/* устанавливаем адрес получателя - равен адресу отправителя пакета, вызвавшего
	 * посылку сервисного пакета.
	 */
	memcpy(pserv, rcvr_addr, 6);
	pserv += 6; 
	/* устанавливаем адрес отправителя - равен адресу
	 * текущего соединения
	 */
	if ((sender = sockaddr2protoform(host_addr)) == NULL) {
		syslog(LOG_ERR, "Error converting IP address from struct sockaddr_in to protocol form\n");
		return -1;
	}
	memcpy(pserv, sender, 6);
	free(sender);
	pserv += 6;
	/* Наш error_code и null-terminate байт */
	*pserv = error_code;
	pserv++;
	*pserv = 0x00;

	if (send_data(serv_pckt, SERV_PACKET_SIZE) == 13) {
		syslog(LOG_ERR, "Error adding service packet to buffer: No enough memory\n");
		return -1;
	}

	return 0;
}

