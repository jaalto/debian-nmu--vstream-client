/*
  media-filesystem library
  tridge@samba.org, January 2001
  released under the Gnu GPL v2
*/

#include "mfs.h"

static struct mfs_super super;
static struct mfs_zone_map *zones[MAX_ZONES];
static int num_zones = 0;

static int total_inodes = 0;

unsigned vstream_fsid_hash(unsigned fsid, unsigned size)
{
	return fsid*67289 % size;
}

// returns 1 on failure
int load_super(void)
{
	char buffer[100*SECTOR_SIZE];

	vstream_mfs_read_partial(&super, 0, sizeof(super));

	if ((super.state & 0xffff) == 0x1492 || (super.state & 0xffff) == 0x9214) {
		if ((super.state & 0xffff) == 0x1492) vstream_io_need_bswap(1);
		if (vstream_partition_parse()) return 1;
		vstream_mfs_read_partial(&super, 0, sizeof(super));
	}

	switch (super.magic) {
	case 0xabbafeed: /* normal tivo access */
		break;
	case 0xbaabedfe:
		vstream_io_need_bswap(1);
		break;
	case 0xedfebaab:
		break;
	case 0xfeedabba:
		vstream_io_need_bswap(1);
		break;
	default:
		vstream_error("Not a TiVo super block! (magic=0x%08x)\n", super.magic);
		return 1;
	}

	vstream_mfs_read_partial(buffer, 0, sizeof(super));
	memcpy(&super, buffer, sizeof(super));

	vstream_check_crc((void *)&super, sizeof(super) - 512, &super.crc);
	
	vstream_byte_swap(&super, "i9 b128 i87");

	if (super.magic != 0xabbafeed) {
		vstream_error("Failed to byte swap correctly\n");
		return 1;
	}

	if (vstream_partition_total_size() && 
      vstream_partition_total_size() != super.total_sectors) {
		vstream_error("WARNING: total sectors doesn't match (total=%d sb=%d)\n",
		       vstream_partition_total_size(), super.total_sectors);
	}

	return 0;
}

/* load the mfs zones - currently we only use the inode
   zone but might as well load the lot */
int load_zones(void)
{
	u32 next = super.zonemap_ptr;
	u32 map_size = super.zonemap_size;
	total_inodes = 0;

	while (next) {
		zones[num_zones] = (struct mfs_zone_map *)malloc(SECTOR_SIZE*map_size);
		vserver_vstream_read_sectors(zones[num_zones], next, map_size);
		vstream_check_crc(zones[num_zones], map_size*SECTOR_SIZE, &zones[num_zones]->crc);
		vstream_byte_swap(zones[num_zones], "i18");
		if (next != zones[num_zones]->sector) {
			vstream_error("sector wrong in zone (%d %d)\n",
				next, zones[num_zones]->sector);
			return 1;
		}
		if (zones[num_zones]->type == ZONE_INODE) {
			total_inodes += zones[num_zones]->zone_size/2;
		}
		next = zones[num_zones]->next_zonemap_ptr;
		map_size = zones[num_zones]->next_zonemap_size;
		num_zones++;
		if (num_zones == MAX_ZONES) {
			vstream_error("Too many zones\n");
			return 1;
		}
	}
	return 0;
}

/* turn a hash into a zone sector */
static u32 zone_sector(u32 hash)
{
	int i;
	u32 start = 0;
	for (i=0;i<num_zones;i++) {
		u32 len;
		if (zones[i]->type != ZONE_INODE) continue;
		len = zones[i]->zone_size/2;
		if (hash < start + len) {
			return zones[i]->zone_start + (hash-start)*2;
		}
		start += len;
	}
	vstream_error("Didn't find hash %x in zones!\n", hash);
	return 0xffffffff;
}

