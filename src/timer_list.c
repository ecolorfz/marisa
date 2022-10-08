include "time.h"
 #include "timer_list.h"
 #include "stdlib.h"

 #define TO_SEC(timespec) (timespec.tv_sec + timespec.tv_nsec / 1000000000.0)
 #define TO_NANO(timespec) (timespec.tv_sec * 1000000000 + timespec.tv_nsec)
 #define TO_TIMESPEC(nano) (struct timespec){.tv_sec = (time_t)(nano / 1000000000), .tv_nsec = (long)(nano % 1000000000)}
 void free_node(struct time_node *tmp);

 struct  timer_list * timer_list_init() {
   struct  timer_list *list = malloc(sizeof(struct  timer_list));
   list->head = NULL;
   list->tail = NULL;
   list->size = 0;
   list->id_pool = 1;
   pthread_mutex_init(&list->lock, NULL);
   return list;
 }

 uint32_t set_timer(struct  timer_list *list, uint32_t sec, uint32_t nano_sec, void *(*callback)(void *), void *args) {

   pthread_mutex_lock(&list->lock);

   struct timespec *now = malloc(sizeof(struct timespec));
   memset(now, 0, sizeof(struct timespec));
   clock_gettime(CLOCK_REALTIME, now);
   struct time_node *node = malloc(sizeof(struct time_node));
   now->tv_sec += sec;
   now->tv_nsec += nano_sec;
   node->event.timeout = now;
   node->event.callback = callback;
   node->event.args = args;
   node->id = list->id_pool++;
   node->next = NULL;
   if (list->head == NULL) {
     list->head = node;
     list->tail = node;
   } else {
     list->tail->next = node;
     list->tail = node;
   }
   list->size++;
   pthread_mutex_unlock(&list->lock);
   return node->id;
 }
 uint32_t set_timer_without_mutex(struct  timer_list *list,uint32_t sec,uint32_t nano_sec, void *(*callback)(void *),void *args) {
   struct timespec *now = malloc(sizeof(struct timespec));
   memset(now, 0, sizeof(struct timespec));
   clock_gettime(CLOCK_REALTIME, now);
   struct time_node *node = malloc(sizeof(struct time_node));
   now->tv_sec += sec;
   now->tv_nsec += nano_sec;
   node->event.timeout = now;
   node->event.callback = callback;
   node->event.args = args;
   node->id = list->id_pool++;
   node->next = NULL;
   if (list->head == NULL) {
     list->head = node;
     list->tail = node;
   } else {
     list->tail->next = node;
     list->tail = node;
   }
   list->size++;
   return node->id;
 }
 
 void *check_timer(struct  timer_list *list) {
   if (list->head == NULL) {
     return NULL;
   }

   pthread_mutex_lock(&list->lock);

   struct timespec now;
   clock_gettime(CLOCK_REALTIME, &now);
   uint64_t current_time = TO_NANO(now);
   uint64_t timeout = TO_NANO((*(list->head->event.timeout)));
   if (timeout <= current_time) {
     struct time_node *tmp = list->head;
     list->head = list->head->next;
      
     //invoke the callback function
     void *result = tmp->event.callback(tmp->event.args);

     free_node(tmp);
     list->size--;
     pthread_mutex_unlock(&list->lock);

     return result;
   }
   pthread_mutex_unlock(&list->lock);

   return NULL;
 }

 void free_node(struct time_node *tmp) {
   free(tmp->event.timeout);
   free(tmp);
 }

 
 uint32_t get_recent_timeout(struct  timer_list *list) {
   if (list->head == NULL) {
     return 0;
   }

   pthread_mutex_lock(&list->lock);

   struct timespec now;
   clock_gettime(CLOCK_REALTIME, &now);
   uint32_t current_time = TO_NANO(now);
   uint32_t timeout = TO_NANO((*(list->head->event.timeout))) - current_time;

   pthread_mutex_unlock(&list->lock);
   return timeout;
 }

 int cancel_timer(struct  timer_list *list, uint32_t id) {
   pthread_mutex_lock(&list->lock);
   struct time_node *tmp = list->head;
   struct time_node *prev = NULL;
   while (tmp != NULL) {
     if (tmp->id == id) {
       if (prev == NULL) {
         list->head = tmp->next;
       } else {
         prev->next = tmp->next;
       }
       free_node(tmp);
       list->size--;
       DEBUG_PRINT("timer %d canceled\n", id);
       pthread_mutex_unlock(&list->lock);
       return 0;
     }
     prev = tmp;
     tmp = tmp->next;
   }
   pthread_mutex_unlock(&list->lock);

   return -1;
 }

 int cancel_timer_until(struct  timer_list *list, int id) {
   pthread_mutex_lock(&list->lock);
   struct time_node *tmp = list->head;
   struct time_node *prev = NULL;
   while (tmp != NULL) {
     if (tmp->id <= id) {
       if (prev == NULL) {
         list->head = tmp->next;
       } else {
         prev->next = tmp->next;
       }
       free_node(tmp);
       list->size--;
       return 0;
     }
     prev = tmp;
     tmp = tmp->next;
   }
   pthread_mutex_unlock(&list->lock);
   return -1;
 }