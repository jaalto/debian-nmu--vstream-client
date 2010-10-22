/*
  media-filesystem object schema code
  tridge@samba.org, January 2001
  released under the Gnu GPL v2
*/

#include "mfs.h"
#include "preload_schema.h"
#include <assert.h>

#define MAX_TYPES 200
#define MAX_ATTRS 100

static int current_type;
static char *types[MAX_TYPES];

static struct {
	char *name;
	int objtype;
} attrs[MAX_TYPES][MAX_ATTRS];

static void schema_add(int type, int attr, char *name, int objtype)
{
	int i;

	assert(type < MAX_TYPES);
	assert(attr < MAX_ATTRS);

	name = strdup(name);
	if (type != -1) {
		attrs[type][attr].name = name;
		attrs[type][attr].objtype = objtype;
		return;
	}
	for (i=0;i<MAX_TYPES;i++) {
		attrs[i][attr].name = name;
		attrs[i][attr].objtype = objtype;
	}
}

/* used to load a local schema.txt to make things faster */
static int preload_schema()
{
	int i, itype, iattr, atype=0;
	char *type, *attr, *flag;
	static int loaded = 0;
	
	if (loaded) return 1;

	loaded = 1;

	for(i = 0; i < sizeof(preload_schema_table) / sizeof(preload_schema_t); i++) {
		itype = preload_schema_table[i].itype;
		type = preload_schema_table[i].type;
		iattr = preload_schema_table[i].iattr;
		attr = preload_schema_table[i].attr;
		flag = preload_schema_table[i].attr_type;

		if (!types[itype]) types[itype] = strdup(type);
		attrs[itype][iattr].name = strdup(attr);
		if (strcmp(flag,"string")==0) {
			atype = TYPE_STRING;
		} else if (strcmp(flag,"int")==0) {
			atype = TYPE_INT;
		} else if (strcmp(flag,"object")==0) {
			atype = TYPE_OBJECT;
		} else if (strcmp(flag,"file")==0) {
			atype = TYPE_FILE;
		} else {
		    vstream_error("error parsing preloaded schema table\n");
		    return 0;
		}
		if (! types[itype]) {
		    types[itype] = strdup(type);
		}
		schema_add(itype, iattr, attr, atype);
	}

	return 1;
}

void load_callback2(int fsid, struct mfs_subobj_header *obj,
		    struct mfs_attr_header *attr, void *data)
	{
		static char *name;
		static u32 iattr;
		if (obj->flags || !attr) return;
		if (attr->attr == 16) {
			name = (char *)data;
		} else if (attr->attr == 17) {
			iattr = ntohl(*(u32 *)data);
		} else if (attr->attr == 18) {
			u32 objtype = ntohl(*(u32 *)data);
			schema_add(current_type, iattr, name, objtype);
		}
	}

void load_callback1(int fsid, struct mfs_subobj_header *obj,
		    struct mfs_attr_header *attr, void *data)
	{
		static char *name;
		if (!obj->flags || !attr) return;
		if (attr->attr == 16) {
			name = strdup((char *)data);
		} else if (attr->attr == 17) {
			current_type = ntohl(*(u32 *)data);
			types[current_type] = name;
		}
	}


static void load_schema(int type)
{
	u32 size, fsid;
	char path[100];
	void *buf;

	if (!attrs[0][1].name) {
		schema_add(-1, 1, "Version", TYPE_INT);
		schema_add(-1, 2, "Expiration", TYPE_INT);
		schema_add(-1, 3, "Path", TYPE_STRING);
		schema_add(-1, 4, "IndexPath", TYPE_STRING);
		schema_add(-1, 5, "IndexUsed", TYPE_OBJECT);
		schema_add(-1, 6, "IndexUsedBy", TYPE_OBJECT);
		schema_add(-1, 7, "IndexAttr", TYPE_OBJECT);
		schema_add(-1, 8, "ServerId", TYPE_STRING);
		schema_add(-1, 9, "ServerVersion", TYPE_INT);
		schema_add(-1, 10, "Uuid", TYPE_STRING);
		schema_add(-1, 11, "Unsatisfied", TYPE_STRING);
		schema_add(-1, 12, "Bits", TYPE_INT);
		schema_add(-1, 13, "Unused1", TYPE_INT);
		schema_add(-1, 14, "Unused2", TYPE_INT);
		schema_add(-1, 15, "Unused3", TYPE_INT);
	}

	sprintf(path, "/ObjectType/%s", vstream_schema_type(type));

	fsid = vstream_mfs_resolve(path);
	if (fsid != 0) {
	    size = vstream_mfs_fsid_size(fsid);
	    if (size > 0) {
		    buf = malloc(size);
		    vstream_mfs_fsid_pread(fsid, buf, 0, size);
		    vstream_parse_object(fsid, buf, load_callback1);
		    vstream_parse_object(fsid, buf, load_callback2);
		    free(buf);
	    }
	}
}

/* lookup a string for a schema type */
char *vstream_schema_type(int type)
{
	if (!types[type]) preload_schema();

	return types[type];
}

/* lookup an attribute for a given type and attr value,
   auto-loading the schema if necessary */
char *vstream_schema_attrib(int type, int attr)
{
	(void) vstream_schema_type(type);
	if (type >= MAX_TYPES) {
		vstream_error("Invalid type %d in vstream_schema_attrib\n", type);
		return "UNKNOWN";
	}
	if (attr >= MAX_ATTRS) {
		vstream_error("Invalid attr %d in vstream_schema_attrib\n", attr);
		return "UNKNOWN";
	}
	if (!attrs[type][attr].name) {
		load_schema(type);
	}
	if (!attrs[type][attr].name) {
		attrs[type][attr].name = malloc(18);
		snprintf(attrs[type][attr].name, 18, "UNKNOWN(%d,%d)", type, attr);
	}
	return attrs[type][attr].name;
}

