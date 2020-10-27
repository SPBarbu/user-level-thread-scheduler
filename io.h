/**
 *  @author: Trung Vuong Thien
 *  @email: trung.vuongthien@mail.mcgill.ca
 *  @description: Header file for socket wrapper functions
 */

#include <arpa/inet.h>
#include <sys/socket.h>

 /**
  *  Connect to a server.
  *
  *  @params:
  *    host:     A string representing the host address of the server.
  *    port:     An integer in the range [0, 65536) representing the port.
  *    sockfd:   The file descriptor associated with the socket
  *  @return:    On success, the fiel descriptor associated with the newly
  *              created socket is assigned to sockfd, and 0 is returned.
  *              If the function fails to set up the socket, -1 is returned.
  */
int connect_to_server(const char* host, uint16_t port, int* sockfd);

/**
 *  Send a message on a socket.
 *
 *  @params:
 *    sockfd:   The file descriptor of the socket.
 *    buf:      The message to send.
 *    len:      Number of bytes to send.
 *  @return:    On success, the functions returns the number of bytes send.
 *              On error, -1 is returned.
 */
ssize_t send_message(int sockfd, const char* buf, size_t len);

/**
 *  Receive a message from a socket.
 *
 *  @params:
 *    sockfd:   The file descriptor of the socket.
 *    buf:      A buffer to store the received message.
 *    len:      The size of the buffer.
 *  @return:    On success, the function returns he number of bytes received.
 *              A value of 0 means the connection on this socket has been
 *              closed. On error, -1 is returned.
 */
ssize_t recv_message(int sockfd, char* buf, size_t len);