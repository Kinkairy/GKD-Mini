#include <assert.h>
#include <stdio.h>
#include <string.h>
#define __init
#define SZ_32M (32UL * 1024 * 1024)
/* CONSTANTS */
struct fake_dt { const char *compatible; const char *model; int has_memory; };
static struct fake_dt dt;
static void *of_root=&dt;
static char *system_type;
static unsigned long mips_machtype;
static unsigned long added_memory;
static unsigned checks;
static int fdt_node_check_compatible(const void *fdt,int node,const char *name)
{ assert(fdt==&dt && node==0);return strcmp(dt.compatible,name); }
static int fdt_path_offset(const void *fdt,const char *path)
{ assert(fdt==&dt && !strcmp(path,"/memory"));return dt.has_memory?1:-1; }
static void early_init_dt_add_memory_arch(unsigned long base,unsigned long size)
{ assert(base==0);added_memory+=size; }
static int of_property_read_string(void *root,const char *name,const char **value)
{ assert(root==&dt && !strcmp(name,"model"));if (!dt.model)return -1;*value=dt.model;return 0; }
static int of_property_read_string_index(void *root,const char *name,int index,const char **value)
{ assert(root==&dt && !strcmp(name,"compatible") && index==0);*value=dt.compatible;return 0; }
/* ACTUAL_FUNCTIONS */
static void check(const char *compat,const char *model,unsigned long mach,const char *want,unsigned long memory)
{
 dt.compatible=compat;dt.model=model;dt.has_memory=memory==0;added_memory=0;
 system_type="stale prior value";
 assert(ingenic_fixup_fdt(&dt,(void *)mach)==&dt);
 assert(mips_machtype==mach && added_memory==memory);
 assert(!strcmp(get_system_type(),want));
 if(!strcmp(compat,"gamekiddy,gkd350"))assert(system_type==NULL);
 else assert(system_type!=NULL);
 ++checks;
}
int main(void)
{
 const char *model=/* ACTUAL_MODEL */;
 check("gamekiddy,gkd350",model,MACH_INGENIC_X1830,model,0);
 /* Model is read from the live DT at query time, not an early flattened pointer. */
 dt.model="DT model lifetime probe";assert(!strcmp(get_system_type(),dt.model));++checks;
 check("other,x1830","Other board",MACH_INGENIC_X1830,"X1830",0);
 check("gamekiddy,gkd350-plus","Other board",MACH_INGENIC_X1830,"X1830",0);
 check("ingenic,x1000","Other board",MACH_INGENIC_X1000,"X1000",0);
 check("ingenic,x2000","Other board",MACH_INGENIC_X2000,"X2000",0);
 check("ingenic,jz4770","Other board",MACH_INGENIC_JZ4770,"JZ4770",0);
 check("unknown","Other board",~0UL,"JZ4740",0);
 check("qi,lb60","Ben NanoNote",MACH_INGENIC_JZ4740,"JZ4740",SZ_32M);
 check("qi,lb60","Ben NanoNote",MACH_INGENIC_JZ4740,"JZ4740",0);
 /* Sequential board calls must not retain either the old SoC or old model. */
 check("gamekiddy,gkd350",model,MACH_INGENIC_X1830,model,0);
 check("other,x1830","Other board",MACH_INGENIC_X1830,"X1830",0);
 printf("GKD_BOARD_IDENTITY_TESTS=PASS checks=%u\n",checks);
 return 0;
}
