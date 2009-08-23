/* File: net.h
* Created: 03 Aug, 2009
* License: GNU GPLv3 
* Author: h0rr0rr_drag0n 
* Description: функции, обеспечивающие работу
* сервера в сети.
*/

#ifndef _NET_H_
#define _NET_H_

/* Функция запускает сетевые соединения для двух типов
 * клиентов.
 * Возвращает ноль если все в порядке и число != 0 если
 * произошла ошибка.
 */
int net_initial(struct settings * sets);

#endif /* _NET_H_ */

