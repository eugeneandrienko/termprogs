/* File: net.h
* Created: 26 Jul, 2009
* License: GNU GPLv3 
* Author: h0rr0rr_drag0n 
* Description: Функции, обеспечивающие
* работу программы в сети.
*/

#ifndef _NET_H_
#define _NET_H_

/* Подключение к серверу, прием, отправка пакетов в сеть */
int net_initial(struct settings * sets, RSA * serv_pubkey);

/* Функция добавляет пакет в буфер пакетов, ожидающих
 * отправки.
 * Возвращает code 13, если место в буфере кончилось.
 */
int send_data(void * packet, unsigned long int size);

#endif /* _NET_H_ */

