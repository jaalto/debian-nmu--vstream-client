/*
  media-filesystem library, io routines
  tridge@samba.org, January 2001
  released under the Gnu GPL v2
*/

#include "mfs.h"

static int readahead_enabled = 0;
static int vserver = -1;
static int need_bswap = 0;

void vstream_mfs_readahead(int set)
{
	readahead_enabled = set;
}

static void vserver_read_req(u32 sec, u32 count)
{
	struct vserver_cmd cmd;

	cmd.command = htonl(MFS_CMD_READ);
	cmd.param1 = htonl(sec);
	cmd.param2 = htonl(count);
	vstream_write_all(vserver, &cmd, sizeof(cmd));
}

static void vserver_receive(void *buf, u32 count)
{
	count <<= SECTOR_SHIFT;
	vstream_read_all(vserver, buf, count);
}

#define RA_BLOCKS	256
#define RA_MIN		256

void vserver_vstream_read_sectors(void *buf, u32 sec, u32 count)
{
	static struct mfs_run readahead;
	u32 discard, coming;

	if (count == 0)
		return;

	discard = coming = 0;
	if (sec >= readahead.start && sec < readahead.start + readahead.len) {
		discard = sec - readahead.start;
		coming = readahead.len - discard;
		if (coming <= count) {
			readahead.len = 0;
		} else {
			readahead.start = sec + count;
			readahead.len -= discard + count;
		}
	} else {
		discard = readahead.len;
		coming = 0;
		readahead.len = 0;
	}

	if (coming < count) {
		u32 nreq = count - coming;
		if (readahead_enabled && nreq < RA_BLOCKS) {
			readahead.start = sec + count;
			readahead.len = RA_BLOCKS - nreq;
			nreq = RA_BLOCKS;
		}
		vserver_read_req(sec + coming, nreq);
	}

	if (readahead.len <= RA_MIN && readahead_enabled) {
		if (readahead.len == 0)
			readahead.start = sec + count;
		vserver_read_req(readahead.start + readahead.len, RA_BLOCKS);
		readahead.len += RA_BLOCKS;
	}

	if (discard) {
		void *buf2 = malloc(discard << SECTOR_SHIFT);
		vserver_receive(buf2, discard);
		free(buf2);
	}

	vserver_receive(buf, count);

	if (need_bswap) {
		u16 *v = (u16 *)buf;
		int i;
		for (i=0;i<count<<(SECTOR_SHIFT-1);i++)
			v[i] = ntohs(v[i]);
	}
}

void vstream_io_need_bswap(int set)
{
	need_bswap = set;
}

/* read bytes from a sector, handling partial sectors */
void vstream_mfs_read_partial(void *buf, u32 sec, u32 size)
{
	char tmp[SECTOR_SIZE];
	if (size >= SECTOR_SIZE) {
		u32 n = size / SECTOR_SIZE;
		vserver_vstream_read_sectors(buf, sec, n);
		buf += n*SECTOR_SIZE;
		size -= n*SECTOR_SIZE;
		sec += n;
	}
	if (size == 0) return;
	vserver_vstream_read_sectors(tmp, sec, 1);
	memcpy(buf, tmp, size);
}

void vstream_set_socket_fd(int fd)
{
	vserver = fd;
}

int vstream_open_socket(char *addy)
{
	if (vserver < 0)
		vserver = vstream_vstream_open_socket_out(addy, VSERVER_PORT);
	if (vserver < 0) {
		vstream_error("Failed to connect to %s\n", addy);
		return 1;
	}
	return 0;
}

