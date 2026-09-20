/* SPDX-License-Identifier: GPL-2.0 */
#define _GNU_SOURCE
#include "gkd-app-fps-gate.h"
#include "gkd-update-sha256.h"
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define PH_MAX 128U
#define DYN_MAX 4096U
#define STR_MAX (1024U*1024U)
#define SYM_MAX 65536U
static int exact_read(int fd,void *data,size_t bytes,off_t offset)
{
    unsigned char *p=data;
    while(bytes){ssize_t n=pread(fd,p,bytes,offset);if(n<0&&errno==EINTR)continue;
        if(n<=0){errno=n?errno:EPROTO;return -1;}p+=n;bytes-=(size_t)n;offset+=n;}
    return 0;
}
static int span(off_t offset,size_t bytes,off_t size)
{
    return offset>=0&&(uint64_t)offset<=(uint64_t)size&&
           (uint64_t)bytes<=(uint64_t)size-(uint64_t)offset;
}
static int virtual_offset(const Elf32_Phdr *ph,unsigned count,Elf32_Addr address,
                          size_t bytes,off_t file_size,off_t *offset)
{
    for(unsigned i=0;i<count;i++)if(ph[i].p_type==PT_LOAD&&address>=ph[i].p_vaddr){
        uint64_t delta=(uint64_t)address-ph[i].p_vaddr;
        if(delta<=ph[i].p_filesz&&bytes<=ph[i].p_filesz-delta){
            uint64_t value=(uint64_t)ph[i].p_offset+delta;
            if(value<=INT64_MAX&&span((off_t)value,bytes,file_size)){*offset=(off_t)value;return 0;}
        }
    }
    errno=EPROTO;return -1;
}
static int bounded_string(const char *table,size_t bytes,uint32_t offset,const char *expected)
{
    size_t length=strlen(expected);
    return offset<bytes&&length<bytes-offset&&!memcmp(table+offset,expected,length)&&
           table[offset+length]==0;
}
static int elf_capable(int fd,int working_directory)
{
    Elf32_Ehdr eh;struct stat st;Elf32_Phdr *ph=NULL;char *strings=NULL;
    Elf32_Addr straddr=0,symaddr=0,hashaddr=0;Elf32_Word strsz=0,syment=0;
    uint32_t needed[64],search[4];unsigned needed_count=0,search_count=0;
    int interp=0,answer=0;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||!(st.st_mode&0111)||
       (st.st_mode&(S_ISUID|S_ISGID)))return 0;
    if(exact_read(fd,&eh,sizeof(eh),0))return 0;
    if(memcmp(eh.e_ident,ELFMAG,SELFMAG)||eh.e_ident[EI_CLASS]!=ELFCLASS32||
       eh.e_ident[EI_DATA]!=ELFDATA2LSB||eh.e_ident[EI_VERSION]!=EV_CURRENT||
       eh.e_type!=ET_EXEC||eh.e_machine!=EM_MIPS||eh.e_version!=EV_CURRENT||
       eh.e_ehsize!=sizeof(eh)||eh.e_phentsize!=sizeof(Elf32_Phdr)||
       !eh.e_phnum||eh.e_phnum>PH_MAX)return 0;
    if(!span((off_t)eh.e_phoff,(size_t)eh.e_phnum*sizeof(*ph),st.st_size))return 0;
    ph=malloc((size_t)eh.e_phnum*sizeof(*ph));if(!ph)return -1;
    if(exact_read(fd,ph,(size_t)eh.e_phnum*sizeof(*ph),(off_t)eh.e_phoff))goto out;
    for(unsigned i=0;i<eh.e_phnum;i++){
        if(ph[i].p_type==PT_INTERP){
            char loader[64];
            if(!ph[i].p_filesz||ph[i].p_filesz>sizeof(loader)||
               !span(ph[i].p_offset,ph[i].p_filesz,st.st_size)||
               exact_read(fd,loader,ph[i].p_filesz,ph[i].p_offset)||
               loader[ph[i].p_filesz-1]!=0||
               strcmp(loader,"/lib/ld-uClibc.so.0"))goto out;
            interp=1;
        }
        if(ph[i].p_type==PT_DYNAMIC){
            if(!ph[i].p_filesz||ph[i].p_filesz%sizeof(Elf32_Dyn)||
               ph[i].p_filesz/sizeof(Elf32_Dyn)>DYN_MAX||
               !span(ph[i].p_offset,ph[i].p_filesz,st.st_size))goto out;
            for(unsigned j=0;j<ph[i].p_filesz/sizeof(Elf32_Dyn);j++){
                Elf32_Dyn d;if(exact_read(fd,&d,sizeof(d),ph[i].p_offset+(off_t)j*sizeof(d)))goto out;
                if(d.d_tag==DT_NULL)break;
                if(d.d_tag==DT_STRTAB)straddr=d.d_un.d_ptr;
                else if(d.d_tag==DT_STRSZ)strsz=d.d_un.d_val;
                else if(d.d_tag==DT_SYMTAB)symaddr=d.d_un.d_ptr;
                else if(d.d_tag==DT_SYMENT)syment=d.d_un.d_val;
                else if(d.d_tag==DT_HASH)hashaddr=d.d_un.d_ptr;
                else if(d.d_tag==DT_RPATH||d.d_tag==DT_RUNPATH){
                    if(search_count>=sizeof(search)/sizeof(search[0]))goto out;
                    search[search_count++]=d.d_un.d_val;
                }else if(d.d_tag==DT_NEEDED){
                    if(needed_count>=sizeof(needed)/sizeof(needed[0]))goto out;
                    needed[needed_count++]=d.d_un.d_val;
                }
            }
        }
    }
    if(!interp||!straddr||!strsz||strsz>STR_MAX||!symaddr||
       syment!=sizeof(Elf32_Sym)||!hashaddr)goto out;
    off_t stroff,hashoff,symoff;
    if(virtual_offset(ph,eh.e_phnum,straddr,strsz,st.st_size,&stroff)||
       virtual_offset(ph,eh.e_phnum,hashaddr,8,st.st_size,&hashoff)||
       virtual_offset(ph,eh.e_phnum,symaddr,sizeof(Elf32_Sym),st.st_size,&symoff))goto out;
    strings=malloc(strsz);if(!strings){answer=-1;goto out;}
    if(exact_read(fd,strings,strsz,stroff))goto out;
    int have_sdl=0,have_flip=0,searches_working_directory=0;
    for(unsigned i=0;i<needed_count;i++)
        if(bounded_string(strings,strsz,needed[i],"libSDL-1.2.so.0"))have_sdl=1;
    for(unsigned i=0;i<search_count;i++){
        if(search[i]>=strsz)goto out;
        const char *value=strings+search[i];
        size_t available=strsz-search[i],length=strnlen(value,available);
        if(length==available)goto out;
        for(size_t j=0;j<length;j++)if(value[j]!=':')goto out;
        searches_working_directory=1;
    }
    if(searches_working_directory){
        struct stat local;
        if(!fstatat(working_directory,"libSDL-1.2.so.0",&local,AT_SYMLINK_NOFOLLOW)||
           errno!=ENOENT)goto out;
    }
    uint32_t hash[2];if(exact_read(fd,hash,sizeof(hash),hashoff)||!hash[1]||hash[1]>SYM_MAX||
       !span(symoff,(size_t)hash[1]*sizeof(Elf32_Sym),st.st_size))goto out;
    for(uint32_t i=0;i<hash[1];i++){
        Elf32_Sym sym;if(exact_read(fd,&sym,sizeof(sym),symoff+(off_t)i*sizeof(sym)))goto out;
        unsigned bind=ELF32_ST_BIND(sym.st_info);
        if(sym.st_shndx==SHN_UNDEF&&(bind==STB_GLOBAL||bind==STB_WEAK)&&
           bounded_string(strings,strsz,sym.st_name,"SDL_Flip")){have_flip=1;break;}
    }
    answer=have_sdl&&have_flip;
