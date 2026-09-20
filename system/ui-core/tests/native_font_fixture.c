/* SPDX-License-Identifier: GPL-2.0 */
#include "../source/gkd-ui.c"
#include <assert.h>

int main(int argc, char **argv)
{
    struct gkd_ui_font latin={0}, cn={0}, compact={0};
    uint16_t actual[64*64], expected[64*64], reference[64*64];
    struct gkd_ui_surface surface={actual,64,64,64}, other={reference,64,64,64};
    struct draw_target target=rgb_target(&surface), compare=rgb_target(&other);
    assert(argc==4);
    assert(!gkd_ui_font_load(&latin,argv[1]));
    assert(!gkd_ui_font_load(&cn,argv[2]));
    assert(!gkd_ui_font_load(&compact,argv[3]));
    cn.latin=compact.latin=&latin;cn.compact=&compact;
    assert(valid_font(&cn));
    const unsigned sizes[]={12U,16U,18U};
    for(unsigned k=0;k<3U;k++){
        unsigned px=sizes[k];
        for(uint32_t cp=32U;cp<127U;cp++){
            memset(actual,0,sizeof(actual));memset(reference,0,sizeof(reference));
            draw_glyph(&target,&cn,cp,20,20,px,0xffffU);
            draw_glyph(&compare,&latin,cp,20,20,px,0xffffU);
            assert(!memcmp(actual,reference,sizeof(actual)));
        }
        const struct gkd_ui_font *face=px==12U?&compact:&cn;
        const unsigned char *table=face->data+face->headersize+face->length*face->charsize;
        for(unsigned index=0;index<face->length;index++){
            uint32_t cp=utf8_next(&table);assert(*table++==255U);
            unsigned height=px==12U?12U:14U;
            assert(codepoint_font(&cn,cp,px)==face);
            assert(glyph_width(face,px)==height);
            memset(actual,0,sizeof(actual));memset(expected,0,sizeof(expected));
            draw_glyph(&target,&cn,cp,20,20,px,0xffffU);
            const unsigned char *bits=face->data+face->headersize+index*face->charsize;
            int top=20+((int)px-(int)height)/2;
            for(unsigned y=0;y<height;y++)for(unsigned x=0;x<height;x++)
                if(bits[y*2U+x/8U]&(0x80U>>(x&7U)))
                    expected[((unsigned)top+y)*64U+20U+x]=0xffffU;
            assert(!memcmp(actual,expected,sizeof(actual)));
        }
        assert(text_width(&cn,"A中B",px)==2U*glyph_width(&latin,px)+(px==12U?12U:14U)+2U);
    }
    struct gkd_ui_config config;gkd_ui_config_defaults(&config);
    uint32_t tile[GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT], original[GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT];
    const char *texts[]={"FPS 60","FPS --","SAVED","SHOT 01 SAVED","VOL 50","BRI 50"};
    for(unsigned i=0;i<sizeof(texts)/sizeof(texts[0]);i++){
        struct gkd_ui_osd osd={texts[i],0U,50,0};
        assert(!gkd_ui_export_osd_argb(tile,GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT,&config,&cn,&osd));
        assert(!gkd_ui_export_osd_argb(original,GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT,&config,&latin,&osd));
        assert(!memcmp(tile,original,sizeof(tile)));
    }
    const unsigned char *table=compact.data+compact.headersize+compact.length*compact.charsize;
    for(unsigned index=0;index<compact.length;index++){
        const unsigned char *start=table;
        (void)utf8_next(&table);size_t len=(size_t)(table-start);assert(*table++==255U);
        char text[5]={0};assert(len<sizeof(text));memcpy(text,start,len);
        struct gkd_ui_osd osd={text,0U,50,0};
        assert(!gkd_ui_export_osd_argb(tile,GKD_UI_OSD_WIDTH*GKD_UI_OSD_HEIGHT,&config,&cn,&osd));
        const unsigned char *bits=compact.data+compact.headersize+index*compact.charsize;
        uint32_t ink=argb_from_565(config.normal,255U);
        for(unsigned y=0;y<12U;y++)for(unsigned x=0;x<12U;x++)
            assert((tile[(y+3U)*GKD_UI_OSD_WIDTH+x+26U]==ink)==
                   !!(bits[y*2U+x/8U]&(0x80U>>(x&7U))));
    }
    compact.compact=&cn;assert(!valid_font(&cn));compact.compact=NULL;
    cn.compact=&cn;assert(!valid_font(&cn));cn.compact=&compact;
    gkd_ui_font_release(&compact);gkd_ui_font_release(&cn);gkd_ui_font_release(&latin);
    puts("NATIVE_FONT_PASS catalog-glyphs-native14/12 title/body/button/OSD/mixed-width/all-ASCII-parity/OSD-rail-bounds");
    return 0;
}
