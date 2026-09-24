/* SPDX-License-Identifier: GPL-2.0 */
#define _POSIX_C_SOURCE 200809L
#include "gkd-ui.h"
#include "gkd-ui-plane-client.h"
#include "gkd-input-owner.h"
#include "gkd-input-keys.h"
#include "gkd-menu-state.h"
#include "gkd-settings-state.h"
#include "gkd-settings-client.h"
#include "gkd-ui-language.h"
#include "gkd-controls-command.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#define POLL_MS 20
#define LEASE_MS 2000U
#define RENEW_MS 500U
#define RELEASE_MS 1000U
#define WORD_BITS (sizeof(unsigned long) * 8U)
static volatile sig_atomic_t stopped;
static void stop(int number) { (void)number; stopped = 1; }
static int now_ms(uint64_t *value)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) || t.tv_sec < 0) return -1;
    *value = (uint64_t)t.tv_sec * 1000U + (uint64_t)t.tv_nsec / 1000000U;
    return 0;
}
static int snapshot(struct gkd_menu_state *s, unsigned source, int fd, const unsigned short *keys, unsigned key_count)
{
    unsigned long bits[(KEY_MAX + WORD_BITS) / WORD_BITS];
    unsigned held = 0, i;
    memset(bits, 0, sizeof(bits));
    if (ioctl(fd, EVIOCGKEY(sizeof(bits)), bits) < 0) return -1;
    for (i = 0; i < key_count; ++i)
        if (bits[keys[i] / WORD_BITS] & (1UL << (keys[i] % WORD_BITS))) held |= 1U << i;
    return gkd_menu_state_snapshot(s, source, held);
}
static int brightness_request(void)
{
    struct sockaddr_un address={.sun_family=AF_UNIX};
    char command=GKD_CONTROLS_COMMAND_BRIGHTNESS;
    int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0),saved;
    if(fd<0)return -1;
    strcpy(address.sun_path,GKD_CONTROLS_COMMAND_SOCKET);
    int result=sendto(fd,&command,1,MSG_NOSIGNAL,(struct sockaddr *)&address,sizeof(address))==1?0:-1;
    saved=errno;(void)close(fd);errno=saved;return result;
}
static int drain(struct gkd_menu_state *s, unsigned source, int fd, const unsigned short *keys, unsigned key_count, struct gkd_settings_state *settings,unsigned held[2])
{
    struct input_event events[32];
    unsigned batch;
    /* Bound hostile/flooding sources; the outer loop still services deadlines. */
    for (batch = 0; batch < 4U; ++batch) {
        ssize_t n = read(fd, events, sizeof(events));
        unsigned i;
        if (n < 0 && errno == EINTR) return 0;
        if (n < 0 && errno == EAGAIN) {
            return s->dropped[source] == 2U ? snapshot(s, source, fd, keys, key_count) : 0;
        }
        if (n <= 0 || n % (ssize_t)sizeof(events[0])) return -1;
        for (i = 0; i < (unsigned)(n / (ssize_t)sizeof(events[0])); ++i) {
            struct input_event *e = &events[i]; unsigned k;
            if (e->type == EV_SYN && e->code == SYN_DROPPED) {
                held[source]=0;
                if (gkd_menu_state_drop(s, source)) return -1;
            } else if (s->dropped[source]) {
                if (e->type == EV_SYN && e->code == SYN_REPORT) {
                    /* Drain all already-queued records before EVIOCGKEY;
                     * otherwise stale post-report presses could confirm. */
                    s->dropped[source] = 2U;
                }
            } else if (e->type == EV_KEY) {
                if(e->code==KEY_END){
                    if(e->value==1&&!held[source]){
                        if(!held[0]&&!held[1]&&brightness_request())return -1;
                        held[source]=1;
                    }else if(e->value==0)held[source]=0;
                    continue;
                }
                for (k = 0; k < key_count; ++k) if (e->code == keys[k]) {
                    if ((settings ? gkd_settings_state_key(settings, source, k, e->value) :
                         gkd_menu_state_key(s, source, k, e->value)) < 0) return -1;
                    break;
                }
            }
        }
    }
    return 0;
}
static int input_cycle(struct gkd_menu_state *s, struct gkd_input_owner *owner,
                       const unsigned short *keys, unsigned key_count, struct gkd_settings_state *settings,unsigned held[2],int delay)
{
    struct pollfd p[2] = {{owner->physical_fd, POLLIN, 0}, {owner->virtual_fd, POLLIN, 0}};
    int ready = poll(p, 2, delay); unsigned i;
    if (ready < 0) return errno == EINTR ? 0 : -1;
    for (i = 0; i < 2U; ++i) {
        if (p[i].revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;
        if (((p[i].revents & POLLIN) || s->dropped[i] == 2U) && drain(s, i, p[i].fd, keys, key_count, settings,held)) return -1;
    }
    return 0;
}
/* Reserved legacy duration: accepted for ABI compatibility, never a UI deadline. */
static int parse_legacy_duration(const char *text)
{
    unsigned n = 0; const unsigned char *p = (const unsigned char *)text;
    if (!p || !*p) return -1;
    for (; *p; ++p) { if (*p < '0' || *p > '9' || n > 300000U / 10U) return -1; n = n * 10U + *p - '0'; }
    if (n > 300000U) return -1;
    return 0;
}

static int number(const char *text,unsigned maximum,unsigned *value)
{
    unsigned n=0;const unsigned char *p=(const unsigned char *)text;
    if(!p||!*p||(*p=='0'&&p[1]))return -1;
    for(;*p;++p){if(*p<'0'||*p>'9'||n>maximum/10U)return -1;n=n*10U+*p-'0';}
    if(n>maximum)return -1;
    *value=n;return 0;
}
static int generation(const char *text)
{
    if(!text||strlen(text)!=64U)return 0;
    for(unsigned i=0;i<64U;++i)if(!((text[i]>='0'&&text[i]<='9')||(text[i]>='a'&&text[i]<='f')))return 0;
    return 1;
}
static int save_result(const char *reply)
{
    static const char prefix[]="GKD_APP_SETTINGS=SAVED generation=";
    if(!strcmp(reply,"GKD_APP_SETTINGS=SAVING\n"))return 0;
    if(!strncmp(reply,prefix,sizeof(prefix)-1U)){
        char hash[65];size_t n=sizeof(prefix)-1U;
        if(strlen(reply)!=n+65U||reply[n+64U]!='\n')return -2;
        memcpy(hash,reply+n,64U);hash[64]=0;return generation(hash)?1:-2;
    }
    const char *kind="GKD_APP_SETTINGS=FAILED state=recoverable errno=";
    int recoverable=1;
    if(strncmp(reply,kind,strlen(kind))){
        kind="GKD_APP_SETTINGS=FAILED state=unknown errno=";recoverable=0;
        if(strncmp(reply,kind,strlen(kind)))return -2;
    }
    size_t n=strlen(kind),length=strlen(reply);char code[12];unsigned value;
    if(length<=n+1U||length-n-1U>=sizeof(code)||reply[length-1U]!='\n')return -2;
    memcpy(code,reply+n,length-n-1U);code[length-n-1U]=0;
    return number(code,4095U,&value)||!value?-2:recoverable?-1:-2;
}
int main(int argc, char **argv)
{
    struct gkd_ui_catalog texts={0};
    struct gkd_ui_menu_item items[GKD_SETTINGS_ROWS];
    struct gkd_ui_config config; struct gkd_ui_font font={0},cjk={0},compact={0};
    struct gkd_ui_surface surface; struct gkd_ui_menu menu;
    struct gkd_ui_text_layout text_layout;
    char update_body[192]; const char *text_title=NULL,*text_body=NULL;
    struct gkd_ui_menu_caps caps; struct gkd_input_owner owner;
    struct gkd_menu_state simple,*state=&simple;
    struct gkd_settings_state settings; struct gkd_settings_values values={0},drawn_values={0};
    struct sigaction action; struct stat st;
    unsigned short keys[6]; unsigned brightness_held[2]={0,0};unsigned loading_frame=0,transition,seq=0,drawn=99U,i,j,key_count=4U,language=0U,input_style=0U,required=0U;
    uint64_t now=0,previous=0,deadline=0,renew=0,hide_until=0,save_next=0;
    uint16_t *pixels=NULL; int fd=-1,result=1,published=0,acquired=0;
    int is_settings=argc>2&&!strcmp(argv[2],"SETTINGS"),is_update=argc>2&&!strcmp(argv[2],"UPDATE"),saving=0,saved=0;
    int is_text=argc>2&&!strcmp(argv[2],"TEXT"),is_confirmation=is_update||is_text;
    const char *cn_path=NULL,*compact_path=NULL,*expected=NULL,*socket_path=NULL;char request[160],reply[160];
    stopped=0;gkd_input_owner_init(&owner);
    /* SETTINGS: six keys, four values, CJK font, expected generation, socket.
     * Existing USB/POWER command shape remains supported; optional locale is explicit. */
    if((is_settings?argc!=20:is_confirmation?argc!=17:(argc!=9&&argc!=10&&argc!=13&&argc!=14&&argc!=15))||
       (!is_settings&&!is_confirmation&&strcmp(argv[2],"USB")&&strcmp(argv[2],"POWER"))||
       (strcmp(argv[3],"enabled")&&strcmp(argv[3],"disabled"))||parse_legacy_duration(argv[4])){
        fputs("usage: gkd-application-menu FONT USB|POWER EFFECTS RESERVED_DURATION UP DOWN A B [INITIAL [LANG CNFONT CNFONT12 [STYLE [REQUIRED]]]]\n"
              "       gkd-application-menu FONT TEXT EFFECTS RESERVED_DURATION UP DOWN A B INITIAL LANG CNFONT CNFONT12 STYLE REQUIRED TITLE BODY\n"
              "       gkd-application-menu FONT SETTINGS EFFECTS RESERVED_DURATION UP DOWN A B LEFT RIGHT ANIMATION SLEEP FPS LANG CNFONT CNFONT12 GENERATION SOCKET STYLE\n",stderr);return 2;
    }
    if(is_settings){
        key_count=6U;state=&settings.input;cn_path=argv[15];compact_path=argv[16];expected=argv[17];socket_path=argv[18];
        if(number(argv[11],1U,&values.animation)||number(argv[12],60U,&values.sleep_minutes)||
           number(argv[13],1U,&values.show_fps)||number(argv[14],1U,&values.chinese)||number(argv[19],2U,&values.input_style)||!generation(expected))return 2;
        language=values.chinese;input_style=values.input_style;
    }else if(argc>=13){
        if(number(argv[10],1U,&language))return 2;
        cn_path=argv[11];compact_path=argv[12];
        if(argc>=14&&number(argv[13],2U,&input_style))return 2;
        if(argc==15&&(number(argv[14],1U,&required)||(required&&strcmp(argv[2],"USB"))))return 2;
    }
    if(is_update){
        for(i=15U;i<17U;i++){
            if(!argv[i][0]||strlen(argv[i])>15U)return 2;
            for(const unsigned char *v=(const unsigned char *)argv[i];*v;v++)
                if(!((*v>='0'&&*v<='9')||(*v>='a'&&*v<='z')||(*v>='A'&&*v<='Z')||*v=='.'||*v=='-'||*v=='_'))return 2;
        }
    }
    for(i=0;i<key_count;++i){
        if(gkd_input_key_code(argv[5U+i],&keys[i]))return 2;
        for(j=0;j<i;++j)if(keys[i]==keys[j])return 2;
    }
    /* Freeze confirmation routing for the open editor, including during draft
     * style changes. New style and matching prompts apply on the next entry. */
    if(input_style){unsigned short swap=keys[2];keys[2]=keys[3];keys[3]=swap;}
    if(!is_settings&&argc>=10&&(argv[9][0]<'0'||argv[9][0]>'2'||argv[9][1]))return 2;
    transition=!strcmp(argv[3],"enabled")?200U:0U;
    gkd_ui_config_defaults(&config);config.input_style=input_style;
    if(gkd_ui_catalog_load(&texts,"/run/gkd-config/current/effective.conf")||gkd_ui_font_load(&font,argv[1])||
       (cn_path&&(gkd_ui_font_load(&cjk,cn_path)||gkd_ui_font_load(&compact,compact_path)))||
       (is_settings?gkd_settings_state_init(&settings,&values):gkd_menu_state_init(state,is_confirmation?1U:3U)))goto done;
    state->cancel_blocked=(int)required;
    if(cn_path){cjk.latin=compact.latin=&font;cjk.compact=&compact;}
    if(!is_settings&&!is_confirmation&&argc>=10)state->selected=(unsigned)(argv[9][0]-'0');
    if(is_confirmation){
        enum gkd_ui_language lang=language?GKD_UI_CN:GKD_UI_EN;
        if(is_update){
            /* Only the verified package adapter supplies the release values. */
            int length=snprintf(update_body,sizeof(update_body),"%s  %s\n%s  %s\n\n%s",
                gkd_ui_catalog_text(&texts,GKD_UI_EN,GKD_UI_TEXT_CURRENT_VERSION),argv[15],
                gkd_ui_catalog_text(&texts,GKD_UI_EN,GKD_UI_TEXT_TARGET_VERSION),argv[16],
                gkd_ui_catalog_text(&texts,GKD_UI_EN,GKD_UI_TEXT_CONFIRM_UPDATE));
            if(length<0||(size_t)length>=sizeof(update_body))goto done;
            text_title=gkd_ui_catalog_text(&texts,lang,GKD_UI_TEXT_SYSTEM_UPDATE);text_body=update_body;
        }else{text_title=argv[15];text_body=argv[16];}
        unsigned visible=gkd_ui_confirmation_visible(&config);
        if(!visible||gkd_ui_layout_text(&config,cn_path?&cjk:&font,text_body,&text_layout))goto done;
        state->count=text_layout.count>visible?text_layout.count-visible+1U:1U;
        state->scroll_only=1;
    }
    pixels=calloc(GKD_UI_MENU_PIXELS,sizeof(*pixels));if(!pixels)goto done;
    surface=(struct gkd_ui_surface){pixels,GKD_UI_MENU_WIDTH,GKD_UI_MENU_HEIGHT,GKD_UI_MENU_WIDTH};
    memset(&action,0,sizeof(action));action.sa_handler=stop;
    if(sigemptyset(&action.sa_mask)||sigaction(SIGINT,&action,NULL)||sigaction(SIGTERM,&action,NULL))goto done;
    fd=open("/dev/fb0",O_RDWR|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0||fstat(fd,&st)||!S_ISCHR(st.st_mode)||gkd_ui_menu_capabilities(fd,&caps)||
       stopped||gkd_input_owner_open_menu(&owner))goto done;
    acquired=1;state->dropped[0]=state->dropped[1]=2U;
    if(drain(state,0U,owner.physical_fd,keys,key_count,is_settings?&settings:NULL,brightness_held)||
       drain(state,1U,owner.virtual_fd,keys,key_count,is_settings?&settings:NULL,brightness_held)||now_ms(&now))goto done;
    previous=now;
    while((!stopped&&state->result==GKD_MENU_PENDING)||saving){
        if(now_ms(&now)||now<previous)goto done;
        previous=now;
        if(saving&&now>=save_next){
            int outcome=0;
            if(!gkd_settings_exchange(socket_path,"settings-status",reply,sizeof(reply)))outcome=save_result(reply);
            save_next=now+100U;
            if(outcome==1){saving=0;saved=1;break;}
            if(outcome==-2)goto done;
            if(outcome==-1){
                saving=0;state->result=GKD_MENU_PENDING;
                state->inhibited=!!(state->held[0]|state->held[1]|state->dropped[0]|state->dropped[1]);drawn=99U;
            }
        }
        if(drawn!=state->selected||now>=renew||
           (is_settings&&memcmp(&drawn_values,&settings.draft,sizeof(drawn_values)))){
            if(is_settings)language=settings.draft.chinese;
            enum gkd_ui_language lang=language?GKD_UI_CN:GKD_UI_EN;
            const char *yes_label=config.action_yes,*no_label=config.action_no;
            if(saving){
                gkd_ui_render_loading(&surface,&config,&font,loading_frame++);
            }else if(is_settings){
                static const enum gkd_ui_text labels[]={GKD_UI_TEXT_ANIMATION,GKD_UI_TEXT_AUTO_SLEEP,GKD_UI_TEXT_SHOW_FPS,GKD_UI_TEXT_LANGUAGE,GKD_UI_TEXT_INPUT_STYLE,GKD_UI_TEXT_SYSTEM_UPDATE};
                for(i=0;i<GKD_SETTINGS_ROWS;++i)items[i]=(struct gkd_ui_menu_item){gkd_ui_catalog_text(&texts,lang,labels[i]),i==GKD_SETTINGS_UPDATE_ROW?1U:0U};
                menu=(struct gkd_ui_menu){gkd_ui_catalog_text(&texts,lang,GKD_UI_TEXT_SETTINGS),items,GKD_SETTINGS_ROWS,state->selected,
                    saving?GKD_UI_ACTION_DISABLED:GKD_UI_ACTION_ENABLED,saving?GKD_UI_ACTION_DISABLED:GKD_UI_ACTION_ENABLED,yes_label,no_label};
                char sleep[4];snprintf(sleep,sizeof(sleep),"%u",settings.draft.sleep_minutes);
                const char *display[]={settings.draft.animation?"ON":"OFF",sleep,settings.draft.show_fps?"ON":"OFF",language?"CN":"EN",settings.draft.input_style==GKD_INPUT_RAW?"ORIG":settings.draft.input_style==GKD_INPUT_XBOX?"XBOX":"PS",NULL};
                /* Interactive rows return after a recoverable save failure. */
                if(gkd_ui_render_settings(&surface,&config,language?&cjk:&font,&menu,display,NULL))goto done;
            }else if(is_confirmation){
                struct gkd_ui_confirmation info={text_title,text_layout.lines,text_layout.count,state->selected,
                    GKD_UI_ACTION_ENABLED,GKD_UI_ACTION_ENABLED};
                if(gkd_ui_render_confirmation_info(&surface,&config,cn_path?&cjk:&font,&info))goto done;
            }else{
                static const enum gkd_ui_text usb[]={GKD_UI_TEXT_CHARGE,GKD_UI_TEXT_STORAGE,GKD_UI_TEXT_DEBUG};
                static const enum gkd_ui_text power[]={GKD_UI_TEXT_SUSPEND,GKD_UI_TEXT_REBOOT,GKD_UI_TEXT_SHUTDOWN};
                int is_power=!strcmp(argv[2],"POWER");
                for(i=0;i<3U;++i)items[i]=(struct gkd_ui_menu_item){gkd_ui_catalog_text(&texts,lang,is_power?power[i]:usb[i]),is_power?i+2U:i};
                menu=(struct gkd_ui_menu){gkd_ui_catalog_text(&texts,lang,is_power?GKD_UI_TEXT_POWER:GKD_UI_TEXT_USB_MODE),
                    items,3U,state->selected,GKD_UI_ACTION_ENABLED,required?GKD_UI_ACTION_DISABLED:GKD_UI_ACTION_ENABLED,
                    yes_label,no_label};
                if(gkd_ui_render_menu(&surface,&config,language?&cjk:&font,&menu))goto done;
            }
            if(seq==UINT32_MAX||gkd_ui_menu_send(fd,pixels,LEASE_MS,saving?0U:transition,++seq))goto done;
            if(!published){printf("GKD_MENU_READY kind=%s\n",argv[2]);if(fflush(stdout))goto done;}
            published=1;drawn=state->selected;if(is_settings)drawn_values=settings.draft;renew=now+(saving?config.loading_interval_ms:RENEW_MS);
        }
        if(input_cycle(state,&owner,keys,key_count,is_settings?&settings:NULL,brightness_held,POLL_MS))goto done;
        if(is_settings&&!saving&&state->result==GKD_MENU_SELECTED&&!stopped){
            snprintf(request,sizeof(request),"settings-save %s %u %u %u %u %u",expected,
                settings.draft.animation,settings.draft.sleep_minutes,settings.draft.show_fps,settings.draft.chinese,settings.draft.input_style);
            /* A lost acknowledgement may follow a committed transaction: query status
             * instead of issuing another save or releasing the input lease. */
            /* Publish the shared spinner before starting persistence, including
             * saves that finish before the first status poll. */
            gkd_ui_render_loading(&surface,&config,&font,loading_frame++);
            if(seq==UINT32_MAX||gkd_ui_menu_send(fd,pixels,LEASE_MS,0U,++seq))goto done;
            (void)gkd_settings_exchange(socket_path,request,reply,sizeof(reply));
            saving=1;save_next=now;drawn=99U;
        }
    }
    if(stopped&&!saved)state->result=GKD_MENU_CANCELLED;
    if(published){
        if(gkd_ui_menu_hide(fd)||now_ms(&now))goto done;
        hide_until=now+(saved?0U:transition)+40U;deadline=hide_until+RELEASE_MS;
        for(;;){
            if(now_ms(&now)||now<previous)goto done;
            previous=now;
            if(now>=hide_until&&!(state->held[0]|state->held[1]|state->dropped[0]|state->dropped[1]))break;
            if(now>=deadline&&!is_settings){state->result=GKD_MENU_CANCELLED;break;}
            if(input_cycle(state,&owner,keys,key_count,is_settings?&settings:NULL,brightness_held,POLL_MS))goto done;
        }
        if(gkd_ui_menu_clear(fd))goto done;
        published=0;
    }
    if(gkd_input_owner_close(&owner)){acquired=0;goto done;}acquired=0;
    if(is_settings&&saved)printf("GKD_MENU_RESULT=%s kind=SETTINGS\n",state->selected==GKD_SETTINGS_UPDATE_ROW?"UPDATE":"SAVED");
    /* Keep the update service action receipt; scrolling is never an action index. */
    else if(is_update&&state->result==GKD_MENU_SELECTED&&!stopped)printf("GKD_MENU_RESULT=SELECTED kind=UPDATE index=2\n");
    else if(is_text&&state->result==GKD_MENU_SELECTED&&!stopped)printf("GKD_MENU_RESULT=CONFIRMED kind=TEXT\n");
    else if(state->result==GKD_MENU_SELECTED&&!stopped)printf("GKD_MENU_RESULT=SELECTED kind=%s index=%u\n",argv[2],state->selected);
    else printf("GKD_MENU_RESULT=CANCELLED kind=%s\n",argv[2]);
    result=0;
done:
    if(published&&gkd_ui_menu_clear(fd))result=1;
    if(acquired&&gkd_input_owner_close(&owner))result=1;
    if(fd>=0)(void)close(fd);
    free(pixels);gkd_ui_font_release(&compact);gkd_ui_font_release(&cjk);gkd_ui_font_release(&font);
    if(result==1)fprintf(stderr,"GKD_MENU_FAILED errno=%d\n",errno);
    return result;
}
