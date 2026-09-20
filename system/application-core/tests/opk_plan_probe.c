/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-opk-plan.h"
#include <stdio.h>
int main(int argc,char **argv)
{
    struct gkd_opk_plan plan;
    if(argc<2)return 64;
    if(gkd_opk_plan_open(argv[1],NULL,argc-2,argv+2,&plan)){perror("OPK_PLAN");return 1;}
    unsigned count=0;while(plan.argv[count])count++;
    printf("GKD_OPK_PLAN=PASS argc=%u mount=%s terminal=%d downscaling=%d\n",
           count,plan.mount_name,plan.needs_terminal,plan.needs_downscaling);
    gkd_opk_plan_close(&plan);return 0;
}