/* load one inode by fsid - optimised to avoid repeats */
static int vstream_mfs_load_inode(int fsid, struct mfs_inode *inode)
{
	static struct mfs_inode in;
	static u32 last_fsid;
	unsigned hash, hash1;

	if (fsid && fsid == last_fsid) {
		*inode = in;
		return 0;
	}

	hash1 = hash = vstream_fsid_hash(fsid, total_inodes);
	do {
		u32 zs = zone_sector(hash);
		if (zs == 0xffffffff) return 1;
		vstream_mfs_read_partial(&in, zs, sizeof(in));
		vstream_byte_swap(&in, "i10 b2 s1 i4");
		if (in.num_runs) {
			// cwingert There is more than 24 runs, so just use the
		   // maximum space available
			// vstream_byte_swap(&in.u.runs[0], "i48");
			vstream_byte_swap(&in.u.runs[0], "i112");
		}
		hash = (hash+1) % total_inodes;
	} while ((in.flags & MFS_FLAGS_CHAIN) && in.id != fsid && hash != hash1);

	if (in.id != fsid) {
		vstream_error("ERROR: Didn't find fsid=%d!\n", fsid);
		return 1;
	}

	*inode = in;
	last_fsid = fsid;

	return 0;
}

/* read count bytes from a mfs file at offset ofs,
   returning the number of bytes read 
   ofs must be on a sector boundary
*/
u32 vstream_mfs_fsid_pread(int fsid, void *buf, u64 ofs, u32 count)
{
	struct mfs_inode inode;
	int i, n;
	u32 start;
	u32 ret=0;
	u32 sec = ofs >> SECTOR_SHIFT;
	u64 size;

	if (vstream_mfs_load_inode(fsid, &inode)) return 0;

	if (inode.num_runs == 0) {
		if (ofs >= inode.size) return 0;
		ret = count;
		if (ret > inode.size-ofs) {
			ret = inode.size-ofs;
		}
		memcpy(buf, inode.u.data, ret);
		return ret;
	}

	size = inode.size;
	if (inode.units == 0x20000) {
		size <<= 17;
	}

	if (ofs > size) return 0;
	if (ofs + count > size) {
		count = size-ofs;
	}

	while (count > 0) {
		void *buf2;
		u32 n2;
		start = 0;
		for (i=0; i<inode.num_runs; i++) {
			if (sec < start + inode.u.runs[i].len) break;
			start += inode.u.runs[i].len;
		}
		if (i == inode.num_runs) return ret;
		n = (count+(SECTOR_SIZE-1))>>SECTOR_SHIFT;
		if (n > inode.u.runs[i].len-(sec-start)) {
			n = inode.u.runs[i].len-(sec-start);
		}
		buf2 = malloc(n<<SECTOR_SHIFT);
		vserver_vstream_read_sectors(buf2, inode.u.runs[i].start+(sec-start), n);
		n2 = n<<SECTOR_SHIFT;
		if (n2 > count) n2 = count;
		memcpy(buf, buf2, n2);
		free(buf2);
		buf += n2;
		sec += n;
		count -= n2;
		ret += n2;
	}
	return ret;
}

/* return the type of a fsid */
int vstream_mfs_fsid_type(int fsid)
{
	struct mfs_inode inode;
	if (fsid == 0) return 0;
	memset(&inode, 0, sizeof(inode));
	if (vstream_mfs_load_inode(fsid, &inode)) return 0;
	return inode.type;
}

/* return the number of bytes used by a fsid */
u64 vstream_mfs_fsid_size(int fsid)
{
	struct mfs_inode inode;

	if (fsid == 0) return 0;
	if (vstream_mfs_load_inode(fsid, &inode)) return 0;

	switch (inode.units) {
	case 0: return inode.size;
	case 0x20000: return ((u64)inode.size) << 17;
	}
	vstream_error("ERROR: fsid=%d Unknown units %d\n", fsid, inode.units);
	return inode.size;
}

/* list a mfs directory - make sure you free with vstream_mfs_dir_free() */
struct mfs_dirent *mfs_dir(int fsid, u32 *count)
{
	u32 *buf, *p;
	int n=0, i;
	int size = vstream_mfs_fsid_size(fsid);
	int dsize, dflags;
	struct mfs_dirent *ret;

