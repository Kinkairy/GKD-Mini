/* SPDX-License-Identifier: GPL-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint8_t u8;
#define BIT(n) (1U<<(n))
struct mutex {int held;};
static void mutex_lock(struct mutex *m){assert(!m->held);m->held=1;}
static void mutex_unlock(struct mutex *m){assert(m->held);m->held=0;}
struct i2c_client {int unused;};
struct gkd_axp173 {struct i2c_client *client;struct mutex lock;bool telemetry_suspended;};
struct power_supply {struct gkd_axp173 *data;};
static void *power_supply_get_drvdata(struct power_supply *p){return p->data;}
enum power_supply_property {POWER_SUPPLY_PROP_VOLTAGE_NOW,POWER_SUPPLY_PROP_ONLINE,UNKNOWN};
union power_supply_propval {int intval;};
static u8 registers[256];
static int fail_register=-1,reads;
static struct gkd_axp173 *active;
static int i2c_smbus_read_byte_data(struct i2c_client *c,u8 r)
{
 assert(c==active->client&&active->lock.held);
 reads++;
 return r==fail_register?-EREMOTEIO:registers[r];
}
/* DRIVER_FRAGMENT */
int main(void)
{
 struct i2c_client client={0};struct gkd_axp173 axp={.client=&client};
 struct power_supply psy={&axp};union power_supply_propval v={0};active=&axp;
 /* Literal hardware addresses/masks here deliberately differ from using
  * driver macros: a changed register or BIT(5) would fail this contract. */
 registers[0x78]=0xd2;registers[0x79]=4;
 assert(!gkd_axp173_battery_get_property(&psy,POWER_SUPPLY_PROP_VOLTAGE_NOW,&v)&&v.intval==3700400&&reads==2);
 registers[0x78]=0;registers[0x79]=0;
 assert(gkd_axp173_battery_get_property(&psy,POWER_SUPPLY_PROP_VOLTAGE_NOW,&v)==-ENODATA);
 registers[0x78]=0xff;registers[0x79]=0xff;
 assert(!gkd_axp173_battery_get_property(&psy,POWER_SUPPLY_PROP_VOLTAGE_NOW,&v)&&v.intval==4504500);
 fail_register=0x79;
 assert(gkd_axp173_battery_get_property(&psy,POWER_SUPPLY_PROP_VOLTAGE_NOW,&v)==-EREMOTEIO);
 fail_register=-1;
 registers[0x00]=0x10;
 assert(!gkd_axp173_usb_get_property(&psy,POWER_SUPPLY_PROP_ONLINE,&v)&&v.intval==1);
 registers[0x00]=0x20;
 assert(!gkd_axp173_usb_get_property(&psy,POWER_SUPPLY_PROP_ONLINE,&v)&&v.intval==0);
 fail_register=0x00;
 assert(gkd_axp173_usb_get_property(&psy,POWER_SUPPLY_PROP_ONLINE,&v)==-EREMOTEIO);
 fail_register=-1;
 int before=reads;axp.telemetry_suspended=true;
 assert(gkd_axp173_usb_get_property(&psy,POWER_SUPPLY_PROP_ONLINE,&v)==-EAGAIN);
 assert(gkd_axp173_battery_get_property(&psy,POWER_SUPPLY_PROP_VOLTAGE_NOW,&v)==-EAGAIN);
 assert(reads==before&&!axp.lock.held);
 axp.telemetry_suspended=false;
 assert(!gkd_axp173_usb_get_property(&psy,POWER_SUPPLY_PROP_ONLINE,&v)&&v.intval==0);
 assert(gkd_axp173_battery_get_property(&psy,UNKNOWN,&v)==-EINVAL);
 assert(gkd_axp173_usb_get_property(&psy,UNKNOWN,&v)==-EINVAL);
 assert(!axp.lock.held);
 puts("GKD_APP_POWER_FIXTURE=PASS actual-registers/voltage/usb-valid/I2C-failure/locking/suspend");
 return 0;
}
