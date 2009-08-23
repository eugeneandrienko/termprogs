/* File: net.c
* Created: 03 Aug, 2009
* License: GNU GPLv3 
* Author: h0rr0rr_drag0n 
* Description: Функции, обеспечивающие работу
* сервера в сети.
*/

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h> /* struct sockaddr_in */
#include <signal.h> /* sigaction */
#include <stdio.h> /* sprintf() */
#include <stdlib.h> /* malloc() */
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#include "defines.h"
#include "net.h"

/* Буфер принятых пакетов */
struct packet_buffer {
	void * packet[MAXDATA2SEND];
	unsigned long int size[MAXDATA2SEND];
	char * ip[MAXDATA2SEND]; /* ip получателя пакета */
	unsigned short int port[MAXDATA2SEND]; /* в сетевом порядке байтов */
	char used_cells[MAXDATA2SEND];
};

/* Набор клиентских сокетов */
struct client_sockets {
	int sockfd[MAXCONNS];
	char * ip[MAXCONNS];
	unsigned short int port[MAXCONNS]; /* порт удаленного конца соединений (в сетевом порядке байт) */
	char used_cells[MAXCONNS]; /* если ячейка с указанным индексом используется - здесь 1 иначе 0. */
};

/* Обработчик сигнала SIGALRM.
 * Необходим для предотвращения прерывания процесса
 * при доставке сигнала SIGALRM, выданного нашим таймером.
 */
void sigalrm_handler(int unused) {
	return;
}

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

/* Функция помещает переданный ей дескриптор в набор
 * struct client_sockets.
 * Если все нормально, возвращает ноль. При возникновении ошибки возвращается
 * число != 0.
 */
int put_sock_sclsckts(int sockfd, struct client_sockets * cl_sockets) {
	int i = 0;
	struct sockaddr_in cliaddr;
	int cliaddrlen = sizeof(cliaddr);

	memset(&cliaddr, 0, sizeof(cliaddr));
	for(; i < MAXCONNS; ++i) {
		if (cl_sockets->used_cells[i] == 0) {
			cl_sockets->sockfd[i] = sockfd;
			cl_sockets->used_cells[i] = 1;
			if (getpeername(sockfd, (struct sockaddr *)&cliaddr, (socklen_t *)&cliaddrlen)) {
				syslog(LOG_ERR, "Error get peer name: %s\n", strerror(errno));
				return -1;
			}
			cl_sockets->port[i] = cliaddr.sin_port;
			if ((cl_sockets->ip[i] = malloc(IP_LENGTH)) == NULL) {
				syslog(LOG_ERR, "Error allocate memory for IP addr string\n");
				return -1;
			}
			memset(cl_sockets->ip[i], 0, IP_LENGTH);
			if (inet_ntop(AF_INET, &cliaddr.sin_addr, cl_sockets->ip[i], IP_LENGTH) == NULL) {
				syslog(LOG_ERR, "Error convert IP addr from binary to text form\n");
				return -1;
			}
#ifdef DEBUG
			printf("Connected %s:%d\n", cl_sockets->ip[i], ntohs(cl_sockets->port[i]));
#endif /* DEBUG */
			return 0;
		}
	}

	return -1;
}

/* Функция удаляет переданный ей дескриптор из набора, если он там есть.
 * Если его там нет - сразу же возвращается с кодом завершения 0.
 */
int remove_sock_sclsckts(int sockfd, struct client_sockets * cl_sockets) {
	int i = 0;

	for (; i < MAXCONNS; ++i) {
		if (cl_sockets->sockfd[i] == sockfd) {
#ifdef DEBUG
			printf("Disconnected: %s:%d\n", cl_sockets->ip[i], ntohs(cl_sockets->port[i]));
#endif /* DEBUG */
			cl_sockets->sockfd[i] = 0;
			free(cl_sockets->ip[i]);
			cl_sockets->port[i] = 0;
			cl_sockets->used_cells[i] = 0;

			return 0;
		}
	}

	return 0;
}

/* Функция помещает пакет в буфер принятых пакетов. Возвращает число != 0 если
 * произошла ошибка.
 */
