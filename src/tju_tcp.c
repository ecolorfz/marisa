#include "tju_tcp.h"

int queueCnt = 0;
tju_tcp_t* connect_queue[1001];

/*
创建 TCP socket 
初始化对应的结构体
设置初始状态为 CLOSED
*/
tju_tcp_t* tju_socket(){
    tju_tcp_t* sock = (tju_tcp_t*)malloc(sizeof(tju_tcp_t));
    sock->state = CLOSED;
    
    pthread_mutex_init(&(sock->send_lock), NULL);
    sock->sending_buf = NULL;
    sock->sending_len = 0;

    pthread_mutex_init(&(sock->recv_lock), NULL);
    sock->received_buf = NULL;
    sock->received_len = 0;
    
    if(pthread_cond_init(&sock->wait_cond, NULL) != 0){
        perror("ERROR condition variable not set\n");
        exit(-1);
    }

    sock->window.wnd_send = NULL;
    sock->window.wnd_recv = NULL;
    sock->window.wnd_send->nextseq = 1;
    return sock;
}

/*
绑定监听的地址 包括ip和端口
*/
int tju_bind(tju_tcp_t* sock, tju_sock_addr bind_addr){
    sock->bind_addr = bind_addr;
    return 0;
}

/*
被动打开 监听bind的地址和端口
设置socket的状态为LISTEN
注册该socket到内核的监听socket哈希表
*/
int tju_listen(tju_tcp_t* sock){
    sock->state = LISTEN;
    int hashval = cal_hash(sock->bind_addr.ip, sock->bind_addr.port, 0, 0);
    listen_socks[hashval] = sock;
    return 0;
}

/*
接受连接 
返回与客户端通信用的socket
这里返回的socket一定是已经完成3次握手建立了连接的socket
因为只要该函数返回, 用户就可以马上使用该socket进行send和recv
*/
tju_tcp_t* tju_accept(tju_tcp_t* listen_sock){
    while(queueCnt == 0){}
   tju_tcp_t *new_conn = connect_queue[0];
   queueCnt--;
   tju_sock_addr local_addr = new_conn->established_local_addr;
   tju_sock_addr remote_addr = new_conn->established_remote_addr;
    
   int hashval = cal_hash(local_addr.ip, local_addr.port, remote_addr.ip, remote_addr.port);
   established_socks[hashval] = new_conn;

   // 如果new_conn的创建过程放到了tju_handle_packet中 那么accept怎么拿到这个new_conn呢
   // 在linux中 每个listen socket都维护一个已经完成连接的socket队列
   // 每次调用accept 实际上就是取出这个队列中的一个元素
   // 队列为空,则阻塞
   return new_conn;
}


/*
连接到服务端
该函数以一个socket为参数
调用函数前, 该socket还未建立连接
函数正常返回后, 该socket一定是已经完成了3次握手, 建立了连接
因为只要该函数返回, 用户就可以马上使用该socket进行send和recv
*/
int tju_connect(tju_tcp_t* sock, tju_sock_addr target_addr){

    sock->established_remote_addr = target_addr;

    tju_sock_addr local_addr;
    local_addr.ip = inet_network("172.17.0.2");
    local_addr.port = 5678; // 连接方进行connect连接的时候 内核中是随机分配一个可用的端口
    sock->established_local_addr = local_addr;

    // 这里也不能直接建立连接 需要经过三次握手
    // 实际在linux中 connect调用后 会进入一个while循环
    // 循环跳出的条件是socket的状态变为ESTABLISHED 表面看上去就是 正在连接中 阻塞
    // 而状态的改变在别的地方进行 在我们这就是tju_handle_packet
    
    //send syn
    char *msg = create_packet_buf(sock->established_local_addr.port, target_addr.port, CLIENT_SEQ, 0,
                           DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, SYN_FLAG_MASK, 1, 0, NULL, 0);
   sendToLayer3(msg,DEFAULT_HEADER_LEN);
   sock->state = SYN_SENT;
    int hashval = cal_hash(local_addr.ip, local_addr.port, 0, 0);
    listen_socks[hashval] = sock;

    while(sock->state != ESTABLISHED);

    // 将建立了连接的socket放入内核 已建立连接哈希表中
    listen_socks[hashval] = NULL;
    hashval = cal_hash(local_addr.ip, local_addr.port, target_addr.ip, target_addr.port);
    established_socks[hashval] = sock;

    return 0;
}

int tju_send(tju_tcp_t* sock, const void *buffer, int len){
    // 这里当然不能直接简单地调用sendToLayer3
    char* data = malloc(len);
    memcpy(data, buffer, len);

    char* msg;
    uint32_t seq = 464;
    uint16_t plen = DEFAULT_HEADER_LEN + len;

    msg = create_packet_buf(sock->established_local_addr.port, sock->established_remote_addr.port, seq, 0, 
              DEFAULT_HEADER_LEN, plen, NO_FLAG, 1, 0, data, len);

    sendToLayer3(msg, plen);
    
    return 0;
}

int tju_recv(tju_tcp_t* sock, void *buffer, int len){
    while(sock->received_len<=0){
        // 阻塞
    }

    while(pthread_mutex_lock(&(sock->recv_lock)) != 0); // 加锁

    int read_len = 0;
    if (sock->received_len >= len){ // 从中读取len长度的数据
        read_len = len;
    }else{
        read_len = sock->received_len; // 读取sock->received_len长度的数据(全读出来)
    }

    memcpy(buffer, sock->received_buf, read_len);

    if(read_len < sock->received_len) { // 还剩下一些
        char* new_buf = malloc(sock->received_len - read_len);
        memcpy(new_buf, sock->received_buf + read_len, sock->received_len - read_len);
        free(sock->received_buf);
        sock->received_len -= read_len;
        sock->received_buf = new_buf;
    }else{
        free(sock->received_buf);
        sock->received_buf = NULL;
        sock->received_len = 0;
    }
    pthread_mutex_unlock(&(sock->recv_lock)); // 解锁

    return 0;
}

