#include <stdio.h>
#include <stdarg.h>
#include "mfs.h"
#include "vstream-client.h"

#define CHUNK ( 1024 * 1024 )

// on cygwin, we have large file support already, and no fopen64.
#ifdef __CYGWIN__
#define fopen64 fopen
#endif

void vstream_error( const char *format, ... )
{
        va_list va;
        va_start( va, format );
        vfprintf( stdout, format, va );
        va_end( va );
}

int usage(char *name)
{
	printf("Simple Usage:\n");
	printf("list available streams: %s hostname\n", name);
	printf("list available streams (long format): %s hostname -l\n", name);
	printf("download a stream: %s hostname fsid [-o filename] [-a offset]\n", name);
	printf("\n");
	printf("URL Usage:\n");
	printf("list available streams: %s tivo://hostname/list\n", name);
	printf("list available streams (long format): %s tivo://hostname/llist\n", name);
	printf("download a stream: %s tivo://hostname/fsid [-o filename] [-a offset]\n", name);
	printf("\n");
	printf("offsets are specified in megabytes.\n");
	printf("filename, if unspecified, defaults to <fsid>.ty\n");
	printf("\n");
	return 1;
}

int main( int argc, char *argv[] )
{
	int64_t offset = 0, size, count;
	unsigned char buffer[ CHUNK ];
	FILE *output;
	char *filename = NULL, *hostname = NULL, *fsid = NULL;
	int megs, seconds, start_time;
	char *me;
	int dashes = 0, ll = 0;
	
	me = strrchr(argv[0], '/');
	if (!me) me = strrchr(argv[0], '\\'); else me++;
	if (!me) me = argv[0]; else me++;

	argc--;
	argv++;
	while (argc) {
		if (!strcmp(argv[0], "--")) {
			dashes = 1;
		} else if (!dashes && !strcmp(argv[0], "-l")) {
			ll = 1;
		} else if (!dashes && !strcmp(argv[0], "-o")) {
			argv++; argc--;
			if (!argc) return usage(me);
			filename = strdup(argv[0]);
		} else if (!dashes && !strcmp(argv[0], "-a")) {
			argv++; argc--;
			if (!argc) return usage(me);
			offset = ((int64_t)atol(argv[0])) << 20;
		} else if (!strncmp(argv[0], "tivo://", 7)) {
			if (hostname) return usage(me);
			hostname = argv[0] + 7;
			if (!hostname[0]) return usage(me);
			fsid = strchr(hostname, '/');
			if (!fsid) return usage(me);
			*fsid = '\0';
			fsid++;
			if (strchr(fsid, '/')) return usage(me);
		} else {
			if (!hostname) hostname = argv[0];
			else if (hostname && !fsid) fsid = argv[0];
			else return usage(me);
		}
		argc--;
		argv++;
	}

	if (!hostname) return usage(me);
	
	if (vstream_open_socket(hostname)) {
		printf("%s: connection error.\n", me);
		return 1;
	}

	if (!fsid || !strcmp(fsid, "list")) {
		vstream_list_streams(0);
		return 0;
	} else if (!strcmp(fsid, "llist") || ll) {
		vstream_list_streams(1);
		return 0;
	}
	
	if (!filename) {
		int len = strlen(fsid) + 4;
		filename = malloc(len);
		snprintf(filename, len, "%s.ty", fsid);
	}

	{
		if (vstream_start()) {
			printf("%s: Cryptic internal error 1.\n", me);
			return 1;
		}
		if (vstream_startstream(fsid)) {
			printf("%s: Cryptic internal error 2.\n", me);
			return 1;
		}
		size = vstream_streamsize();
		megs = 0;
		printf("(%s) size %lld MB, getting %lld MB\n", fsid, size >> 20, (size - offset) >> 20);
		output = fopen64(filename, "w" );
		if (!output) {
			printf("%s: File creation failed.\n", me);
			return 1;
		}
		start_time = time(NULL);
		while (offset < size)
		{
			count = CHUNK;
			if (offset + CHUNK > size)
				count = size - offset;
			count = vstream_load_chunk(fsid, buffer, count, offset);
			offset += count;
			fwrite(buffer, 1, count, output);
			megs++;
			seconds = time(NULL) - start_time;
			if (seconds) printf("\r%5d MB   %5d kb/s", megs, (megs << 10) / seconds);
			fflush(stdout);
		}
		fclose(output);
		printf("\nDone.\n");
	}
	free(filename);
	return 0;
}