int put_packet_buffer(void * packet, unsigned long int size, struct packet_buffer * pbuffer) {
	int i = 0, j = 0;
	unsigned char octet[4] = {0, 0, 0, 0};

	for (; i < MAXDATA2SEND; ++i) {
		if (pbuffer->used_cells[i] == 0) {
			if ((pbuffer->packet[i] = malloc(size)) == NULL) {
				syslog(LOG_ERR, "Cannot allocate memory for packet in buffer\n");
				return -1;
			}
			memcpy(pbuffer->packet[i], packet, size);
			pbuffer->size[i] = size;

			for (j = 0; j < 4; j++) { /* выковыриваем адрес получателя */
				octet[j] = *((unsigned char *)packet + j + 1);
			}
			if ((pbuffer->ip[i] = malloc(IP_LENGTH + 1)) == NULL) {
				syslog(LOG_ERR, "Cannot allocate memory for IP addr string\n");
				return -1;
			}
			memset(pbuffer->ip[i], 0, IP_LENGTH + 1);
			snprintf(pbuffer->ip[i], IP_LENGTH + 1, "%d.%d.%d.%d", octet[0],
					octet[1], octet[2], octet[3]);
			memcpy(&pbuffer->port[i], packet + 5, 2);
			pbuffer->used_cells[i] = 1;
#ifdef DEBUG
			printf("Got packet for %s:%d\n", pbuffer->ip[i], ntohs(pbuffer->port[i]));
#endif /* DEBUG */

			return 0;
		}
	}

	return -1;
}

/* Функция ищет в буфере пакетов, пакеты для переданного ей сокета и отсылает
 * их в сеть. Отосланные пакеты удаляются из буфера.
 */
int search_packets4sock(struct packet_buffer * pbuffer, struct client_sockets * cl_socks, int index) {
	int i = 0;

	for (; i < MAXDATA2SEND; i++) {
		if ((pbuffer->used_cells[i] == 1) && (!strncmp(cl_socks->ip[index], pbuffer->ip[i], IP_LENGTH)) &&
				(cl_socks->port[index] == pbuffer->port[i])) {
#ifdef DEBUG
			printf("We found data for %s:%d\n", cl_socks->ip[index], ntohs(cl_socks->port[index]));
#endif /* DEBUG */
			if (writen(cl_socks->sockfd[index], pbuffer->packet[i], pbuffer->size[i]) < 0) {
				syslog(LOG_ERR, "Error writing dat to socket: %s\n", strerror(errno));
				return -1;
			}
			free(pbuffer->packet[i]);
			pbuffer->size[i] = 0;
			free(pbuffer->ip[i]);
			pbuffer->port[i] = 0;
			pbuffer->used_cells[i] = 0;
		}
	}

	return 0;
}

/* Функция удаляет из буфера пакетов пакеты для получателей, которые, в данный момент
 * не подключены к серверу -- не содержатся в структуре данных client_sockets. Это позволяет
 * избежать атаки типа Denial of Service, когда злоумышленник заполняет весь буфер пакетами
 * для несуществующих получателей и для остальных пакетов не остается места.
 */
int clear_packets(struct packet_buffer * pbuffer, struct client_sockets * cl_socks) {
	int i = 0, j = 0;

	for (; i < MAXDATA2SEND; i++) {
		if (pbuffer->used_cells[i] == 0) {
			continue;
		}
		for (j = 0; j < MAXCONNS; j++) {
			if ((cl_socks->used_cells[j] == 1) &&
					(!strncmp(cl_socks->ip[j], pbuffer->ip[i], IP_LENGTH)) &&
					(cl_socks->port[j] == pbuffer->port[i])) {
				break;
			}
		}
		if (j == MAXCONNS) {
#ifdef DEBUG
			printf("Removing packet for non-existent client: %s:%d\n",
					pbuffer->ip[i], ntohs(pbuffer->port[i]));
#endif /* DEBUG */
			free(pbuffer->ip[i]);
			free(pbuffer->packet[i]);
			pbuffer->size[i] = 0;
			pbuffer->port[i] = 0;
			pbuffer->used_cells[i] = 0;
		}
	}

	return 0;
}

/* Функция запускает сетевые соединения для двух типов
 * клиентов.
 * Возвращает ноль если все в порядке и число != 0 если
 * произошла ошибка.
 */
