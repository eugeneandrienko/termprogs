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
#define CONFFILE "/etc/termserv" /* путь к конфигурационному файлу */
#define MAXCONNS 256 /* максимальное количество принимаемых соединений для каждого типа клиентов */
#define MAXDATA2SEND 128 /* Сколько всего пакетов, можем послать сразу */
#define TIME 5 /* столько секунд ждем прихода команды */
#define NRETRY 3 /* количество попыток чтения команды */
#define INCOMING_PCKT_SIZE 17 /* Максимальный размер пакета, который мы можем принять, без выделения дополнительной памяти */

/* Настройки, читаемые из конфигурационного файла */
struct settings {
	char aws_ip[IP_LENGTH];
	char terminal_ip[IP_LENGTH];
	unsigned short int aws_port;
	unsigned short int terminal_port;
};

#endif /* _DEFINES_H_ */