out:{int saved=errno;free(strings);free(ph);errno=saved;return answer;}
}
static int digest_fd(int fd,const char expected[64])
{
    struct gkdu_sha256 hash;unsigned char data[32768],digest[32];char actual[65];
    if(!expected||strlen(expected)!=64U||lseek(fd,0,SEEK_SET)!=0)return -1;
    gkdu_sha256_init(&hash);
    for(;;){ssize_t n=read(fd,data,sizeof(data));if(n<0&&errno==EINTR)continue;
        if(n<0)return -1;
        if(!n)break;
        gkdu_sha256_update(&hash,data,(size_t)n);
    }
    gkdu_sha256_final(&hash,digest);
    for(unsigned i=0;i<32;i++)(void)sprintf(actual+2*i,"%02x",digest[i]);
    return memcmp(actual,expected,64U)?0:1;
}
static int pinned_pair(const char *alias,const char *resolved,const char *hash)
{
    struct stat a,b;int one=-1,two=-1,result=0;
    one=open(alias,O_RDONLY|O_CLOEXEC);two=open(resolved,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(one<0||two<0||fstat(one,&a)||fstat(two,&b))goto out;
    if(a.st_dev!=b.st_dev||a.st_ino!=b.st_ino||!S_ISREG(b.st_mode)||
       b.st_uid||(b.st_mode&0022))goto out;
    result=digest_fd(two,hash);
out:{int saved=errno;if(one>=0)close(one);if(two>=0)close(two);errno=saved;return result;}
}
static int pinned_file(const char *path,const char *hash)
{
    struct stat st;int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW),result=0;
    if(fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&!st.st_uid&&!(st.st_mode&0022))
        result=digest_fd(fd,hash);
    int saved=errno;if(fd>=0)close(fd);errno=saved;return result;
}
int gkd_app_fps_gate(int fd,int working_directory,const char *ld_library_path,
                     const struct gkd_app_fps_gate_paths *p)
{
    struct stat directory;
    if(fd<0||working_directory<0||fstat(working_directory,&directory)||
       !S_ISDIR(directory.st_mode)||!p||!p->sdl_alias||!p->sdl_resolved||
       !p->sdl_sha256||!p->interposer||!p->interposer_sha256){
        errno=EINVAL;return -1;
    }
    if(ld_library_path&&*ld_library_path)return 0;
    int capable=elf_capable(fd,working_directory);if(capable<=0)return capable;
    int sdl=pinned_pair(p->sdl_alias,p->sdl_resolved,p->sdl_sha256);
    if(sdl<=0)return sdl;
    return pinned_file(p->interposer,p->interposer_sha256);
}
