/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-orientation.h"
#include "gkd-fps-counter.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <linux/memfd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <zlib.h>
#define ROM_MAX (16U*1024U*1024U)
#define ZIP_MAX (32U*1024U*1024U)
static unsigned u16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t u32(const unsigned char *p){return u16(p)|(uint32_t)u16(p+2)<<16;}
static int read_at(int fd,void *p,size_t n,off_t offset)
{
    while(n){ssize_t got=pread(fd,p,n,offset);if(got<0&&errno==EINTR)continue;
        if(got<=0)return -1;
        p=(unsigned char *)p+got;n-=(size_t)got;offset+=got;}
    return 0;
}
static int extension(const char *p,int lynx)
{
    const char *dot=strrchr(p,'.');
    return dot&&(lynx?!strcasecmp(dot,".lnx"):(!strcasecmp(dot,".ws")||!strcasecmp(dot,".wsc")));
}
/* Single-ROM ZIP, no disk extraction. Bounded directory/input/output sizes;
 * exact CRC, sizes and deflate EOF are required. Unsupported input is unknown. */
static unsigned char *zip_rom(int fd,size_t file_size,int lynx,size_t *size)
{
    unsigned char end[22],entry[46],local[30];char name[513],local_name[513];
    unsigned char tail[65557];
    if(file_size<22)return NULL;
    size_t tail_bytes=file_size<sizeof(tail)?file_size:sizeof(tail);
    size_t lower=file_size-tail_bytes,eocd=file_size-22;
    if(read_at(fd,tail,tail_bytes,(off_t)lower))return NULL;
    for(;;){
        memcpy(end,tail+eocd-lower,sizeof(end));
        if(u32(end)==0x06054b50U&&eocd+22+u16(end+20)==file_size)break;
        if(eocd==lower)return NULL;
        --eocd;
    }
    unsigned count=u16(end+10);
    size_t cd=u32(end+16),cd_bytes=u32(end+12),cursor=cd;
    if(u16(end+4)||u16(end+6)||!count||count>256||count!=u16(end+8)||
       cd>eocd||cd_bytes!=eocd-cd||cd_bytes>1024U*1024U)return NULL;
    uint32_t offset=0,compressed=0,bytes=0,crc=0;unsigned method=0,flags=0,found=0,namelen=0;
    for(unsigned i=0;i<count;i++){
        if(cursor>eocd||eocd-cursor<sizeof(entry)||read_at(fd,entry,sizeof(entry),(off_t)cursor)||u32(entry)!=0x02014b50U)return NULL;
        unsigned n=u16(entry+28),extra=u16(entry+30),comment=u16(entry+32);
        size_t next=cursor+46+n+extra+comment;
        if(!n||n>512||next>eocd||u16(entry+6)>20||u16(entry+34)||read_at(fd,name,n,(off_t)cursor+46))return NULL;
        if(memchr(name,0,n))return NULL;
        name[n]=0;
        if(extension(name,lynx)){
            if(found++)return NULL;
            flags=u16(entry+8);method=u16(entry+10);crc=u32(entry+16);
            compressed=u32(entry+20);bytes=u32(entry+24);offset=u32(entry+42);namelen=n;
            memcpy(local_name,name,n+1);
        }
        cursor=next;
    }
    if(cursor!=eocd||!found||(flags&~0x080eU)||(flags&1U)||(method!=0&&method!=8)||
       !bytes||bytes>(lynx?1024U*1024U+64U:ROM_MAX)||!compressed||compressed>ZIP_MAX||
       offset>cd||cd-offset<30||read_at(fd,local,sizeof(local),offset)||u32(local)!=0x04034b50U||
       u16(local+4)>20||u16(local+6)!=flags||u16(local+8)!=method||u16(local+26)!=namelen)return NULL;
    size_t data=(size_t)offset+30+namelen+u16(local+28);
    if(data>cd||compressed>cd-data||read_at(fd,name,namelen,(off_t)offset+30)||memcmp(name,local_name,namelen))return NULL;
    if(!(flags&8U)&&(u32(local+14)!=crc||u32(local+18)!=compressed||u32(local+22)!=bytes))return NULL;
    unsigned char *rom=malloc((size_t)bytes+1U);if(!rom)return NULL;
    if(method==0){if(bytes!=compressed||read_at(fd,rom,bytes,(off_t)data))goto bad;}
    else {
        z_stream z;memset(&z,0,sizeof(z));
        if(inflateInit2(&z,-MAX_WBITS)!=Z_OK)goto bad;
        unsigned char input[16384];size_t supplied=0;int result=Z_OK;
        z.next_out=rom;z.avail_out=bytes+1U;
        while(result==Z_OK){
            if(!z.avail_in&&supplied<compressed){size_t n=compressed-supplied;if(n>sizeof(input))n=sizeof(input);
                if(read_at(fd,input,n,(off_t)(data+supplied))){result=Z_DATA_ERROR;break;}
                supplied+=n;z.next_in=input;z.avail_in=(uInt)n;
            }
            result=inflate(&z,Z_NO_FLUSH);
            if(result==Z_OK&&(!z.avail_out||(!z.avail_in&&supplied==compressed))){result=Z_DATA_ERROR;break;}
        }
        int valid=result==Z_STREAM_END&&z.total_in==compressed&&z.total_out==bytes;
        inflateEnd(&z);if(!valid)goto bad;
    }
    if((uint32_t)crc32(0,rom,bytes)!=crc)goto bad;
    *size=bytes;return rom;
bad:free(rom);return NULL;
}
void gkd_app_orientation_rom(const char *path,int lynx,struct gkd_game_orientation *out)
{
    *out=(struct gkd_game_orientation)GKD_GAME_ORIENTATION_INIT;
    if(!path)return;
    int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);struct stat st;
    if(fd<0)return;
    unsigned char *rom=NULL;size_t bytes=0;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<16||st.st_size>(off_t)ZIP_MAX)goto done;
    const char *dot=strrchr(path,'.');
    if(dot&&!strcasecmp(dot,".zip"))rom=zip_rom(fd,(size_t)st.st_size,lynx,&bytes);
    else if(extension(path,lynx)&&st.st_size<=(off_t)(lynx?1024U*1024U+64U:ROM_MAX)){
        bytes=(size_t)st.st_size;rom=malloc(bytes);
        if(rom&&read_at(fd,rom,bytes,0)){free(rom);rom=NULL;}
    }
    if(!rom)goto done;
    if(lynx){
        if(bytes<65||memcmp(rom,"LYNX",4)||u16(rom+8)!=1||rom[58]>2)goto done;
        size_t payload=bytes-64;
        /* libretro-handy lynx/cart_db.h: factual CRC/size/rotation metadata.
         * Override incorrect valid headers by content, never a ROM filename. */
        static const struct {uint32_t crc,bytes,hint;} known[]={
            {0x97501709,131072,GKD_HINT_RIGHT},{0xdcd723e3,131072,GKD_HINT_LEFT},
            {0x7f0ec7ad,131072,GKD_HINT_LEFT},{0x4d5d94f4,262144,GKD_HINT_LEFT},
            {0xa53649f1,262144,GKD_HINT_LEFT},{0x0271b6e9,262144,GKD_HINT_RIGHT},
            {0x006fd398,262144,GKD_HINT_RIGHT},{0xbcd10c3a,262144,GKD_HINT_RIGHT},
            {0x689f31a4,524288,GKD_HINT_RIGHT}};
        uint32_t crc=(uint32_t)crc32(0,rom+64,(uInt)payload);
        unsigned hint=rom[58]?rom[58]+1U:GKD_HINT_NONE,source=GKD_ORIGIN_LNX_HEADER;
        for(unsigned i=0;i<sizeof(known)/sizeof(known[0]);i++)if(known[i].crc==crc&&known[i].bytes==payload){hint=known[i].hint;source=GKD_ORIGIN_LYNX_CRC;break;}
        if(payload<131072||(payload&(payload-1)))goto done;
        out->hint=hint;out->source=source;
        out->aspect=hint==GKD_HINT_NONE?GKD_ASPECT_LANDSCAPE:GKD_ASPECT_PORTRAIT;
    }else{
        /* WonderSwan ROM footer orientation is the default, not live HVMode. */
        if(bytes<65536||(bytes&(bytes-1))||rom[bytes-16]!=0xea)goto done;
        unsigned sum=0;for(size_t i=0;i<bytes-2;i++)sum+=rom[i];
        if((sum&65535U)!=u16(rom+bytes-2))goto done;
        out->aspect=(rom[bytes-4]&1U)?GKD_ASPECT_PORTRAIT:GKD_ASPECT_LANDSCAPE;
        out->source=GKD_ORIGIN_WS_FOOTER;
    }
    out->scope=GKD_SCOPE_LAUNCH_ROM;
