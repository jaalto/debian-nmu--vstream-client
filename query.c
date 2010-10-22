/*
  media-filesystem object query code
  tridge@samba.org, January 2001
  released under the Gnu GPL v2
*/

#include "mfs.h"

static struct {
	int fsid;
	void *buf;
	int size;
} loaded;

static void load_object(int fsid)
{
	if (fsid == loaded.fsid) return;

	if (loaded.buf) free(loaded.buf);

	loaded.size = vstream_mfs_fsid_size(fsid);
	loaded.buf = malloc(loaded.size);
	loaded.fsid = fsid;
	vstream_mfs_fsid_pread(fsid, loaded.buf, 0, loaded.size);
}

static int subobj_g=0;
static char *name_g=NULL;
static int *len_g=NULL;
static void *ret_g=NULL;

static void callback(int fsid, struct mfs_subobj_header *obj,
      struct mfs_attr_header *attr, void *data)
{
	if (!attr) return;
	if (!(obj->flags && subobj_g == -1) && !(obj->id == subobj_g)) return;
	if (strcmp(vstream_schema_attrib(obj->obj_type, attr->attr), name_g)) return;
	*len_g = attr->len;
	ret_g = data;
}

/* return the data portion of a part of an object */
void *vstream_query_part(int fsid, int subobj, char *name, int *len)
{
	if (vstream_mfs_fsid_type(fsid) != MFS_TYPE_OBJ) {
		vstream_error("%d is not an object\n", fsid);
		return NULL;
	}

	load_object(fsid);
	subobj_g = subobj;
	name_g = name;
	len_g = len;
	vstream_parse_object(fsid, loaded.buf, callback);
   
	return ret_g;
}

/* query a subobject path starting at fsid returning the data in the
   tail of the path */
static int vstream_myisdigit( char p )
{
   if ( ( p >= '0' ) && ( p <= '9' ) )
      return( 1 );
   return( 0 );
}

void *vstream_query_path(int fsid, char *path, int *len)
{
	char *tok, *p;
	void *ret=NULL;
	int subobj = -1;
	
	path = strdup(path);
	tok = strtok_r(path,"/", &p);

	while (tok) {
		ret = vstream_query_part(fsid, subobj, tok, len);
		if (!ret) return NULL;
		tok = strtok_r(NULL,"/", &p);
		if (tok && vstream_myisdigit(tok[0])) {
			subobj = atoi(tok);
			tok = strtok_r(NULL,"/", &p);
		} else if (tok) {
			struct mfs_obj_attr *objattr = ret;
			fsid = ntohl(objattr->fsid);
			subobj = ntohl(objattr->subobj);
		}
	}
	free(path);
	return ret;
}

char *vstream_query_string(int fsid, char *path)
{
	int len;
	char *p = vstream_query_path(fsid, path, &len);
	return p;
}

int vstream_query_int(int fsid, char *path)
{
	int len;
	int *p = vstream_query_path(fsid, path, &len);
	if (!p) return -1;
	return ntohl(*p);
}

struct mfs_obj_attr *query_object(int fsid, char *path, int *count)
{
	int len, i;
	struct mfs_obj_attr *ret = NULL;
	struct mfs_obj_attr *p = vstream_query_path(fsid, path, &len);
	if (!p) return ret;
	*count = (len-4)/8;
	ret = calloc(*count, sizeof(*ret));
	for (i=0;i<*count;i++) {
		ret[i] = p[i];
		ret[i].fsid = ntohl(ret[i].fsid);
		ret[i].subobj = ntohl(ret[i].subobj);
	}
	return ret;
}