	*count = 0;

	if (size < 4) return NULL;

	if (vstream_mfs_fsid_type(fsid) != MFS_TYPE_DIR) {
		vstream_error("fsid %d is not a directory\n", fsid);
		return NULL;
	}

	buf = (u32 *)malloc(size);
	vstream_mfs_fsid_pread(fsid, buf, 0, size);
	dsize = ntohl(buf[0]) >> 16;
	dflags = ntohl(buf[0]) & 0xFFFF;
	p = buf + 1;
	while ((int)(p-buf) < dsize/4) {
		u8 *s = ((char *)p)+4;
		p += s[0]/4;
		n++;
	}
	ret = malloc((n+1)*sizeof(*ret));
	p = buf + 1;
	for (i=0;i<n;i++) {
		u8 *s = ((char *)p)+4;
		ret[i].name = strdup(s+2);
		ret[i].type = s[1];
		ret[i].fsid = ntohl(p[0]);
		p += s[0]/4;
	}	
	ret[n].name = NULL;
	free(buf);
	*count = n;

	/* handle meta-directories. These are just directories which are
	   lists of other directories. All we need to do is recursively read
	   the other directories and piece together the top level directory */
	if (dflags == 0x200) {
		struct mfs_dirent *meta_dir = NULL;
		int meta_size=0;

		*count = 0;

		for (i=0;i<n;i++) {
			struct mfs_dirent *d2;
			u32 n2;
			if (ret[i].type != MFS_TYPE_DIR) {
				vstream_error("ERROR: non dir %d/%s in meta-dir %d!\n", 
				       ret[i].type, ret[i].name, fsid);
				continue;
			}
			d2 = mfs_dir(ret[i].fsid, &n2);
			if (!d2 || n2 == 0) continue;
			meta_dir = realloc(meta_dir, sizeof(ret[0])*(meta_size + n2 + 1));
			memcpy(meta_dir+meta_size, d2, n2*sizeof(ret[0]));
			meta_size += n2;
			free(d2);
		}
		vstream_mfs_dir_free(ret);
		if (meta_dir) meta_dir[meta_size].name = NULL;
		*count = meta_size;
		return meta_dir;
	}


	return ret;
}

/* free a dir from mfs_dir */
void vstream_mfs_dir_free(struct mfs_dirent *dir)
{
	int i;
	for (i=0; dir[i].name; i++) {
		free(dir[i].name);
		dir[i].name = NULL;
	}
	free(dir);
}

/* resolve a path to a fsid */
u32 vstream_mfs_resolve(char *path)
{
	char *p0, *tok, *r;
	u32 fsid;
	struct mfs_dirent *dir = NULL;

	if (path[0] != '/') {
		return atoi(path);
	}

	fsid = MFS_ROOT_FSID;
	p0 = strdup(path);
	path = p0;
	for (tok=strtok_r(path,"/", &r); tok; tok=strtok_r(NULL,"/", &r)) {
		u32 count;
		int i;
		dir = mfs_dir(fsid, &count);
		if (!dir) {
			vstream_error("resolve failed for fsid=%d\n", fsid);
			return 0;
		}
		for (i=0;i<count;i++) {
			if (strcmp(tok, dir[i].name) == 0) break;
		}
		if (i == count) {
			fsid = 0;
			goto done;
		}
		fsid = dir[i].fsid;
		if (dir[i].type != MFS_TYPE_DIR) {
			if (strtok_r(NULL, "/", &r)) {
				vstream_error("not a directory %s\n",tok);
				fsid = 0;
				goto done;
			}
			goto done;
		}
		vstream_mfs_dir_free(dir);
		dir = NULL;
	}

 done:
	if (dir) vstream_mfs_dir_free(dir);
	if (p0) free(p0);
	return fsid;
}

