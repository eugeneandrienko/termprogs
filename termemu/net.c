/* File: net.c
* Created: 27 Jul, 2009
* License: GNU GPLv3 
* Author: h0rr0rr_drag0n 
* Description: Функции, обеспечивающие работу в сети
*/

#include <arpa/inet.h> /* [hn]to[nh][ls](), inet_pton() */
#include <errno.h>
#include <netinet/in.h> /* struct sockaddr_in */
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <signal.h> /* sigaction */
#include <stdlib.h> /* malloc(), free() */
#include <string.h>
#include <syslog.h> /* syslog() */
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include "defines.h"
#include "net.h"
#include "data_pckt.h"
#include "serv_pckt.h"

/* Буфер пакетов, готовых для отправки.
 * Каждому адресу пакета соответствует размер этого пакета (см.
 * соответствующие индексы).
 * Память под пакет выделяется функциями создаюшими этот пакет.
 * Память освобождается после отправки пакета в сеть, также обнуляется
 * размер пакета. По мере отправки всех пакетов уменьшается номер
 * первого свободного элемента двух массивов.
 */
struct sendata_buff {
	void * packet[MAXBUFFSIZE]; /* адрес пакета в памяти */
	unsigned long int packet_size[MAXBUFFSIZE]; /* размер соответствующего пакета */
	unsigned int first_free_elem; /* Номер первого свободного элемента двух массивов */
};
static struct sendata_buff sbuffer;

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

/* Подключение к серверу, прием, отправка пакетов в сеть */
int net_initial(struct settings * sets, RSA * serv_pubkey) {
	int sockfd = 0;
	int readretval = 0;
	struct sigaction act;
	struct sockaddr_in servaddr;
	struct sockaddr_in hostaddr; /* адрес нашего хоста */
	socklen_t hostaddrsize = sizeof(hostaddr);
	struct itimerval it;
	unsigned char incoming_pckt[INCOMING_PCKT_SIZE] = "\0";

	sbuffer.first_free_elem = 0;
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		syslog(LOG_ERR, "Error creating new socket: %s\n", strerror(errno));
		return -1;
	}
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
	/* устанавливаем обработчик сигнала SIGALRM, во избежание
	 * прерывания процесса при приходе этого сигнала.
	 */
	memset(&act, 0, sizeof(act));
	act.sa_handler = sigalrm_handler;
	if (sigaction(SIGALRM, &act, NULL)) {
		syslog(LOG_ERR, "Error install SIGALRM handler: %s\n", strerror(errno));
		return -1;
	}

	/* --------------main loop here--------------------------------- */
	
	while (1) {
		/* Запускаем TIME-секундный таймер.
		 * Если команда так и не будет прочитана, выполнение readn()
		 * завершится через NRETRY * TIME секунд.
		 */
		memset(&it, 0, sizeof(it));
		it.it_interval.tv_sec = TIME;
		it.it_value.tv_sec = TIME;
		setitimer(ITIMER_REAL, &it, NULL);
		memset(&incoming_pckt, 0, INCOMING_PCKT_SIZE);
		readretval = readn(sockfd, &incoming_pckt, INCOMING_PCKT_SIZE);
		/* Останавливаем таймер */
		it.it_interval.tv_sec = 0;
		it.it_interval.tv_usec = 0;
		it.it_value.tv_sec = 0;
		it.it_value.tv_usec = 0;
		setitimer(ITIMER_REAL, &it, NULL);

		if (readretval == -1) {
			syslog(LOG_ERR, "Error reading packet from socket: %s\n", strerror(errno));
			goto shutdown;
		} else if (readretval == 0) { /* сервер закрыл соединение */
			goto shutdown;
		}
		/* Мы прочитали пакет до конца, не прервавшись по таймауту.
		 * Переходим к обработке полученного пакета
		 */
		if (readretval > 0) {
			switch (incoming_pckt[0]) {
				case 0xff: /* получили сервисный пакет */
					if (incoming_pckt[13] == 0x00) { /* keep-alive ping */
						if (create_serv_pckt(0x00, &incoming_pckt[7], &hostaddr)) {
							syslog(LOG_ERR, "Cannot create keep-alive packet\n");
						}
					} else {
						syslog(LOG_ERR, "We get service packet with error code 0x%x\n",
								(unsigned int)incoming_pckt[13]);
					}
					break;
				case 0xc0: /* получили управляющий пакет */
					/* Управляющий пакет достаточно прост -
					 * разбираем его тут.
					 */
					switch (incoming_pckt[13]) {
						case 0xdc: /* запрос пакета с данными */
							if (create_data_pckt(&incoming_pckt[7], &hostaddr, serv_pubkey)) {
								syslog(LOG_ERR, "Cannot create data packet\n");
							}
							break;
						case 0xf2: /* остановка работы терминала */
							goto shutdown;
						default: /* другой код команды */
							if (create_serv_pckt(0x1c, &incoming_pckt[7], &hostaddr)) {
								syslog(LOG_ERR, "Cannot create service packet\n");
							}
							break;
					} /* switch (incoming_pckt[14]) */
					break;
				default: /* получили то, что не должны были получить */
					break;
			} /* switch (incoming_pckt[0]) */
		} /* if (readretval > 0) */

		/* Проверяем буфер отправки данных.
		 * Если он не пуст - отправляем из него данные в сеть.
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
	} /* while (1) */

	/* ------------------------------------------------------------- */

shutdown:
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

