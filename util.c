/*
  media-filesystem library
  tridge@samba.org, January 2001
  released under the Gnu GPL v2
*/

#include "mfs.h"
#ifdef __MINGW32__
# define HAVE_WINSOCK2
# include <winsock2.h>
#else
# include <sys/socket.h>
# include <netdb.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# define closesocket(f) close(f)
#endif

void vstream_read_all(int fd, void *buf, int size)
{
	while (size) {
		int n = read(fd, buf, size);
		if (n <= 0) return; // FIXME
		buf += n;
		size -= n;
	}
}

void vstream_write_all(int fd, void *buf, int size)
{
	while (size) {
		int n = write(fd, buf, size);
		if (n <= 0) return; // FIXME
		buf += n;
		size -= n;
	}
}


// stolen from MPlayer:
// Connect to a server using a TCP connection, with specified address family
// return -2 for fatal error, like unable to resolve name, connection timeout...
// return -1 is unable to connect to a particular port

#define mp_msg(a, ...)
#define mp_input_check_interrupt(a) 0
#define USE_ATON 1

static int connect2Server_with_af(char *host, int port, int af, int verb) {
	int socket_server_fd;
	int err, err_len;
	int ret,count = 0;
	fd_set set;
	struct timeval tv;
	union {
		struct sockaddr_in four;
#ifdef HAVE_AF_INET6
		struct sockaddr_in6 six;
#endif
	} server_address;
	size_t server_address_size;
	void *our_s_addr;	// Pointer to sin_addr or sin6_addr
	struct hostent *hp=NULL;
	char buf[255];
	
#ifdef HAVE_WINSOCK2
	u_long val;
#endif
	
	socket_server_fd = socket(af, SOCK_STREAM, 0);
	
	
	if( socket_server_fd==-1 ) {
//		mp_msg(MSGT_NETWORK,MSGL_ERR,"Failed to create %s socket:\n", af2String(af));
		return -2;
	}

	switch (af) {
		case AF_INET:  our_s_addr = (void *) &server_address.four.sin_addr; break;
#ifdef HAVE_AF_INET6
		case AF_INET6: our_s_addr = (void *) &server_address.six.sin6_addr; break;
#endif
		default:
			mp_msg(MSGT_NETWORK,MSGL_ERR, "Unknown address family %d:\n", af);
			return -2;
	}
	
	
	memset(&server_address, 0, sizeof(server_address));
	
#ifndef HAVE_WINSOCK2
#ifdef USE_ATON
	if (inet_aton(host, our_s_addr)!=1)
#else
	if (inet_pton(af, host, our_s_addr)!=1)
#endif
#else
	if ( inet_addr(host)==INADDR_NONE )
#endif
	{
		if(verb) mp_msg(MSGT_NETWORK,MSGL_STATUS,"Resolving %s for %s...\n", host, af2String(af));
		
#ifdef HAVE_GETHOSTBYNAME2
		hp=(struct hostent*)gethostbyname2( host, af );
#else
		hp=(struct hostent*)gethostbyname( host );
#endif
		if( hp==NULL ) {
			if(verb) mp_msg(MSGT_NETWORK,MSGL_ERR,"Couldn't resolve name for %s: %s\n", af2String(af), host);
			return -2;
		}
		
		memcpy( our_s_addr, (void*)hp->h_addr, hp->h_length );
	}
#ifdef HAVE_WINSOCK2
	else {
		unsigned long addr = inet_addr(host);
		memcpy( our_s_addr, (void*)&addr, sizeof(addr) );
	}
#endif
	
	switch (af) {
		case AF_INET:
			server_address.four.sin_family=af;
			server_address.four.sin_port=htons(port);			
			server_address_size = sizeof(server_address.four);
			break;
#ifdef HAVE_AF_INET6		
		case AF_INET6:
			server_address.six.sin6_family=af;
			server_address.six.sin6_port=htons(port);
			server_address_size = sizeof(server_address.six);
			break;
#endif
		default:
			mp_msg(MSGT_NETWORK,MSGL_ERR, "Unknown address family %d:\n", af);
			return -2;
	}

#if defined(USE_ATON) || defined(HAVE_WINSOCK2)
	strncpy( buf, inet_ntoa( *((struct in_addr*)our_s_addr) ), 255);
#else
	inet_ntop(af, our_s_addr, buf, 255);
#endif
	if(verb) mp_msg(MSGT_NETWORK,MSGL_STATUS,"Connecting to server %s[%s]:%d ...\n", host, buf , port );

	// Turn the socket as non blocking so we can timeout on the connection
#ifndef HAVE_WINSOCK2
	fcntl( socket_server_fd, F_SETFL, fcntl(socket_server_fd, F_GETFL) | O_NONBLOCK );
#else
	val = 1;
	ioctlsocket( socket_server_fd, FIONBIO, &val );
#endif
	if( connect( socket_server_fd, (struct sockaddr*)&server_address, server_address_size )==-1 ) {
#ifndef HAVE_WINSOCK2
		if( errno!=EINPROGRESS ) {
#else
		if( (WSAGetLastError() != WSAEINPROGRESS) && (WSAGetLastError() != WSAEWOULDBLOCK) ) {
#endif
			if(verb) mp_msg(MSGT_NETWORK,MSGL_ERR,"Failed to connect to server with %s\n", af2String(af));
			closesocket(socket_server_fd);
			return -1;
		}
	}
	tv.tv_sec = 0;
	tv.tv_usec = 500000;
	FD_ZERO( &set );
	FD_SET( socket_server_fd, &set );
	// When the connection will be made, we will have a writable fd
	while((ret = select(socket_server_fd+1, NULL, &set, NULL, &tv)) == 0) {
	      if( ret<0 ) mp_msg(MSGT_NETWORK,MSGL_ERR,"select failed\n");
	      else if(ret > 0) break;
	      else if(count > 30 || mp_input_check_interrupt(500)) {
		if(count > 30)
		  mp_msg(MSGT_NETWORK,MSGL_ERR,"Connection timeout\n");
		else
		  mp_msg(MSGT_NETWORK,MSGL_V,"Connection interuppted by user\n");
		return -3;
	      }
	      count++;
	      FD_ZERO( &set );
	      FD_SET( socket_server_fd, &set );
	      tv.tv_sec = 0;
	      tv.tv_usec = 500000;
	}

	// Turn back the socket as blocking
#ifndef HAVE_WINSOCK2
	fcntl( socket_server_fd, F_SETFL, fcntl(socket_server_fd, F_GETFL) & ~O_NONBLOCK );
#else
	val = 0;
	ioctlsocket( socket_server_fd, FIONBIO, &val );
#endif
	// Check if there were any error
	err_len = sizeof(int);
	ret =  getsockopt(socket_server_fd,SOL_SOCKET,SO_ERROR,&err,&err_len);
	if(ret < 0) {
		mp_msg(MSGT_NETWORK,MSGL_ERR,"getsockopt failed : %s\n",strerror(errno));
		return -2;
	}
	if(err > 0) {
		mp_msg(MSGT_NETWORK,MSGL_ERR,"Connect error : %s\n",strerror(err));
		return -1;
	}
	
	return socket_server_fd;
}


int vstream_vstream_open_socket_out(char *host, int port)
{
#ifdef __MINGW32__
  static int winsock_ready = 0;
  if (!winsock_ready) {
    WSADATA data;
    WSAStartup(514, &data);
    winsock_ready = 1;
  }
#endif
  return connect2Server_with_af(host, port, AF_INET, 0);
}

void vstream_byte_swap(void *p, char *desc)
{
	int n, i;
	while (*desc) {
		switch (*desc) {
		case 'i': {
			u32 *v;
			n = strtol(desc+1, &desc, 10);
			v = p;
			for (i=0;i<n;i++) v[i] = ntohl(v[i]);
			p = (void *)(v+n);
			break;
		}

		case 's': {
			u16 *v;
			n = strtol(desc+1, &desc, 10);
			v = p;
			for (i=0;i<n;i++) v[i] = ntohs(v[i]);
			p = (void *)(v+n);
			break;
		}

		case 'b': {
			n = strtol(desc+1, &desc, 10);
			p += n;
			break;
		}
		}
		while (*desc == ' ') desc++;
	}
}

