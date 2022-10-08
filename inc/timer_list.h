/*
implement a c++ stl list
using c
in order to store timer
*/
#ifndef TIMER_LIST_H_
#define TIMER_LIST_H_
#include "global.h"
#define SEC2NANO(x) (uint32_t)(x*1e9)
#define TO_TIMESPEC(nano) (struct timespec){.tv_sec = (time_t)(nano / 1000000000), .tv_nsec = (long)(nano % 1000000000)}

typedef struct timer_event{
    struct timespec *timeout;
    void *(*callback)(void *);
    void *args;
}timer_event;

typedef struct timer_node{
   struct timer_event event;
   struct time_node *next;
   uint32_t id;
}timer_node;

typedef struct timer_list{
   struct time_node *head;
   struct time_node *tail;
   int size;
   uint32_t id_pool;
   pthread_mutex_t lock;
}timer_list;

/*
初始化list
*/
struct timer_list * timer_list_init();

/*
* get the timeout of the first timer
*/
 uint32_t get_recent_timeout(struct timer_list *list);
 
/*
set a timer with nano_sec timeout
callback is the function to be called when timeout
args is the arguments to be passed to the callback function
return timer id
*/
 uint32_t set_timer(struct timer_list *list, uint32_t sec, uint32_t nano_sec, void *(*callback)(void *), void *args);
 
 /*
  * check if any timer is timeout
  * invoke the callback function if timeout
  * return the callback function's return value
  * NULL if no timer is timeout
  */
 void *check_timer(struct timer_list *list);

 /*
 */
 uint32_t set_timer_without_mutex(struct timer_list *list,uint32_t sec, uint32_t nano_sec,void *(*callback)(void *),void *args);    
 
/*
  * cancel a timer
  * return 0 if success
  * return -1 if timer not found
  */             
 int cancel_timer(struct timer_list *list, uint32_t id);

int cancel_timer_until(struct timer_list *list, int id);


#endif