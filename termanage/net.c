/* File: net.c
* Created: 31 Jul, 2009
* License: GNU GPLv3 
* Author: h0rr0rr_drag0n 
* Description: Функции обеспечивающие работу программы
* в сети.
*/

#include <arpa/inet.h> /* [hn]to[nh][ls](), inet_pton() */
#include <errno.h>
#include <netinet/in.h> /* struct sockaddr_in */
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <signal.h> /* sigaction */
#include <stdlib.h> /* malloc(), free() */
#include <string.h>
#include <sys/time.h>
#include <syslog.h> /* syslog() */
#include <unistd.h>
#include "data_pckt.h"
#define _SOCKADDR2PROTOFORM_
#include "defines.h"
#include "manag_pckt.h"
#include "net.h"
#include "serv_pckt.h"

void start_timer(struct itimerval * it) {
	memset(it, 0, sizeof(*it));
	it->it_interval.tv_sec = TIME;
	it->it_value.tv_sec = TIME;
	setitimer(ITIMER_REAL, it, NULL);
	return;
}

void stop_timer(struct itimerval * it) {
	it->it_interval.tv_sec = 0;
	it->it_interval.tv_usec = 0;
	it->it_value.tv_sec = 0;
	it->it_value.tv_usec = 0;
	setitimer(ITIMER_REAL, it, NULL);
	return;
}

/* Буфер пакетов, готовых для отправки.
 * Каждому адресу пакета соответствует размер этого пакета (см.
 * соответствующие индексы).
 * Память под пакет выделяется функциями создаюшими этот пакет.
 * Память освобождается после отправки пакета в сеть, также обнуляется
 * размер пакета. По мере отправки всех пакетов уменьшается номер
 * первого свободного элемента двух массивов.
 */
struct sencmds_buff {
	void * packet[MAXBUFFSIZE]; /* адрес пакета в памяти */
	unsigned long int packet_size[MAXBUFFSIZE]; /* размер соответствующего пакета */
	unsigned int first_free_elem; /* Номер первого свободного элемента двух массивов */
};
static struct sencmds_buff sbuffer;

/* Обработчик сигнала SIGALRM.
 * Необходим для предотвращения прерывания процесса
 * при доставке сигнала SIGALRM, выданного нашим таймером.
 */
void sigalrm_handler(int unused) {
	return;
}

/* Функция, гарантированно читающая n байт из дескриптора.
 * Реализация взята из Стивенса "UNIX Разработка сетевых приложений"
 */
ssize_t readn(int fd, void * vptr, size_t n) {
	size_t nleft;
	ssize_t nread;
	char * ptr;
	int retry = NRETRY;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ((nread = read(fd, ptr, nleft)) < 0) {
			if (errno == EINTR) { /* прерваны сигналом */
				nread = 0;
				if (retry == 0) { /* исчерпано количество попыток прочитать команду */
					return -2;
				}
				retry--;
			} else {
				return -1;
			}
		} else {
			if (nread == 0) {
				break; /* EOF */
			}
		}
		nleft -= nread;
		ptr += nread;
	}

	return (n - nleft);
}

/* Функция, гарантированно пишущая n байт в дескриптор.
 * Реализация взята из Стивенса "UNIX Разработка сетевых приложений"
 */
ssize_t writen(int fd, const void * vptr, size_t n) {
	size_t nleft;
	ssize_t nwritten;
	const char * ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ((nwritten = write(fd, ptr, nleft)) <= 0) {
			if (errno == EINTR) { /* прерваны сигналом */
				nwritten = 0;
			} else {
				return -1;
			}
		}
		nleft -= nwritten;
		ptr += nwritten;
	}

	return n;
}

/* Функция обеспечивает подключение программы к серверу; прием, отправку и
 * обработку пакетов.
 * Функция возвращает ноль если все успешно и число != 0 если произошла ошибка.
 */