int net_initial(struct settings * sets) {
	int lawsfd = 0, ltermfd = 0; /* прослушивающие сокеты для АРМ и терминалов */
	int connfd = 0;
	int nready = 0;
	int maxfd = 0;
	int readretval = 0;
	struct sockaddr_in lawsaddr, ltermaddr;
	struct sockaddr_in cliaddr;
	int cliaddr_size = 0;
	fd_set allset, rset, wset;
	struct client_sockets clsocks;
	int i = 0;
	struct itimerval it;
	struct timeval tivl;
	unsigned char incoming_pckt[INCOMING_PCKT_SIZE] = "\0";
	void * data_pckt = NULL;
	int data_pckt_size = 0;
	void * inc_pckt = NULL;
	struct packet_buffer pbuffer;
	struct sigaction act;

	if ((lawsfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		syslog(LOG_ERR, "Cannot create aws listening socket: %s\n", strerror(errno));
		return -1;
	}
	if ((ltermfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		syslog(LOG_ERR, "Cannot create term listening socket: %s\n", strerror(errno));
		return -1;
	}

	memset(&lawsaddr, 0, sizeof(lawsaddr));
	memset(&ltermaddr, 0, sizeof(ltermaddr));
	lawsaddr.sin_family = AF_INET;
	ltermaddr.sin_family = AF_INET;
	if (inet_pton(AF_INET, sets->aws_ip, &lawsaddr.sin_addr) != 1) {
		syslog(LOG_ERR, "Cannot convert aws IP %s from text to binary form: %s\n",
				sets->aws_ip, strerror(errno));
		return -1;
	}
	if (inet_pton(AF_INET, sets->terminal_ip, &ltermaddr.sin_addr) != 1) {
		syslog(LOG_ERR, "Cannot convert term IP %s from text to binary form: %s\n", 
				sets->terminal_ip, strerror(errno));
		return -1;
	}
	lawsaddr.sin_port = htons(sets->aws_port);
	ltermaddr.sin_port = htons(sets->terminal_port);

	if (bind(lawsfd, (struct sockaddr *)&lawsaddr, sizeof(lawsaddr))) {
		syslog(LOG_ERR, "Error binding aws socket: %s\n", strerror(errno));
		return -1;
	}
	if (bind(ltermfd, (struct sockaddr *)&ltermaddr, sizeof(ltermaddr))) {
		syslog(LOG_ERR, "Error binding terminal socket: %s\n", strerror(errno));
		return -1;
	}
	if (listen(lawsfd, MAXCONNS)) {
		syslog(LOG_ERR, "Error listen aws socket: %s\n", strerror(errno));
		return -1;
	}
	if (listen(ltermfd, MAXCONNS)) {
		syslog(LOG_ERR, "Error listen terminal socket: %s\n", strerror(errno));
		return -1;
	}

	FD_ZERO(&allset);
	FD_SET(lawsfd, &allset);
	FD_SET(ltermfd, &allset);
	if (lawsfd > ltermfd) {
		maxfd = lawsfd;
	} else {
		maxfd = ltermfd;
	}
	memset(&clsocks, 0, sizeof(clsocks));
	memset(&pbuffer, 0, sizeof(pbuffer));
	/* устанавливаем обработчик сигнала SIGALRM, во избежание
	 * прерывания процесса при приходе этого сигнала.
	 */
	memset(&act, 0, sizeof(act));
	act.sa_handler = sigalrm_handler;
	if (sigaction(SIGALRM, &act, NULL)) {
		syslog(LOG_ERR, "Error install SIGALRM handler: %s\n", strerror(errno));
		return -1;
	}

	/* ---------------------------------------------------------------------------*/

	while (1) {
		rset = allset;
		if ((nready = select(maxfd + 1, &rset, NULL, NULL, NULL)) < 0) {
			syslog(LOG_ERR, "Error select(): %s\n", strerror(errno));
			goto shutdown;
		}
		/* соединение с новым клиентом */
		if (FD_ISSET(lawsfd, &rset)) {
			if ((connfd = accept(lawsfd, (struct sockaddr *)&cliaddr,
						(socklen_t *)&cliaddr_size)) < 0) {
				syslog(LOG_ERR, "Error accepting incoming connection\n");
			} else {
				if (put_sock_sclsckts(connfd, &clsocks) != 0) {
					syslog(LOG_ERR, "Error adding new client socket to buffer\n");
				} else {
					FD_SET(connfd, &allset);
					if (connfd > maxfd) {
						maxfd = connfd;
					}
				}
			}
			--nready;
		}
		if (FD_ISSET(ltermfd, &rset)) {
			if ((connfd = accept(ltermfd, (struct sockaddr *)&cliaddr,
						(socklen_t *)&cliaddr_size)) < 0) {
				syslog(LOG_ERR, "Error accepting incoming connection\n");
			} else {
				if (put_sock_sclsckts(connfd, &clsocks) != 0) {
					syslog(LOG_ERR, "Error adding new client socket to buffer\n");
				} else {
					FD_SET(connfd, &allset);
					if (connfd > maxfd) {
						maxfd = connfd;
					}
				}
			}
			--nready;
		}

		/* получение новых данных от клиента */
		for (i = 0; i < MAXCONNS; ++i) {
			if (FD_ISSET(clsocks.sockfd[i], &rset)) {
				start_timer(&it);
				readretval = readn(clsocks.sockfd[i], incoming_pckt, INCOMING_PCKT_SIZE - 2);
				stop_timer(&it);
				if (readretval == -2) {
					continue;
				}
				if (readretval == -1) {
					syslog(LOG_ERR, "Read error: %s\n", strerror(errno));
				} else if (readretval == 0) {
					if (close(clsocks.sockfd[i])) {
						syslog(LOG_ERR, "Error close(): %s\n", strerror(errno));
					}
					FD_CLR(clsocks.sockfd[i], &allset);
					if (remove_sock_sclsckts(clsocks.sockfd[i], &clsocks)) {
						syslog(LOG_ERR, "Error removing socket from buffer\n");
					}
				} else {
					if (incoming_pckt[0] == 0xda) {
						start_timer(&it);
						readretval = readn(clsocks.sockfd[i], &incoming_pckt[INCOMING_PCKT_SIZE - 2], 2);
						stop_timer(&it);
						if (readretval == -2) {
							continue;
						}
						if (readretval == -1) {
							syslog(LOG_ERR, "Cannot read data from socket\n");
							goto shutdown;
						}
						memcpy(&data_pckt_size, &incoming_pckt[13], 4);
						data_pckt_size = ntohl(data_pckt_size);
						if ((data_pckt = malloc(data_pckt_size + 3)) == NULL) {
							syslog(LOG_ERR, "Cannot allocate memory for data packet: %s\n", strerror(errno));
							goto shutdown;
						}
						start_timer(&it);
						readretval = readn(clsocks.sockfd[i], data_pckt, data_pckt_size + 3);
						stop_timer(&it);
						if (readretval == -2) {
							continue;
						}
						if (readretval == -1) {
							syslog(LOG_ERR, "Cannot read data from socket\n");
							goto shutdown;
						}
						if ((inc_pckt = malloc(data_pckt_size + 20)) == NULL) {
							syslog(LOG_ERR, "Cannot allocate memory for data packet: %s\n", strerror(errno));
							goto shutdown;
						}
						memcpy(inc_pckt, incoming_pckt, 17);
						memcpy(inc_pckt + 17, data_pckt, data_pckt_size + 3);
						if (put_packet_buffer(inc_pckt, data_pckt_size + 20, &pbuffer)) {
							syslog(LOG_ERR, "Cannot put incoming pckt to buffer\n");
							goto shutdown;
						}
					} else {
						if (put_packet_buffer(incoming_pckt, 15, &pbuffer)) {
							syslog(LOG_ERR, "Cannot put incoming pckt to buffer\n");
							goto shutdown;
						}
					}
				}

				if (--nready <= 0) {
					break;
				}
			}
		}

		memset(&tivl, 0, sizeof(tivl));
		tivl.tv_sec = TIME;
		tivl.tv_usec = 0;
		FD_ZERO(&wset);
		wset = allset;
		if ((nready = select(maxfd + 1, NULL, &wset, NULL, &tivl)) < 0) {
			syslog(LOG_ERR, "Error select(): %s\n", strerror(errno));
			goto shutdown;
		}
		if (nready == 0) {
			continue;
		}
		/* отправляем данные готовым клиентам */
		for (i = 0; i < MAXCONNS; ++i) {
			if ((clsocks.used_cells[i] == 1) && FD_ISSET(clsocks.sockfd[i], &wset)) {
				if (search_packets4sock(&pbuffer, &clsocks, i)) {
					syslog(LOG_ERR, "Error sending packets to network\n");
				}
				if (--nready <= 0) {
					break;
				}
			}
		} /* for (i = 0; i < MAXCONNS; ++i) */

		/* чистим буфер пакетов */
		if (clear_packets(&pbuffer, &clsocks)) {
			syslog(LOG_ERR, "Error clearing packets buffer\n");
		}
	} /* while (1) */

	/* ------------------------------------------------------------- */

shutdown:
	for (i = 0; i < MAXCONNS; ++i) {
		if (clsocks.used_cells[i] == 1) {
			close(clsocks.sockfd[i]);
		}
	}
	close(ltermfd);
	close(lawsfd);

	return 0;
}

