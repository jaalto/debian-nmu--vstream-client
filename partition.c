/*
  media-filesystem library, partition routines
  tridge@samba.org, January 2001
  released under the Gnu GPL v2
*/

#include "mfs.h"

#define MAX_PARTITIONS 20
#define PARTITION_MAGIC	0x504d

struct tivo_partition {
	u16	signature;	/* expected to be PARTITION_MAGIC */
	u16	res1;
	u32	map_count;	/* # blocks in partition map */
	u32	start_block;	/* absolute starting block # of partition */
	u32	block_count;	/* number of blocks in partition */
	char	name[32];	/* partition name */
	char	type[32];	/* string type description */
	u32	data_start;	/* rel block # of first data block */
	u32	data_count;	/* number of data blocks */
	u32	status;		/* partition status bits */
	u32	boot_start;
	u32	boot_size;
	u32	boot_load;
	u32	boot_load2;
	u32	boot_entry;
	u32	boot_entry2;
	u32	boot_cksum;
	char	processor[16];	/* identifies ISA of boot */
	/* there is more stuff after this that we don't need */
};

struct pmap {
	u32 start;
	u32 length;
} pmaps[MAX_PARTITIONS];

static int num_partitions = 0;
static int use_ptable = 0;

/* parse the tivo partition table */
int vstream_partition_parse(void)
{
	char buf[SECTOR_SIZE];
	struct tivo_partition *tp;
	int i, count;

	tp = (struct tivo_partition *)buf;

	vserver_vstream_read_sectors(tp, 1, 1);

	count = ntohl(tp->map_count);

	for (i=0;i<count;i++) {
		vserver_vstream_read_sectors(tp, i+1, 1);
		if (ntohs(tp->signature) != PARTITION_MAGIC) {
			vstream_error("wrong magic %x in partition %d\n",
			       ntohs(tp->signature), i);
			return 1;
		}
		if (strcmp(tp->type, "MFS") == 0) {
			pmaps[num_partitions].start = ntohl(tp->start_block);
			pmaps[num_partitions].length = ntohl(tp->block_count);
			num_partitions++;
		}
	}
	use_ptable = 1;
	return 0;
}

u32 vstream_partition_total_size(void)
{
	u32 total=0;
	int i;

	if (!use_ptable) return 0;

	for (i=0; i<num_partitions; i++) {
		total += (pmaps[i].length & ~(MFS_BLOCK_ROUND-1));
	}

	return total;
}