int net_initial(struct settings * sets, char * termip, unsigned short int termport,
		RSA * serv_privkey) {
	int sockfd = 0;
	struct sockaddr_in servaddr;
	struct sockaddr_in hostaddr;
	struct sigaction act;
	socklen_t hostaddrsize = sizeof(hostaddr);
	struct itimerval it;
	unsigned char incoming_pckt[INCOMING_PCKT_SIZE] = "\0";
	int readretval = 0;
	void * data_pckt_data = NULL;
	unsigned long int data_pckt_data_size = 0;
	int i = 0;
	unsigned char mancodes[3] = {0xdc, 0xdc, 0xf2};
	struct sockaddr_in termaddr; /* адрес получателя */
	void * termaddr_protoform = NULL; /* адрес получателя в формате протокола */

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		syslog(LOG_ERR, "Error creating new socket: %s\n", strerror(errno));
		return -1;
	}
	sbuffer.first_free_elem = 0;
	memset(&servaddr, 0, sizeof(servaddr));
	memset(&hostaddr, 0, sizeof(hostaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(sets->serv_port);
	if (inet_pton(AF_INET, sets->serv_ip, &servaddr.sin_addr) != 1) {
		syslog(LOG_ERR, "Bad IP address format: %s\n", sets->serv_ip);
		return -1;
	}
	if (connect(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) {
		syslog(LOG_ERR, "Connect error: %s\n", strerror(errno));
		return -1;
	}
	if (getsockname(sockfd, (struct sockaddr *)&hostaddr, &hostaddrsize)) {
		syslog(LOG_ERR, "Error getting info about host address: %s\n", strerror(errno));
		return -1;
	}
	/* Преобразуем адрес получателя в форму, принятую в протоколе, для
	 * дальнейшего использования в функции create_manag_pckt()
	 */
	memset(&termaddr, 0, sizeof(termaddr));
	termaddr.sin_family = AF_INET;
	termaddr.sin_port = htons(termport);
	if (inet_pton(AF_INET, termip, &termaddr.sin_addr) != 1) {
		syslog(LOG_ERR, "Bad terminal IP address format: %s\n", termip);
		return -1;
	}
	if ((termaddr_protoform = sockaddr2protoform(&termaddr)) == NULL) {
		syslog(LOG_ERR, "Error transforming terminal address to proto form\n");
		return -1;
	}
	/* устанавливаем обработчик сигнала SIGALRM, во избежание
	 * прерывания процесса при приходе этого сигнала.
	 */
	memset(&act, 0, sizeof(act));
	act.sa_handler = sigalrm_handler;
	if (sigaction(SIGALRM, &act, NULL)) {
		syslog(LOG_ERR, "Error install SIGALRM handler: %s\n", strerror(errno));
		return -1;
	}

	/* -------------------main loop here----------------------------------*/

	while (1) {
		/* добавляем в буфер отправки соответствующий управляющий пакет */
		if (i > 2) {
			break;
		}
		if (create_manag_pckt(mancodes[i], termaddr_protoform, & hostaddr)) {
			syslog(LOG_ERR, "Error creating manage packet\n");
			return -1;
		}
		i++;

		/* Проверяем буфер отправки команд.
		 * Если он не пуст - отправляем команды из него в сеть.
		 */
		if (sbuffer.first_free_elem > 0) {
			while (sbuffer.first_free_elem > 0) {
				if (writen(sockfd,
							sbuffer.packet[sbuffer.first_free_elem - 1],
							sbuffer.packet_size[sbuffer.first_free_elem - 1]) == -1) {
					syslog(LOG_ERR, "Error sending packet to network: %s\n", strerror(errno));
					goto shutdown;
				}
				free(sbuffer.packet[sbuffer.first_free_elem -1]);
				sbuffer.packet[sbuffer.first_free_elem - 1] = NULL;
				sbuffer.packet_size[sbuffer.first_free_elem -1] = 0;
				sbuffer.first_free_elem--;
			} /* while (sbuffer.first_free_elem > 0) */
		} /* if (sbuffer.first_free_elem > 0) */

		/* Запускаем TIME-секундный таймер.
		 * Если команда так и не будет прочитана, выполнение readn()
		 * завершится через NRETRY * TIME секунд.
		 */
		memset(incoming_pckt, 0, INCOMING_PCKT_SIZE);
		start_timer(&it);
		readretval = readn(sockfd, incoming_pckt, 15);
		stop_timer(&it);
		if (readretval == -1) {
			syslog(LOG_ERR, "Error reading first 15 bytes from socket: %s\n", strerror(errno));
			goto shutdown;
		} else if (readretval == -2) {
			continue;
		}

		switch (incoming_pckt[0]) {
			case 0xda: /* прочитали пакет с данными */
				start_timer(&it);
				readretval = readn(sockfd, &incoming_pckt[INCOMING_PCKT_SIZE - 2], 2);
				stop_timer(&it);
				if (readretval == -1) {
					syslog(LOG_ERR, "Error reading last 2 bytes from socket: %s\n", strerror(errno));
					goto shutdown;
					continue;
				} else if (readretval == -2) {
					continue;
				}
				/* выделяем размер блока с данными */
				memcpy(&data_pckt_data_size, &incoming_pckt[INCOMING_PCKT_SIZE - 4], sizeof(data_pckt_data_size));
				data_pckt_data_size = ntohl(data_pckt_data_size);
				if ((data_pckt_data = malloc(data_pckt_data_size + 3)) == NULL) {
					syslog(LOG_ERR, "Cannot allocate memory for data in data packet\n");
					goto shutdown;
				}
				start_timer(&it);
				readretval = readn(sockfd, data_pckt_data, data_pckt_data_size + 3);
				stop_timer(&it);
				if (readretval == -1) {
					syslog(LOG_ERR, "Error reading data block from socket: %s\n", strerror(errno));
					goto shutdown;
				} else if (readretval == -2) {
					continue;
				}
				if (parse_data_pckt(incoming_pckt, INCOMING_PCKT_SIZE,
							data_pckt_data, data_pckt_data_size + 3, serv_privkey)) {
					syslog(LOG_ERR, "Error parsing data packet\n");
					continue;
				}
				free(data_pckt_data);
				break;
			case 0xff: /* разбираем сервисный пакет */
				if (incoming_pckt[13] == 0x00) {
					if (create_serv_pckt(0x00, &incoming_pckt[7], &hostaddr)) {
						syslog(LOG_ERR, "Error creating service packet\n");
						continue;
					} else {
						syslog(LOG_ERR, "Service packet error code: %d\n",
								(unsigned int)incoming_pckt[13]);
						continue;
					}
				}
				break;
			default:
				continue;
		} /* switch (incoming_pckt[0]) */
	} /* while (1) */

	/* -------------------------------------------------------------------*/

shutdown:
	free(termaddr_protoform);
	close(sockfd);

	return 0;
}

/* Функция добавляет пакет в буфер пакетов, ожидающих
 * отправки.
 * Возвращает code 13, если место в буфере кончилось.
 */
int send_data(void * packet, unsigned long int size) {
	if (sbuffer.first_free_elem > MAXBUFFSIZE - 1) {
		return 13;
	}
	sbuffer.packet[sbuffer.first_free_elem] = packet;
	sbuffer.packet_size[sbuffer.first_free_elem] = size;
	sbuffer.first_free_elem++;

	return 0;
}