int tju_handle_packet(tju_tcp_t* sock, char* pkt){
    
    uint32_t data_len = get_plen(pkt) - DEFAULT_HEADER_LEN;

    // 把收到的数据放到接受缓冲区
    while(pthread_mutex_lock(&(sock->recv_lock)) != 0); // 加锁
    uint8_t flag = get_flags(pkt);
    uint32_t seq = get_seq(pkt);
    uint32_t ack = get_ack(pkt);
    uint16_t src_port = get_src(pkt);
    uint16_t dst_port = get_dst(pkt);
    tju_tcp_t *new_conn = NULL;

    if(sock->state == LISTEN){
        if(flag == SYN_FLAG_MASK){
            char *msg = create_packet_buf(dst_port, src_port, SERVER_SEQ, seq + 1,
                                 DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, SYN_FLAG_MASK | ACK_FLAG_MASK, 1, 0, NULL, 0);
            sendToLayer3(msg,DEFAULT_HEADER_LEN);
            sock->state = SYN_RECV;
        }
        else if(sock->state == SYN_SENT){
            if(flag == ACK_FLAG_MASK){
                new_conn = (tju_tcp_t *)malloc(sizeof(tju_tcp_t));
                memcpy(new_conn,sock,sizeof(tju_tcp_t));
                new_conn->state = ESTABLISHED;
                connect_queue[queueCnt++] = new_conn;
            }
        }
    }

    else if(sock->state == SYN_SENT){
        if(flag == SYN_FLAG_MASK | ACK_FLAG_MASK){
             sock->state = ESTABLISHED;
             char *msg = create_packet_buf(dst_port, src_port, ack, seq + 1,
                                     DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, ACK_FLAG_MASK, 1, 0, NULL, 0);
             sendToLayer3(msg,DEFAULT_HEADER_LEN);
         }
    }
    if(sock->state == ESTABLISHED){
        if (flag == FIN_ACK_FLAG_MASK){
            uint32_t sseq = sock->window.wnd_send->nextseq;
            uint32_t ack = seq + 1;
            char *msg = create_packet_buf(sock->established_local_addr.port, sock->established_remote_addr.port, sseq, ack,
                                           DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, ACK_FLAG_MASK, 1, 0, NULL, 0);
            sendToLayer3(msg, DEFAULT_HEADER_LEN);
            sock->state = CLOSE_WAIT;
            sleep(1);
            tju_close(sock);
        }
    }
    
    if (sock->state == FIN_WAIT_1){
        if (flag == ACK_FLAG_MASK){
            sock->state = FIN_WAIT_2;
        }

        if (flag == FIN_ACK_FLAG_MASK){
            // 向客户端发送ACK
            uint32_t sseq = sock->window.wnd_send->nextseq + 1;
            uint32_t aack = sseq + 1;
            char *msg = create_packet_buf(sock->established_local_addr.port, sock->established_remote_addr.port, sseq, aack,
                                           DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, ACK_FLAG_MASK, 1, 0, NULL, 0);
            sendToLayer3(msg, DEFAULT_HEADER_LEN);
            sock->state = CLOSING;
        }
    }

    if (sock->state == FIN_WAIT_2){
        if (flag == FIN_ACK_FLAG_MASK){
            uint32_t sseq = ack;
            uint32_t aack = seq + 1;
            char *msg = create_packet_buf(sock->established_local_addr.port, sock->established_remote_addr.port,
                                          sseq, aack, DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, ACK_FLAG_MASK, 1, 0, NULL, 0);
            sendToLayer3(msg, DEFAULT_HEADER_LEN);
            sleep(1);
            sock->state = CLOSED;
        }
    }

    if (sock->state == LAST_ACK){
        if (flag == FIN_ACK_FLAG_MASK){
            sock->state = CLOSED;
        }
    }

    if (sock->state == CLOSING){
        if (flag == ACK_FLAG_MASK){
            sleep(1);
            sock->state = CLOSED;
        }
    }

    if(sock->received_buf == NULL){
        sock->received_buf = malloc(data_len);
    }else {
        sock->received_buf = realloc(sock->received_buf, sock->received_len + data_len);
    }
    memcpy(sock->received_buf + sock->received_len, pkt + DEFAULT_HEADER_LEN, data_len);
    sock->received_len += data_len;

    pthread_mutex_unlock(&(sock->recv_lock)); // 解锁

    return 0;
}

int tju_close (tju_tcp_t* sock){
    uint32_t seq = sock->window.wnd_send->nextseq;
    char *msg = create_packet_buf(sock->established_local_addr.port, sock->established_remote_addr.port, seq, 0,
                                  DEFAULT_HEADER_LEN, DEFAULT_HEADER_LEN, FIN_ACK_FLAG_MASK, 1, 0, NULL, 0);
    sendToLayer3(msg, DEFAULT_HEADER_LEN);
    if (sock->state == ESTABLISHED){
        sock->state = FIN_WAIT_1;
    }
    else if (sock->state == CLOSE_WAIT){
        sock->state = LAST_ACK;
    }
    // 等待四次挥手完成
    while (sock->state != CLOSED){}
    sock = NULL;
    return 0;
}