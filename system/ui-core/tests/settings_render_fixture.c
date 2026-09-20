/* SPDX-License-Identifier: GPL-2.0 */
#include "gkd-ui.h"
#include "gkd-ui-language.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    static const struct gkd_ui_menu_item items[]={{"ANIMATION",0},{"AUTO SLEEP",0},{"SHOW FPS",0},{"LANGUAGE",0}};
    const char *values[]={"ON","0","OFF","EN"};
    struct gkd_ui_config c;struct gkd_ui_font f={0};
    uint16_t pixels[320*240], before[320*240];
    struct gkd_ui_surface s={pixels,320,240,320};
    struct gkd_ui_menu m={"SETTINGS",items,4,0,GKD_UI_ACTION_ENABLED,GKD_UI_ACTION_ENABLED,NULL,NULL};
    assert(argc==5);gkd_ui_config_defaults(&c);assert(!gkd_ui_font_load(&f,argv[1]));
    m.action_a_label=c.action_yes;m.action_b_label=c.action_no;
    for(unsigned selected=0;selected<4;selected++){
        char path[512];m.selected=selected;
        assert(!gkd_ui_render_settings(&s,&c,&f,&m,values,NULL));
        /* Native settings reuse the complete common footer, pixels and wording;
         * no navigation caption or settings-only font-size override. */
        struct gkd_ui_surface reference={before,320,240,320};
        assert(!gkd_ui_render_menu(&reference,&c,&f,&m));
        assert(!memcmp(pixels+178U*320U,before+178U*320U,(240U-178U)*320U*sizeof(*pixels)));
        /* Fixed triangle masks match across all four rows after color normalization. */
        for(unsigned row=1;row<4;row++)for(unsigned side=0;side<2;side++)
            for(unsigned dy=0;dy<9;dy++)for(unsigned dx=0;dx<5;dx++){
                unsigned cx=side ? c.menu_row_x+c.menu_row_width-14U : c.menu_row_x+c.menu_row_width-69U;
                unsigned y0=c.menu_row_y+c.menu_row_height/2U-4U;
                unsigned y=y0+row*c.menu_row_step;
                uint16_t a=pixels[(y0+dy)*320U+cx+dx];
                uint16_t b=pixels[(y+dy)*320U+cx+dx];
                assert((a==(selected==0 ? c.dark:c.normal))==(b==(selected==row ? c.dark:c.normal)));
            }
        assert(snprintf(path,sizeof(path),"%s/settings-%u.rgb565",argv[2],selected)>0);
        assert(!gkd_ui_write_raw(&s,path));
    }
    memcpy(before,pixels,sizeof(pixels));values[0]="toolong";
    assert(gkd_ui_render_settings(&s,&c,&f,&m,values,NULL)<0);
    assert(!memcmp(before,pixels,sizeof(pixels)));
    values[0]="ON";values[1]="60";values[3]="CN";
    assert(!gkd_ui_render_settings(&s,&c,&f,&m,values,NULL));
    {
        struct gkd_ui_font cn={0},compact={0};
        struct gkd_ui_menu_item translated[4];
        char path[512];
        assert(!gkd_ui_font_load(&cn,argv[3]));assert(!gkd_ui_font_load(&compact,argv[4]));
        cn.latin=compact.latin=&f;cn.compact=&compact;
        /* Reusing the original Latin face preserves every English pixel. */
        assert(!gkd_ui_render_settings(&s,&c,&f,&m,values,NULL));
        memcpy(before,pixels,sizeof(pixels));
        assert(!gkd_ui_render_settings(&s,&c,&cn,&m,values,NULL));
        assert(!memcmp(before,pixels,sizeof(pixels)));
        for(unsigned i=0;i<4;i++){
            translated[i].label=gkd_ui_text(GKD_UI_CN,(enum gkd_ui_text)(GKD_UI_TEXT_ANIMATION+i));
            translated[i].icon=0;
        }
        m.title=gkd_ui_text(GKD_UI_CN,GKD_UI_TEXT_SETTINGS);m.items=translated;
        m.action_a_label=gkd_ui_text(GKD_UI_CN,GKD_UI_TEXT_YES);
        m.action_b_label=gkd_ui_text(GKD_UI_CN,GKD_UI_TEXT_NO);
        snprintf(c.action_yes,sizeof(c.action_yes),"%s",m.action_a_label);
        snprintf(c.action_no,sizeof(c.action_no),"%s",m.action_b_label);
        assert(!gkd_ui_render_settings(&s,&c,&cn,&m,values,NULL));
        assert(memcmp(before,pixels,sizeof(pixels)));
        /* Translation changes content only; the shared button pair is invariant. */
        assert(!memcmp(before+178U*320U,pixels+178U*320U,(240U-178U)*320U*sizeof(*pixels)));
        {
            uint16_t common_pixels[320U*240U];
            struct gkd_ui_surface common={common_pixels,320U,240U,320U};
            assert(!gkd_ui_render_menu(&common,&c,&cn,&m));
            assert(!memcmp(pixels+178U*320U,common_pixels+178U*320U,(240U-178U)*320U*sizeof(*pixels)));
        }
        assert(snprintf(path,sizeof(path),"%s/settings-cn.rgb565",argv[2])>0);
        assert(!gkd_ui_write_raw(&s,path));
        memcpy(before,pixels,sizeof(pixels));
        m.action_a=m.action_b=GKD_UI_ACTION_DISABLED;
        assert(!gkd_ui_render_settings(&s,&c,&cn,&m,values,gkd_ui_text(GKD_UI_CN,GKD_UI_TEXT_SAVING)));
        assert(memcmp(before,pixels,sizeof(pixels)));
        assert(snprintf(path,sizeof(path),"%s/settings-saving-cn.rgb565",argv[2])>0);
        assert(!gkd_ui_write_raw(&s,path));
        m.action_a=m.action_b=GKD_UI_ACTION_ENABLED;
        for(unsigned kind=0;kind<2U;kind++){
            struct gkd_ui_config native=c;
            static const enum gkd_ui_text labels[2][3]={{GKD_UI_TEXT_CHARGE,GKD_UI_TEXT_STORAGE,GKD_UI_TEXT_DEBUG},
                                                       {GKD_UI_TEXT_SUSPEND,GKD_UI_TEXT_REBOOT,GKD_UI_TEXT_SHUTDOWN}};
            for(unsigned i=0;i<3U;i++){
                translated[i].label=gkd_ui_text(GKD_UI_CN,labels[kind][i]);translated[i].icon=kind?i+2U:i;
            }
            m.title=gkd_ui_text(GKD_UI_CN,kind?GKD_UI_TEXT_POWER:GKD_UI_TEXT_USB_MODE);m.count=3U;m.selected=0;
            assert(!gkd_ui_render_menu(&s,&native,&cn,&m));
            assert(snprintf(path,sizeof(path),"%s/%s-cn.rgb565",argv[2],kind?"power":"usb")>0);
            assert(!gkd_ui_write_raw(&s,path));
        }
        uint32_t tile[GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT];
        const enum gkd_ui_text notices[]={GKD_UI_TEXT_SHOT_SAVED,GKD_UI_TEXT_SHOT_FAILED,GKD_UI_TEXT_CHARGE,
            GKD_UI_TEXT_STORAGE,GKD_UI_TEXT_DEBUG,GKD_UI_TEXT_BAT_LOW,GKD_UI_TEXT_BAT_CRITICAL,GKD_UI_TEXT_UPDATED};
        for(unsigned i=0;i<sizeof(notices)/sizeof(notices[0]);i++){
            const unsigned icons[]={3U,3U,5U,6U,7U,2U,2U,7U};
            struct gkd_ui_osd notice={gkd_ui_text(GKD_UI_CN,notices[i]),icons[i],-1,0};
            if(notices[i]==GKD_UI_TEXT_CHARGE){notice.text="充电 80%";notice.level=80;}
            assert(!gkd_ui_export_osd_argb(tile,GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT,&c,&cn,&notice));
            assert(snprintf(path,sizeof(path),"%s/osd-cn-%u.argb",argv[2],i)>0);
            FILE *file=fopen(path,"wb");assert(file);
            assert(fwrite(tile,sizeof(tile),1,file)==1);assert(!fclose(file));
        }
        {
            struct gkd_ui_config status=c;
            snprintf(status.action_no,sizeof(status.action_no),"%s",gkd_ui_text(GKD_UI_CN,GKD_UI_TEXT_BACK));
            gkd_ui_render_status(&s,&status,&cn,gkd_ui_text(GKD_UI_CN,GKD_UI_TEXT_ACTION_FAILED),1);
            assert(snprintf(path,sizeof(path),"%s/status-cn.rgb565",argv[2])>0);
            assert(!gkd_ui_write_raw(&s,path));
        }
        m.count=4U;
        cn.latin=&cn;
        memcpy(before,pixels,sizeof(pixels));
        assert(gkd_ui_render_settings(&s,&c,&cn,&m,values,"hint")<0);
        assert(!memcmp(before,pixels,sizeof(pixels)));
        cn.latin=NULL;cn.compact=NULL;gkd_ui_font_release(&compact);gkd_ui_font_release(&cn);
        assert(!gkd_ui_text((enum gkd_ui_language)2,GKD_UI_TEXT_SETTINGS));
    }
    {
        struct gkd_ui_font cn={0},compact={0};
        assert(!gkd_ui_font_load(&cn,argv[3])&&!gkd_ui_font_load(&compact,argv[4]));
        cn.latin=compact.latin=&f;cn.compact=&compact;
        const enum gkd_ui_text labels[]={GKD_UI_TEXT_ANIMATION,GKD_UI_TEXT_AUTO_SLEEP,GKD_UI_TEXT_SHOW_FPS,GKD_UI_TEXT_LANGUAGE,GKD_UI_TEXT_INPUT_STYLE};
        for(unsigned lang=0;lang<2;lang++)for(unsigned style=0;style<3;style++)for(unsigned row=0;row<5;row++) {
            struct gkd_ui_menu_item five[5];
            for(unsigned i=0;i<5;i++)five[i]=(struct gkd_ui_menu_item){gkd_ui_text((enum gkd_ui_language)lang,labels[i]),0};
            const char *display[]={"ON","10","OFF",lang?"CN":"EN",style==0?"ORIG":style==1?"XBOX":"PS"};
            gkd_ui_config_defaults(&c);c.input_style=style;
            const char *yes=gkd_ui_text((enum gkd_ui_language)lang,GKD_UI_TEXT_YES),*no=gkd_ui_text((enum gkd_ui_language)lang,GKD_UI_TEXT_NO);
            snprintf(c.action_yes,sizeof(c.action_yes),"%s",yes);snprintf(c.action_no,sizeof(c.action_no),"%s",no);
            struct gkd_ui_menu five_menu={gkd_ui_text((enum gkd_ui_language)lang,GKD_UI_TEXT_SETTINGS),five,5,row,GKD_UI_ACTION_ENABLED,GKD_UI_ACTION_ENABLED,yes,no};
            if(gkd_ui_render_settings(&s,&c,lang?&cn:&f,&five_menu,display,NULL)){fprintf(stderr,"STYLE_RENDER_REJECT lang=%u style=%u row=%u\n",lang,style,row);abort();}
            char path[512];assert(snprintf(path,sizeof(path),"%s/style-%u-lang-%u-row-%u.rgb565",argv[2],style,lang,row)>0);
            assert(!gkd_ui_write_raw(&s,path));
        }
        gkd_ui_font_release(&compact);gkd_ui_font_release(&cn);
        puts("SETTINGS_STYLE_RENDER_PASS three-styles/two-languages/five-rows/30-screens");
    }
    {
        struct gkd_ui_menu_item six[6]={{"ANIMATION",0},{"AUTO SLEEP",0},{"SHOW FPS",0},{"LANGUAGE",0},{"CONTROLS",0},{"SYSTEM UPDATE",1}};
        const char *display[]={"ON","10","OFF","EN","ORIG",NULL};
        gkd_ui_config_defaults(&c);
        struct gkd_ui_menu six_menu={"SETTINGS",six,6,5,GKD_UI_ACTION_ENABLED,GKD_UI_ACTION_ENABLED,"DIFFERENT","BACK"};
        assert(!gkd_ui_render_settings(&s,&c,&f,&six_menu,display,NULL));
        char path[512];snprintf(path,sizeof(path),"%s/settings-update.rgb565",argv[2]);assert(!gkd_ui_write_raw(&s,path));
        memcpy(before,pixels,sizeof(pixels));
        six_menu.action_a_label="OK";six_menu.action_b_label="CANCEL";
        assert(!gkd_ui_render_settings(&s,&c,&f,&six_menu,display,NULL));
        assert(!memcmp(before,pixels,sizeof(pixels)));
        unsigned track_x=c.menu_row_x+c.menu_row_width+6U;
        unsigned top=c.menu_row_y,bottom=top+4U*c.menu_row_step+c.menu_row_height-1U;
        /* Last viewport: remaining content is above the thumb. */
        assert(pixels[top*320U+track_x]!=c.normal&&pixels[bottom*320U+track_x]==c.normal);
        for(unsigned row=0;row<6U;row++){
            six_menu.selected=row;assert(!gkd_ui_render_settings(&s,&c,&f,&six_menu,display,NULL));
            snprintf(path,sizeof(path),"%s/settings-scroll-%u.rgb565",argv[2],row);assert(!gkd_ui_write_raw(&s,path));
        }
        six_menu.selected=0;assert(!gkd_ui_render_settings(&s,&c,&f,&six_menu,display,NULL));
        assert(pixels[top*320U+track_x]==c.normal&&pixels[bottom*320U+track_x]!=c.normal);
        struct gkd_ui_surface common={before,320,240,320};
        assert(!gkd_ui_render_menu(&common,&c,&f,&six_menu));
        for(unsigned y=top;y<=bottom;y++)for(unsigned dx=0;dx<3U;dx++)
            assert(before[y*320U+track_x+dx]==pixels[y*320U+track_x+dx]);
        six_menu.count=5;assert(!gkd_ui_render_settings(&s,&c,&f,&six_menu,display,NULL));
        for(unsigned y=top;y<=bottom;y++)assert(pixels[y*320U+track_x]!=c.normal);
        puts("SHARED_MENU_SCROLLBAR_PASS below/above/no-overflow/common-menu-pixel-parity");
        puts("SETTINGS_UPDATE_RENDER_PASS six-rows/shared-five-row-viewport/action-row/shared-buttons");
    }
    gkd_ui_font_release(&f);
    puts("SETTINGS_RENDER_PASS four-selections/aligned-arrows/shared-footer-pixel-parity/no-navigation-caption/bounds/atomic-rejection");
    return 0;
}