done:free(rom);close(fd);
}
void gkd_app_orientation_close(struct gkd_app_orientation *s)
{
    if(!s)return;
    if(s->page)munmap(s->page,sizeof(*s->page));
    if(s->fd>=0)close(s->fd);
    if(s->lifetime>=0)close(s->lifetime);
    *s=(struct gkd_app_orientation)GKD_APP_ORIENTATION_INIT;
}
void gkd_app_orientation_read(const struct gkd_app_orientation *s,struct gkd_game_orientation *out)
{
    *out=(struct gkd_game_orientation)GKD_GAME_ORIENTATION_INIT;
    if(!s)return;
    *out=s->launch;
    if(s->page){
        struct pollfd life={s->lifetime,POLLIN,0};
        if(s->lifetime<0||poll(&life,1,0)!=0)return;
        uint32_t aspect=__atomic_load_n(&s->page->aspect,__ATOMIC_ACQUIRE);
        if(aspect==GKD_ASPECT_LANDSCAPE||aspect==GKD_ASPECT_PORTRAIT){
            out->aspect=aspect;out->source=GKD_ORIGIN_BURN_DRIVER;out->scope=GKD_SCOPE_ACTIVE_DRIVER;
        }
    }
}
void gkd_app_orientation_prepare(struct gkd_app_orientation *s,const struct gkd_menu_profile *p,int argc,char *const argv[])
{
    *s=(struct gkd_app_orientation)GKD_APP_ORIENTATION_INIT;
    if(!p||argc!=1||!argv||!argv[0])return;
    int handy=!strcmp(p->opk_sha256,"c05003443098c6de8d47cef9ab4c7c372c6cc907bfcc97692655d4df85e46a36")&&!strcmp(p->executable,"handy");
    int oswan=!strcmp(p->opk_sha256,"abaa9fde1ff7a8f889f603a68ee80b9e7ac6d1a37a7ca42185984c83a53fdd95")&&!strcmp(p->executable,"oswan");
    if((handy||oswan)&&!strcmp(p->desktop,"default.gcw0.desktop")){gkd_app_orientation_rom(argv[0],handy,&s->launch);return;}
    int burn=(!strcmp(p->opk_sha256,"67fda9e6d14ea7ee30e4522455ba3ff21bfb37cf02eb619eb15283ceebb60f8b")||
        !strcmp(p->opk_sha256,"ac721a8bd00a5ab0731a461240b79597cdf8c52b25d2cca7f011e6d5a795d691"))&&
        !strcmp(p->executable,"fbasdl.dge")&&!strcmp(p->desktop,"fba_ux.gcw0.desktop");
    if(!burn)return;
    const char *base=strrchr(argv[0],'/');base=base?base+1:argv[0];size_t n=strlen(base);
    if(n<5||n>=64||strcasecmp(base+n-4,".zip"))return;
    n-=4;for(size_t i=0;i<n;i++)if(!((base[i]>='a'&&base[i]<='z')||(base[i]>='0'&&base[i]<='9')||base[i]=='_'))return;
    s->fd=(int)syscall(SYS_memfd_create,"gkd-orientation",MFD_CLOEXEC|MFD_ALLOW_SEALING);
    if(s->fd<0||ftruncate(s->fd,sizeof(*s->page)))goto bad;
    void *map=mmap(NULL,sizeof(*s->page),PROT_READ|PROT_WRITE,MAP_SHARED,s->fd,0);
    if(map==MAP_FAILED)goto bad;
    s->page=map;memset(s->page,0,sizeof(*s->page));
    s->page->magic=GKD_ORIENTATION_MAGIC;s->page->version=GKD_ORIENTATION_VERSION;
    memcpy(s->page->driver,base,n);
    if(fcntl(s->fd,F_ADD_SEALS,F_SEAL_SEAL|F_SEAL_SHRINK|F_SEAL_GROW))goto bad;
    return;
bad:gkd_app_orientation_close(s);
}
