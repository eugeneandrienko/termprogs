/* File: manag_pckt.c
* Created: 01 Aug, 2009
* License: GNU GPLv3 
* Author: h0rr0rr_drag0n 
* Description: Функции, формирующие управляющий пакет.
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
#include "manag_pckt.h"

/* Функция формирует управляющий пакет и помещает его в
 * буфер пакетов, ожидающих отправки.
 * Возвращает ноль если все нормально и не ноль если
 * что-то не в порядке.
 */
int create_manag_pckt(unsigned char manag_code, void * rcvr_addr, struct sockaddr_in * host_addr) {
	void * manag_pckt = NULL;
	unsigned char * pmanag = NULL;
	void * sender = NULL;

	if ((manag_pckt = malloc(MANAG_PACKET_SIZE)) == NULL) {
		syslog(LOG_ERR, "Error allocating memory for manage packet\n");
		return -1;
	}

	pmanag = manag_pckt;
	*pmanag = 0xc0; /* устанавливаем байт типа пакета */
	pmanag++;
	/* устанавливаем адрес получателя - равен адресу отправителя пакета, вызвавшего
	 * посылку управляющего пакета.
	 */
	memcpy(pmanag, rcvr_addr, 6);
	pmanag += 6; 
	/* устанавливаем адрес отправителя - равен адресу
	 * текущего соединения
	 */
	if ((sender = sockaddr2protoform(host_addr)) == NULL) {
		syslog(LOG_ERR, "Error converting IP address from struct sockaddr_in to protocol form\n");
		return -1;
	}
	memcpy(pmanag, sender, 6);
	free(sender);
	pmanag += 6;
	/* Наш manag_code и null-terminate байт */
	*pmanag = manag_code;
	pmanag++;
	*pmanag = 0x00;

	if (send_data(manag_pckt, MANAG_PACKET_SIZE) == 13) {
		syslog(LOG_ERR, "Error adding manage packet to buffer: No enough memory\n");
		return -1;
	}

	return 0;
}

