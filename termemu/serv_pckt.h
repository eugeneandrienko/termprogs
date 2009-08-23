/* File: serv_pckt.h
* Created: 27 Jul, 2009
* License: GNU GPLv3 
* Author: h0rr0rr_drag0n 
* Description: Функции, формирующие сервисный
* пакет.
*/

#ifndef _SERV_PCKT_H_
#define _SERV_PCKT_H_

/* Функция формирует сервисный пакет и помещает его в
 * буфер пакетов, ожидающих отправки.
 * Возвращает ноль если все нормально и не ноль если
 * что-то не в порядке.
 */
int create_serv_pckt(unsigned char error_code,
		void * rcvr_addr, /* Указатель на адрес получателя пакета,
							  адрес в формате адреса получателя, описанном в
							  протоколе.
							*/
		struct sockaddr_in * host_addr /* IP, port текущего подключения */
		);

#endif /* _SERV_PCKT_H_ */

