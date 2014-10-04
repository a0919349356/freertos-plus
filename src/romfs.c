#include <string.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <unistd.h>
#include "fio.h"
#include "filesystem.h"
#include "romfs.h"
#include "osdebug.h"
#include "hash-djb2.h"
#include "clib.h"


struct romfs_fds_t {
    const uint8_t * file;
    uint32_t cursor;
};

static struct romfs_fds_t romfs_fds[MAX_FDS];

static uint32_t get_unaligned(const uint8_t * d) {
    return ((uint32_t) d[0]) | ((uint32_t) (d[1] << 8)) | ((uint32_t) (d[2] << 16)) | ((uint32_t) (d[3] << 24));
}

static int equ_path(const char *path,const uint8_t *d){
	char *loc1;
	char *loc2;
	char seq[500];
	int i;
	int s=6;
	for(i=0;i<(int)get_unaligned(d-4);i++)
	{
		seq[s+i]=d[i];
	}
	seq[s+(int)get_unaligned(d-4)]='\0';
	seq[0]='r';
	seq[1]='o';
	seq[2]='m';
	seq[3]='f';
	seq[4]='s';
	seq[5]='/';
//	fio_printf(1,"\r\nseq test %s\r\n",seq);
//	fio_printf(1,"\r\npath test %s\r\n",path);
	loc1 = strrchr(seq,'/');
	loc2 = strrchr(path,'/');
	*loc1='\0';
	*loc2='\0';
//	fio_printf(1,"\r\nseq test %s\r\n",seq);
//	fio_printf(1,"\r\npath test %s\r\n",path);
	if(strcmp(seq,path)==0)
	{
		*loc1='/';
		*loc2='/';
		return 1;
	}
	else
	{
		*loc1='/';
		*loc2='/';
		return 0;
	}
}


static ssize_t romfs_read(void * opaque, void * buf, size_t count) {
    struct romfs_fds_t * f = (struct romfs_fds_t *) opaque;
    const uint8_t * size_p = f->file - 4;
    uint32_t size = get_unaligned(size_p);
    
    if ((f->cursor + count) > size)
        count = size - f->cursor;

    memcpy(buf, f->file + f->cursor, count);
    f->cursor += count;

    return count;
}

static off_t romfs_seek(void * opaque, off_t offset, int whence) {
    struct romfs_fds_t * f = (struct romfs_fds_t *) opaque;
    const uint8_t * size_p = f->file - 4;
    uint32_t size = get_unaligned(size_p);
    uint32_t origin;
    
    switch (whence) {
    case SEEK_SET:
        origin = 0;
        break;
    case SEEK_CUR:
        origin = f->cursor;
        break;
    case SEEK_END:
        origin = size;
        break;
    default:
        return -1;
    }

    offset = origin + offset;

    if (offset < 0)
        return -1;
    if (offset > size)
        offset = size;

    f->cursor = offset;

    return offset;
}

const uint8_t * romfs_get_file_by_ID(const uint8_t * romfs,const char *path)
{
	const uint8_t *meta;
	//fio_printf(1,"YESSSS!!! %s\r\n",path);
	char seq[500];
	fio_printf(1,"\r\n");
	for(meta = romfs+8;get_unaligned(meta-8) && get_unaligned(meta-4) ;meta+= get_unaligned(meta - 4) +4 + get_unaligned(meta + get_unaligned(meta-4)) + 8 )
	{
		if(equ_path(path,meta))
		{
			
			for(int i=0;i<(int)get_unaligned(meta-4);i++)
			{
				seq[i]=meta[i];
			}
			seq[(int)get_unaligned(meta-4)]='\0';
			fio_printf(1,"  %s  ",seq);
		}
	}
	fio_printf(1,"\r\n");
	return NULL;
}

const uint8_t * romfs_get_file_by_hash(const uint8_t * romfs, uint32_t h, uint32_t * len) {
    const uint8_t * meta;

    for (meta = romfs; get_unaligned(meta) && get_unaligned(meta + 4); meta += get_unaligned(meta + 4) + get_unaligned(meta + 8 + get_unaligned(meta + 4) )  + 12) {
        if (get_unaligned(meta) == h) {
            if (len) {
                *len = get_unaligned(meta + 8 + get_unaligned(meta + 4)) ;
            }
            return meta + get_unaligned(meta + 4) + 12;
        }
    }

    return NULL;
}

static int romfs_open(void * opaque, const char * path, int flags, int mode) {
    uint32_t h = hash_djb2((const uint8_t *) path, -1);
    const uint8_t * romfs = (const uint8_t *) opaque;
    const uint8_t * file;
    int r = -1;

	if(flags==1)
	{
		file = romfs_get_file_by_ID(romfs,path);
		return 1;
	}
	else{
	file = romfs_get_file_by_hash(romfs, h, NULL);
	}
    fio_printf(1,"hash code = %X\r\n",h);
    fio_printf(1,"path = %s\r\n",path);

    if (file) {
        r = fio_open(romfs_read, NULL, romfs_seek, NULL, NULL);
        if (r > 0) {
            romfs_fds[r].file = file;
            romfs_fds[r].cursor = 0;
            fio_set_opaque(r, romfs_fds + r);
        }
    }
    return r;
}

void register_romfs(const char * mountpoint, const uint8_t * romfs) {
//    DBGOUT("Registering romfs `%s' @ %p\r\n", mountpoint, romfs);
    register_fs(mountpoint, romfs_open, (void *) romfs);
}
