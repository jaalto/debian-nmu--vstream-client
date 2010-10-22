#ifndef _VSTREAM_H
#define _VSTREAM_H

#include <inttypes.h>

int vstream_start( );
int vstream_startstream( char *fsid );
int64_t vstream_streamsize( );
void vstream_list_streams( int longList );
int vstream_load_chunk( char *fsid, unsigned char *buff, int size, int64_t offset );
void vstream_set_socket_fd( int fd );
int vstream_open_socket( char *addy );

// you must implement this if you link against this library:
void vstream_error( const char *format, ... );

#define VSERVER_PORT 8074

#endif
