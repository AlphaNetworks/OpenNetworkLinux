/*
 * SFP driver for alphanetworks snj61d0-320f sfp
 *
 * Copyright (C) 2020 Alphanetworks Technology Corporation.
 * Philip Wang <philip_wang@alphanetworks.com>
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * any later version. 

 * This program is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * GNU General Public License for more details. 
 * see <http://www.gnu.org/licenses/>
 *
 * Copyright (C)  Brandon Chuang <brandon_chuang@accton.com.tw>
 *
 * Based on ad7414.c
 * Copyright 2006 Stefan Roese <sr at denx.de>, DENX Software Engineering
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#define DRIVER_NAME 	"snj61d0-320f_sfp"

#define DEBUG_MODE 0

#if (DEBUG_MODE == 1)
	#define DEBUG_PRINT(fmt, args...)                                        \
		printk (KERN_INFO "[%s,%d]: " fmt "\r\n", __FUNCTION__, __LINE__, ##args)
#else
	#define DEBUG_PRINT(fmt, args...)
#endif

#define NUM_OF_SFP_PORT 		32
#define EEPROM_NAME 			"sfp_eeprom"
#define EEPROM_SIZE				256	/*  256 byte eeprom */
#define BIT_INDEX(i) 			(1ULL << (i))
#define USE_I2C_BLOCK_READ 		1
#define I2C_RW_RETRY_COUNT		3
#define I2C_RW_RETRY_INTERVAL	100 /* ms */

#define SFP_CPLD_I2C_ADDR	0x5F
//#define SFP_FPGA_I2C_ADDR       0x5E
#define SFP_EEPROM_A0_I2C_ADDR	0x50
#define SFP_EEPROM_A2_I2C_ADDR	0x68

#define SFPPLUS_1_PORT_NUMBER   32
#define SFPPLUS_2_PORT_NUMBER   33

#define SFPPLUS_PRESENT_ADDRESS         0x28

#define SFP_CPLD_REG_ADDR_REVISION      0x00
#define SFP_CPLD_REG_ADDR_PRESENT       0x03
#define SFP_CPLD_REG_ADDR_RESET         0x04
#define SFP_CPLD_REG_ADDR_LOWPOWERMODE  0x05
#define SFP_CPLD_REG_ADDR_MODSELECT     0x06
#define SFP_CPLD_REG_ADDR_LED           0x07

#define SFP_CPLD_REVISION_BIT           0xF

u8 portCPLDID_0[]={13,14,15,16};
u8 sfpPlusID_0[]={12,11};
u8 portCPLDID_1[]={14,15,16,17};
u8 sfpPlusID_1[]={13,12};
u8 *portCPLDID;
u8 *sfpPlusID;


#define SFF8024_PHYSICAL_DEVICE_ID_ADDR		0x0
#define SFF8024_DEVICE_ID_SFP				0x3
#define SFF8024_DEVICE_ID_QSFP				0xC
#define SFF8024_DEVICE_ID_QSFP_PLUS			0xD
#define SFF8024_DEVICE_ID_QSFP28			0x11
#define SFF8024_DEVICE_ID_QSFPDD                0x18

#define SFF8472_DIAG_MON_TYPE_ADDR        	92
#define SFF8472_DIAG_MON_TYPE_DDM_MASK		0x40
#define SFF8472_10G_ETH_COMPLIANCE_ADDR		0x3
#define SFF8472_10G_BASE_MASK				0xF0

#define SFF8436_RX_LOS_ADDR					3
#define SFF8436_TX_FAULT_ADDR				4
#define SFF8436_TX_DISABLE_ADDR				86

static ssize_t sfp_eeprom_read(struct i2c_client *, u8, u8 *,int);
static ssize_t sfp_eeprom_write(struct i2c_client *, u8 , const char *,int);
/* extern int alpha_i2c_cpld_read(unsigned short cpld_addr, u8 reg); */
extern int alpha_i2c_fpga_write(unsigned short cpld_addr, u8 reg, u8 value);

/* Addresses scanned
 */
static const unsigned short normal_i2c[] = { SFP_EEPROM_A0_I2C_ADDR, SFP_EEPROM_A2_I2C_ADDR, SFP_CPLD_I2C_ADDR, I2C_CLIENT_END };

#define CPLD_PORT_TO_FRONT_PORT(port)  (port+1)

enum port_numbers {
sfp1,  sfp2,  sfp3,  sfp4,  sfp5,  sfp6,  sfp7,  sfp8, 
sfp9,  sfp10, sfp11, sfp12, sfp13, sfp14, sfp15, sfp16,
sfp17, sfp18, sfp19, sfp20, sfp21, sfp22, sfp23, sfp24,
sfp25, sfp26, sfp27, sfp28, sfp29, sfp30, sfp31, sfp32,
sfp33, sfp34
};

static const struct i2c_device_id sfp_device_id[] = {
{ "sfpcpld1",  sfp1 },  { "sfpcpld2",  sfp2 },  { "sfpcpld3",  sfp3 },  { "sfpcpld4",  sfp4 },
{ "sfpcpld5",  sfp5 },  { "sfpcpld6",  sfp6 },  { "sfpcpld7",  sfp7 },  { "sfpcpld8",  sfp8 },
{ "sfpcpld9",  sfp9 },  { "sfpcpld10", sfp10 }, { "sfpcpld11", sfp11 }, { "sfpcpld12", sfp12 },
{ "sfpcpld13", sfp13 }, { "sfpcpld14", sfp14 }, { "sfpcpld15", sfp15 }, { "sfpcpld16", sfp16 },
{ "sfpcpld17", sfp17 }, { "sfpcpld18", sfp18 }, { "sfpcpld19", sfp19 }, { "sfpcpld20", sfp20 },
{ "sfpcpld21", sfp21 }, { "sfpcpld22", sfp22 }, { "sfpcpld23", sfp23 }, { "sfpcpld24", sfp24 },
{ "sfpcpld25", sfp25 }, { "sfpcpld26", sfp26 }, { "sfpcpld27", sfp27 }, { "sfpcpld28", sfp28 },
{ "sfpcpld29", sfp29 }, { "sfpcpld30", sfp30 }, { "sfpcpld31", sfp31 }, { "sfpcpld32", sfp32 },
{ "sfpcpld33", sfp33 }, { "sfpcpld34", sfp34 },
{}
};
MODULE_DEVICE_TABLE(i2c, sfp_device_id);

/* 
 * list of valid port types
 * note OOM_PORT_TYPE_NOT_PRESENT to indicate no 
 * module is present in this port
 */
typedef enum oom_driver_port_type_e {
	OOM_DRIVER_PORT_TYPE_INVALID,
	OOM_DRIVER_PORT_TYPE_NOT_PRESENT,
	OOM_DRIVER_PORT_TYPE_SFP,
	OOM_DRIVER_PORT_TYPE_SFP_PLUS,
	OOM_DRIVER_PORT_TYPE_QSFP,
	OOM_DRIVER_PORT_TYPE_QSFP_PLUS,
	OOM_DRIVER_PORT_TYPE_QSFP28,
	OOM_DRIVER_PORT_TYPE_QSFPDD
} oom_driver_port_type_t;

enum driver_type_e {
	DRIVER_TYPE_SFP_MSA,
	DRIVER_TYPE_SFP_DDM,
	DRIVER_TYPE_QSFP
};

/* Each client has this additional data
 */
struct eeprom_data {
	char				 valid;			/* !=0 if registers are valid */
	unsigned long		 last_updated;	/* In jiffies */
	struct bin_attribute bin;			/* eeprom data */		
};

struct sfp_msa_data {
	char			valid;		  	/* !=0 if registers are valid */
	unsigned long	last_updated;   /* In jiffies */
	u64				status[2];	   	/* index 0 => device id
											 1 => 10G Ethernet Compliance Codes 
												  to distinguish SFP or SFP+ 
											 2 => DIAGNOSTIC MONITORING TYPE */
	struct eeprom_data				eeprom;
};

struct sfp_ddm_data {
	char			valid;			/* !=0 if registers are valid */
	unsigned long	last_updated; 	/* In jiffies */
	u64				status[3];	  	/* bit0:port0, bit1:port1 and so on */
									/* index 0 => tx_fail
											 1 => tx_disable
											 2 => rx_loss */
	struct eeprom_data				eeprom;
};

struct qsfp_data {
	char			valid;		  	/* !=0 if registers are valid */
	unsigned long	last_updated;   /* In jiffies */
	u8				status[3];	  	/* bit0:port0, bit1:port1 and so on */
									/* index 0 => tx_fail
											 1 => tx_disable
											 2 => rx_loss */

	u8					device_id;	
	struct eeprom_data	eeprom;
};

struct sfp_port_data {
	struct mutex		   update_lock;
	enum driver_type_e     driver_type;
	int 				   port;		/* CPLD port index */
	oom_driver_port_type_t port_type;
	u64					   present;   /* present status, bit0:port0, bit1:port1 and so on */
	u64					   port_reset;   /* reset status, bit0:port0, bit1:port1 and so on */
	u64					   port_lpmode;   /* lpmode status, bit0:port0, bit1:port1 and so on */
	u64					   port_led;   /* port led status, bit0:port0, bit1:port1 and so on */

	struct sfp_msa_data	  *msa;
	struct sfp_ddm_data   *ddm;
	struct qsfp_data 	  *qsfp;

	struct i2c_client 	  *client;
};

enum sfp_sysfs_port_num_attributes {
	PORT1_NUMBER,
	PORT2_NUMBER,
	PORT3_NUMBER,
	PORT4_NUMBER,
	PORT5_NUMBER,
	PORT6_NUMBER,
	PORT7_NUMBER,
	PORT8_NUMBER,
	PORT_NUMBER_MAX
};

enum sfp_sysfs_present_attributes {
	PORT1_PRESENT,
	PORT2_PRESENT,
	PORT3_PRESENT,
	PORT4_PRESENT,
	PORT5_PRESENT,
	PORT6_PRESENT,
	PORT7_PRESENT,
	PORT8_PRESENT,
	PORT1_PRESENT_ALL,
	PORT2_PRESENT_ALL,
	PORT3_PRESENT_ALL,
	PORT4_PRESENT_ALL,
	PORT5_PRESENT_ALL,
	PORT6_PRESENT_ALL,
	PORT7_PRESENT_ALL,
	PORT8_PRESENT_ALL,
	PORT_PRESENT_MAX
};

enum sfp_sysfs_type_attributes {
	PORT1_TYPE,
	PORT2_TYPE,
	PORT3_TYPE,
	PORT4_TYPE,
	PORT5_TYPE,
	PORT6_TYPE,
	PORT7_TYPE,
	PORT8_TYPE,
	PORT_TYPE_MAX
};

enum sfp_sysfs_reset_attributes {
	PORT1_RESET,
	PORT2_RESET,
	PORT3_RESET,
	PORT4_RESET,
	PORT5_RESET,
	PORT6_RESET,
	PORT7_RESET,
	PORT8_RESET,
	PORT_RESET_MAX
};

enum sfp_sysfs_rx_los_attributes {
	PORT1_RX_LOS,
	PORT2_RX_LOS,
	PORT3_RX_LOS,
	PORT4_RX_LOS,
	PORT5_RX_LOS,
	PORT6_RX_LOS,
	PORT7_RX_LOS,
	PORT8_RX_LOS,
	PORT_RX_LOS_MAX
};	

enum sfp_sysfs_rx_los1_attributes {
	PORT1_RX_LOS1 = PORT_RX_LOS_MAX,
	PORT2_RX_LOS1,
	PORT3_RX_LOS1,
	PORT4_RX_LOS1,
	PORT5_RX_LOS1,
	PORT6_RX_LOS1,
	PORT7_RX_LOS1,
	PORT8_RX_LOS1,
	PORT_RX_LOS1_MAX
};

enum sfp_sysfs_rx_los2_attributes {
	PORT1_RX_LOS2 = PORT_RX_LOS1_MAX,
	PORT2_RX_LOS2,
	PORT3_RX_LOS2,
	PORT4_RX_LOS2,
	PORT5_RX_LOS2,
	PORT6_RX_LOS2,
	PORT7_RX_LOS2,
	PORT8_RX_LOS2,
	PORT_RX_LOS2_MAX
};

enum sfp_sysfs_rx_los3_attributes {
	PORT1_RX_LOS3 = PORT_RX_LOS2_MAX,
	PORT2_RX_LOS3,
	PORT3_RX_LOS3,
	PORT4_RX_LOS3,
	PORT5_RX_LOS3,
	PORT6_RX_LOS3,
	PORT7_RX_LOS3,
	PORT8_RX_LOS3,
	PORT_RX_LOS3_MAX
};

enum sfp_sysfs_rx_los4_attributes {
	PORT1_RX_LOS4 = PORT_RX_LOS3_MAX,
	PORT2_RX_LOS4,
	PORT3_RX_LOS4,
	PORT4_RX_LOS4,
	PORT5_RX_LOS4,
	PORT6_RX_LOS4,
	PORT7_RX_LOS4,
	PORT8_RX_LOS4,
	PORT_RX_LOS4_MAX
};

enum sfp_sysfs_rx_los_all_attributes {
	PORT1_RX_LOS_ALL = PORT_RX_LOS4_MAX,
	PORT2_RX_LOS_ALL,
	PORT3_RX_LOS_ALL,
	PORT4_RX_LOS_ALL,
	PORT5_RX_LOS_ALL,
	PORT6_RX_LOS_ALL,
	PORT7_RX_LOS_ALL,
	PORT8_RX_LOS_ALL,
	PORT_RX_LOS_ALL_MAX
};

enum sfp_sysfs_tx_disable_attributes {
	PORT1_TX_DISABLE = PORT_RX_LOS_ALL_MAX,
	PORT2_TX_DISABLE,
	PORT3_TX_DISABLE,
	PORT4_TX_DISABLE,
	PORT5_TX_DISABLE,
	PORT6_TX_DISABLE,
	PORT7_TX_DISABLE,
	PORT8_TX_DISABLE,
	PORT_TX_DISABLE_MAX
};

enum sfp_sysfs_tx_disable1_attributes {
	PORT1_TX_DISABLE1 = PORT_TX_DISABLE_MAX,
	PORT2_TX_DISABLE1,
	PORT3_TX_DISABLE1,
	PORT4_TX_DISABLE1,
	PORT5_TX_DISABLE1,
	PORT6_TX_DISABLE1,
	PORT7_TX_DISABLE1,
	PORT8_TX_DISABLE1,
	PORT_TX_DISABLE1_MAX
};

enum sfp_sysfs_tx_disable2_attributes {
	PORT1_TX_DISABLE2 = PORT_TX_DISABLE1_MAX,
	PORT2_TX_DISABLE2,
	PORT3_TX_DISABLE2,
	PORT4_TX_DISABLE2,
	PORT5_TX_DISABLE2,
	PORT6_TX_DISABLE2,
	PORT7_TX_DISABLE2,
	PORT8_TX_DISABLE2,
	PORT_TX_DISABLE2_MAX
};

enum sfp_sysfs_tx_disable3_attributes {
	PORT1_TX_DISABLE3 = PORT_TX_DISABLE2_MAX,
	PORT2_TX_DISABLE3,
	PORT3_TX_DISABLE3,
	PORT4_TX_DISABLE3,
	PORT5_TX_DISABLE3,
	PORT6_TX_DISABLE3,
	PORT7_TX_DISABLE3,
	PORT8_TX_DISABLE3,
	PORT_TX_DISABLE3_MAX
};

enum sfp_sysfs_tx_disable4_attributes {
	PORT1_TX_DISABLE4 = PORT_TX_DISABLE3_MAX,
	PORT2_TX_DISABLE4,
	PORT3_TX_DISABLE4,
	PORT4_TX_DISABLE4,
	PORT5_TX_DISABLE4,
	PORT6_TX_DISABLE4,
	PORT7_TX_DISABLE4,
	PORT8_TX_DISABLE4,
	PORT_TX_DISABLE4_MAX
};
	
enum sfp_sysfs_tx_fault_attributes {
	PORT1_TX_FAULT = PORT_TX_DISABLE4_MAX,
	PORT2_TX_FAULT,
	PORT3_TX_FAULT,
	PORT4_TX_FAULT,
	PORT5_TX_FAULT,
	PORT6_TX_FAULT,
	PORT7_TX_FAULT,
	PORT8_TX_FAULT,
	PORT_TX_FAULT_MAX
};

enum sfp_sysfs_tx_fault1_attributes {
	PORT1_TX_FAULT1 = PORT_TX_FAULT_MAX,
	PORT2_TX_FAULT1,
	PORT3_TX_FAULT1,
	PORT4_TX_FAULT1,
	PORT5_TX_FAULT1,
	PORT6_TX_FAULT1,
	PORT7_TX_FAULT1,
	PORT8_TX_FAULT1,
	PORT_TX_FAULT1_MAX
};

enum sfp_sysfs_tx_fault2_attributes {
	PORT1_TX_FAULT2 = PORT_TX_FAULT1_MAX,
	PORT2_TX_FAULT2,
	PORT3_TX_FAULT2,
	PORT4_TX_FAULT2,
	PORT5_TX_FAULT2,
	PORT6_TX_FAULT2,
	PORT7_TX_FAULT2,
	PORT8_TX_FAULT2,
	PORT_TX_FAULT2_MAX
};

enum sfp_sysfs_tx_fault3_attributes {
	PORT1_TX_FAULT3 = PORT_TX_FAULT2_MAX,
	PORT2_TX_FAULT3,
	PORT3_TX_FAULT3,
	PORT4_TX_FAULT3,
	PORT5_TX_FAULT3,
	PORT6_TX_FAULT3,
	PORT7_TX_FAULT3,
	PORT8_TX_FAULT3,
	PORT_TX_FAULT3_MAX
};

enum sfp_sysfs_tx_fault4_attributes {
	PORT1_TX_FAULT4 = PORT_TX_FAULT3_MAX,
	PORT2_TX_FAULT4,
	PORT3_TX_FAULT4,
	PORT4_TX_FAULT4,
	PORT5_TX_FAULT4,
	PORT6_TX_FAULT4,
	PORT7_TX_FAULT4,
	PORT8_TX_FAULT4,
	PORT_TX_FAULT4_MAX
};

enum sfp_sysfs_lpmode_attributes {
	PORT1_LPMODE,
	PORT2_LPMODE,
	PORT3_LPMODE,
	PORT4_LPMODE,
	PORT5_LPMODE,
	PORT6_LPMODE,
	PORT7_LPMODE,
	PORT8_LPMODE,
	PORT_LPMODE_MAX
};

enum sfp_sysfs_eeprom_attributes {
	PORT1_EEPROM,
	PORT2_EEPROM,
	PORT3_EEPROM,
	PORT4_EEPROM,
	PORT5_EEPROM,
	PORT6_EEPROM,
	PORT7_EEPROM,
	PORT8_EEPROM,
	PORT_EEPROM_MAX
};

enum sfp_sysfs_ddm_implemented_attributes {
	PORT1_DDM_IMPLEMENTED,
	PORT2_DDM_IMPLEMENTED,
	PORT3_DDM_IMPLEMENTED,
	PORT4_DDM_IMPLEMENTED,
	PORT5_DDM_IMPLEMENTED,
	PORT6_DDM_IMPLEMENTED,
	PORT7_DDM_IMPLEMENTED,
	PORT8_DDM_IMPLEMENTED,
	PORT_DDM_IMPLEMENTED_MAX
};

enum sfp_sysfs_port_led_attributes {
	PORT_LED,
	PORT_LED_MAX
};

enum sfp_sysfs_cpld_revision_attributes {
	CPLD_REVISION,
	CPLD_REVISION_MAX
};

static ssize_t show_cpld_version(struct device *dev, struct device_attribute *attr, char *buf)
{
    int val = 0;
    struct i2c_client *client = to_i2c_client(dev);

	if (client->addr == SFP_CPLD_I2C_ADDR)
	{
	    val = i2c_smbus_read_byte_data(client, SFP_CPLD_REG_ADDR_REVISION);

	    if (val < 0) {
	        dev_dbg(&client->dev, "cpld(0x%x) reg(0x0) err %d\n", client->addr, val);
	    }
	}

    return sprintf(buf, "%d\n", (val & SFP_CPLD_REVISION_BIT));
}

static ssize_t show_port_number(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sfp_port_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

	DEBUG_PRINT("show_port_number port number:%d", data->port + attr->index);
	return sprintf(buf, "%d\n", CPLD_PORT_TO_FRONT_PORT(data->port + attr->index));
}

static struct sfp_port_data *sfp_plus_update_present(struct i2c_client *client)
{
	struct sfp_port_data *data = i2c_get_clientdata(client);
	int i = 0;
	int status = -1;
    u8 regs[] = {SFPPLUS_PRESENT_ADDRESS};

    DEBUG_PRINT("Starting sfp+ present status update");
	mutex_lock(&data->update_lock);
	
	/* Read present status of port */
    data->present = 0;
	
    for (i = 0; i < ARRAY_SIZE(regs); i++) {
        /* status = alpha_i2c_cpld_read(0x5f, regs[i]); */
		status = i2c_smbus_read_byte_data(client, regs[i]);
        
        if (status < 0) {
            DEBUG_PRINT("SFP+ reg(0x%x) err %d", regs[i], status);
            goto exit;
        }

        DEBUG_PRINT("Status:%d", status);
        
        if(data->port == SFPPLUS_1_PORT_NUMBER)
            data->present = ((status & 0x2) == 0)?0x1:0;
        else
            data->present = ((status & 0x1) == 0)?0x2:0;
    }

	DEBUG_PRINT("Present status = 0x%llx", data->present);
exit:
	mutex_unlock(&data->update_lock);
	return data;
}

static struct sfp_port_data *sfp_update_present(struct i2c_client *client)
{
	struct sfp_port_data *data = i2c_get_clientdata(client);
	int i = 0;
	int status = -1;
    u8 regs[] = {SFP_CPLD_REG_ADDR_PRESENT};

	if(data->port >= SFPPLUS_1_PORT_NUMBER){
        if(client->addr == SFP_EEPROM_A0_I2C_ADDR)
            return sfp_plus_update_present(client);
        else{
            data->present = 0;
            return data;
        }
	}

	DEBUG_PRINT("Starting sfp present status update");
	mutex_lock(&data->update_lock);

	/* Read present status of port 1~32 */
    data->present = 0;
	
    for (i = 0; i < ARRAY_SIZE(regs); i++) {
		status = i2c_smbus_read_byte_data(client, regs[i]);
        
        if (status < 0) {
            DEBUG_PRINT("cpld(%d) reg(0x%x) err %d", SFP_CPLD_I2C_ADDR, regs[i], status);
            goto exit;
        }
        DEBUG_PRINT("Present status = 0x%x", status);
        
		data->present |= (u64)status << (i*8);
        DEBUG_PRINT("Present status = 0x%llx", data->present);
    }

	DEBUG_PRINT("Present status = 0x%llx", data->present);
exit:
	mutex_unlock(&data->update_lock);
	return data;
}

static struct sfp_port_data *sfp_update_tx_rx_status(struct device *dev)
{
	return NULL;
}

static ssize_t sfp_set_tx_disable(struct device *dev, struct device_attribute *da, 
			const char *buf, size_t count)
{
	return 0;
}

static int sfp_is_port_present(struct i2c_client *client, int port)
{
    struct sfp_port_data *data = sfp_update_present(client);
        return (data->present & BIT_INDEX(port)) ? 1 : 0;
}

static ssize_t show_present(struct device *dev, struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	
	if ((attr->index >= PORT1_PRESENT_ALL) && (attr->index <= PORT8_PRESENT_ALL)) {
	}

	/* PRESENT */
	return sprintf(buf, "%d\n", sfp_is_port_present(client, attr->index));
}

static struct sfp_port_data *sfp_update_port_type(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct sfp_port_data *data = i2c_get_clientdata(client);
    u8 buf = 0;
    int status;

    mutex_lock(&data->update_lock);

    switch (data->driver_type) {
        case DRIVER_TYPE_SFP_MSA:
        {
            status = sfp_eeprom_read(data->client, SFF8024_PHYSICAL_DEVICE_ID_ADDR, &buf, sizeof(buf));	
            if (status < 0) {
                data->port_type = OOM_DRIVER_PORT_TYPE_INVALID;
                break;
            }

            if (buf != SFF8024_DEVICE_ID_SFP) {
                data->port_type = OOM_DRIVER_PORT_TYPE_INVALID;
                break;
            }

            status = sfp_eeprom_read(data->client, SFF8472_10G_ETH_COMPLIANCE_ADDR, &buf, sizeof(buf));	
            if (status < 0) {
                data->port_type = OOM_DRIVER_PORT_TYPE_INVALID;
                break;
            }

            DEBUG_PRINT("sfp port type (0x3) data = (0x%x)", buf);
            data->port_type = buf & SFF8472_10G_BASE_MASK ? OOM_DRIVER_PORT_TYPE_SFP_PLUS : OOM_DRIVER_PORT_TYPE_SFP;
            break;
        }
        case DRIVER_TYPE_QSFP:
        {
            status = sfp_eeprom_read(data->client, SFF8024_PHYSICAL_DEVICE_ID_ADDR, &buf, sizeof(buf));	
            if (status < 0) {
                data->port_type = OOM_DRIVER_PORT_TYPE_INVALID;
                break;
            }

            DEBUG_PRINT("qsfp port type (0x0) buf = (0x%x)", buf);
            switch (buf) {
            case SFF8024_DEVICE_ID_QSFP:
                data->port_type = OOM_DRIVER_PORT_TYPE_QSFP;
                break;
            case SFF8024_DEVICE_ID_QSFP_PLUS:
                data->port_type = OOM_DRIVER_PORT_TYPE_QSFP_PLUS;
                break;
            case SFF8024_DEVICE_ID_QSFP28:
                data->port_type = OOM_DRIVER_PORT_TYPE_QSFP28;
                break;				
            case SFF8024_DEVICE_ID_QSFPDD:
                data->port_type = OOM_DRIVER_PORT_TYPE_QSFPDD;
                break;				
            default:
                data->port_type = OOM_DRIVER_PORT_TYPE_INVALID;
                break;
            }

            break;
        }
        default:
            break;
    }

    mutex_unlock(&data->update_lock);
    return data;
}

static ssize_t show_port_type(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sfp_port_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

	if (!sfp_is_port_present(client, attr->index)) {
		return sprintf(buf, "%d\n", OOM_DRIVER_PORT_TYPE_NOT_PRESENT);
	}

	sfp_update_port_type(dev);
	return sprintf(buf, "%d\n", data->port_type);
}


static struct sfp_port_data *sfp_update_port_reset(struct i2c_client *client)
{
    struct sfp_port_data *data = i2c_get_clientdata(client);
    int i = 0;
    int status = -1;
    u8 regs[] = {SFP_CPLD_REG_ADDR_RESET};

    mutex_lock(&data->update_lock);

    /* Read reset status of port 1~32 */
    data->port_reset = 0;

    for (i = 0; i < ARRAY_SIZE(regs); i++) {
		status = i2c_smbus_read_byte_data(client, regs[i]);

        if (status < 0) {
            DEBUG_PRINT("cpld(0x%x) reg(0x%x) err %d", SFP_CPLD_REG_ADDR_RESET, regs[i], status);
            goto exit;
        }
        
        DEBUG_PRINT("reset status = 0x%x", status);
        data->port_reset |= (u64)status << (i*8);
    }

    DEBUG_PRINT("reset status = 0x%llx", data->port_reset);
exit:
    mutex_unlock(&data->update_lock);
    return data;
}

static ssize_t show_port_reset(struct device *dev, struct device_attribute *da,
                         char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct sfp_port_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    int is_reset = 0;

    if (!sfp_is_port_present(client, attr->index)) {
        return sprintf(buf, "%d\n", OOM_DRIVER_PORT_TYPE_NOT_PRESENT);
    }

    sfp_update_port_reset(client);
    is_reset = (data->port_reset & BIT_INDEX(attr->index));

    return sprintf(buf, "%d\n", is_reset);
}

static ssize_t sfp_set_port_reset(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct i2c_client *client = to_i2c_client(dev);
    struct sfp_port_data *data = i2c_get_clientdata(client);
    u8 cpld_reg = 0, cpld_val = 0; /*, cpld_bit = 0; //remove unused variable   */
    long is_reset;
    int error;

    error = kstrtol(buf, 10, &is_reset);
    if (error) {
        return error;
    }

    mutex_lock(&data->update_lock);

    cpld_reg = SFP_CPLD_REG_ADDR_RESET;

    cpld_val = i2c_smbus_read_byte_data(client, cpld_reg);

    DEBUG_PRINT("current cpld reg = 0x%x value = 0x%x", cpld_reg, cpld_val);

    /* Update reset status. CPLD defined 0 is reset state, 1 is normal state.
     * is_reset: 0 is not reset. 1 is reset.
     */
    if (is_reset == 1) {
        cpld_val |= BIT_INDEX(attr->index);
    }
    else {
        cpld_val &= ~BIT_INDEX(attr->index);
    }

    i2c_smbus_write_byte_data(client, cpld_reg, cpld_val);
    DEBUG_PRINT("write cpld reg = 0x%x value = 0x%x", cpld_reg, cpld_val);

    mutex_unlock(&data->update_lock);

    return count;
}

static struct sfp_port_data *qsfp_update_tx_rx_status(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct sfp_port_data *data = i2c_get_clientdata(client);	
	int i, status = -1;
	u8 buf = 0;
	u8 reg[] = {SFF8436_TX_FAULT_ADDR, SFF8436_TX_DISABLE_ADDR, SFF8436_RX_LOS_ADDR};

	if (time_before(jiffies, data->qsfp->last_updated + HZ + HZ / 2) && data->qsfp->valid) {
		return data;
	}

	dev_dbg(dev, "Starting sfp tx rx status update");
	mutex_lock(&data->update_lock);
	data->qsfp->valid = 0;
	memset(data->qsfp->status, 0, sizeof(data->qsfp->status));

	/* Notify device to update tx fault/ tx disable/ rx los status */
	for (i = 0; i < ARRAY_SIZE(reg); i++) {
		status = sfp_eeprom_read(data->client, reg[i], &buf, sizeof(buf)); 
		if (status < 0) {
			goto exit;
		}		
	}
	msleep(200);

	/* Read actual tx fault/ tx disable/ rx los status */
	for (i = 0; i < ARRAY_SIZE(reg); i++) {
		status = sfp_eeprom_read(data->client, reg[i], &buf, sizeof(buf)); 
		if (status < 0) {
			goto exit;
		}

		DEBUG_PRINT("qsfp reg(0x%x) status = (0x%x)", reg[i], data->qsfp->status[i]);
		data->qsfp->status[i] = (buf & 0xF);
	}

	data->qsfp->valid = 1;
	data->qsfp->last_updated = jiffies;
	mutex_unlock(&data->update_lock);
	return data;

exit:
	mutex_unlock(&data->update_lock);
	return NULL;	
}

static ssize_t qsfp_show_tx_rx_status(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	u8 val = 0;
	struct i2c_client *client = to_i2c_client(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct sfp_port_data *data = i2c_get_clientdata(client);	

	if (!sfp_is_port_present(client, attr->index)) {
		return -ENODEV;
	}

	data = qsfp_update_tx_rx_status(dev);
	if (!data) {
		return -EIO;
	}

	if ((attr->index >= PORT1_TX_FAULT) && (attr->index < PORT_TX_FAULT4_MAX)){
		if (attr->index < PORT_TX_FAULT_MAX)
		    val = ((data->qsfp->status[2] & 0xF) == 0xF) ? 1 : 0;
		else
		val = (data->qsfp->status[2] & BIT_INDEX(attr->index - PORT1_TX_FAULT1)) ? 1 : 0;
	}
	else if ((attr->index >= PORT1_TX_DISABLE) && (attr->index < PORT_TX_DISABLE4_MAX)){
		if (attr->index < PORT_TX_DISABLE_MAX)
            val = ((data->qsfp->status[1] & 0xF) == 0xF) ? 1 : 0;
		else
		val = (data->qsfp->status[1] & BIT_INDEX(attr->index - PORT1_TX_DISABLE1)) ? 1 : 0;
	}
	if ((attr->index >= PORT1_RX_LOS) && (attr->index < PORT_RX_LOS4_MAX)){
		if (attr->index < PORT_RX_LOS_MAX)
            val = ((data->qsfp->status[0] & 0xf) == 0xf) ? 1 : 0;
        else
		val = (data->qsfp->status[0] & BIT_INDEX(attr->index - PORT1_RX_LOS1)) ? 1 : 0;
	}
	
	return sprintf(buf, "%d\n", val);
}

static ssize_t qsfp_set_tx_disable(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
	long disable;
	int result;
	struct i2c_client *client = to_i2c_client(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct sfp_port_data *data;

	result = kstrtol(buf, 10, &disable);
	if (result) {
		return result;
	}

	data = qsfp_update_tx_rx_status(dev);
	if (!data) {
		return -EIO;
	}

	mutex_lock(&data->update_lock);

	if((attr->index >= PORT1_TX_DISABLE) && (attr->index < PORT_TX_DISABLE_MAX)){
		DEBUG_PRINT ("disable:%ld %d==TX_DISABLE %u\r\n", disable, attr->index, data->qsfp->status[1]);
        data->qsfp->status[1] =  disable? 0xF:0;
	}
	else{
    	if (disable) {
    		data->qsfp->status[1] |= (1 << (attr->index - PORT1_TX_DISABLE1));
    	}
    	else {
    		data->qsfp->status[1] &= ~(1 << (attr->index - PORT1_TX_DISABLE1));
    	}
	}
	
	DEBUG_PRINT("index = (%d), status = (0x%x)", attr->index, data->qsfp->status[1]);
	result = sfp_eeprom_write(client, SFF8436_TX_DISABLE_ADDR, &data->qsfp->status[1], sizeof(data->qsfp->status[1]));
	mutex_unlock(&data->update_lock);
	return count;
}

static struct sfp_port_data *qsfp_update_port_led(struct i2c_client *client)
{
    struct sfp_port_data *data = i2c_get_clientdata(client);
    int status = -1;
    u8 reg = SFP_CPLD_REG_ADDR_LED;

    mutex_lock(&data->update_lock);

    /* Read led status of port 1~32 */
    data->port_led = 0;

    status = i2c_smbus_read_byte_data(client, reg);

    if (status < 0) {
        DEBUG_PRINT("cpld reg(0x%x) err %d", reg, status);
        goto exit;
    }
        
    data->port_led = status;

    DEBUG_PRINT("led status = %ll", data->port_led);
exit:
    mutex_unlock(&data->update_lock);
    return data;
}

static ssize_t qsfp_show_port_led(struct device *dev, struct device_attribute *da,
                         char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct sfp_port_data *data = i2c_get_clientdata(client);

    data = qsfp_update_port_led(client);

    return sprintf(buf, "%ll\n", data->port_led);
}

static ssize_t qsfp_set_port_led(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct sfp_port_data *data = i2c_get_clientdata(client);
    u8 cpld_reg = 0, cpld_val = 0; /*, cpld_bit = 0; //remove unused variable   */
    long led_state;
    int error;
    int result;
	
    error = kstrtol(buf, 10, &led_state);
    if (error) {
        return error;
    }
	
    mutex_lock(&data->update_lock);

    cpld_reg = SFP_CPLD_REG_ADDR_LED;

    cpld_val = i2c_smbus_read_byte_data(client, cpld_reg);

    DEBUG_PRINT("current cpld reg = 0x%x value = 0x%x", cpld_reg, cpld_val);

    /* Update led status. CPLD defined 0 is LED enable, 1 is LED disable.
     * led_state: 0 is LED enable. 1 is LED disable.
     */
	if (cpld_val != led_state){
        data->port_led = led_state;

	    result = i2c_smbus_write_byte_data(client, cpld_reg, led_state);	
	    if (result < 0) {
		    dev_info(&client->dev, "%s, i2c_smbus_write_byte_data fail(%d)", __FUNCTION__, result);
	    }
        DEBUG_PRINT("write cpld reg = 0x%x value = 0x%x", cpld_reg, led_state);
    }
    
    mutex_unlock(&data->update_lock);

    return count;
}

static struct sfp_port_data *qsfp_update_port_lpmode(struct i2c_client *client)
{
    struct sfp_port_data *data = i2c_get_clientdata(client);
    int i = 0;
    int status = -1;
    u8 regs[] = {SFP_CPLD_REG_ADDR_LOWPOWERMODE};

    mutex_lock(&data->update_lock);

    /* Read lpmode status of port 1~32 */
    data->port_lpmode = 0;

    for (i = 0; i < ARRAY_SIZE(regs); i++) {
		status = i2c_smbus_read_byte_data(client, regs[i]);

        if (status < 0) {
            DEBUG_PRINT("cpld reg(0x%x) err %d", regs[i], status);
            goto exit;
        }
        
        DEBUG_PRINT("lpmode status = 0x%x", status);
        data->port_lpmode |= (u64)status << (i*8);
    }

    DEBUG_PRINT("lpmode status = 0x%llx", data->port_lpmode);
exit:
    mutex_unlock(&data->update_lock);
    return data;
}

static ssize_t qsfp_show_port_lpmode(struct device *dev, struct device_attribute *da,
                         char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct sfp_port_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    int is_lpmode = 0;

    qsfp_update_port_lpmode(client);
    is_lpmode = (data->port_lpmode & BIT_INDEX(attr->index))?1:0;

    return sprintf(buf, "%d\n", is_lpmode);
}

static ssize_t qsfp_set_port_lpmode(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct i2c_client *client = to_i2c_client(dev);
    struct sfp_port_data *data = i2c_get_clientdata(client);
    u8 cpld_reg = 0, cpld_val = 0;
    long is_lpmode;
    int error;

    error = kstrtol(buf, 10, &is_lpmode);
    if (error) {
        return error;
    }

    mutex_lock(&data->update_lock);

    cpld_reg = SFP_CPLD_REG_ADDR_LOWPOWERMODE;

    cpld_val = i2c_smbus_read_byte_data(client, cpld_reg);

    DEBUG_PRINT("current cpld reg = 0x%x value = 0x%x", cpld_reg, cpld_val);

    /* Update lpmode status. CPLD defined 0 is normal mode, 1 is Low Power mode.
     * is_lpmode: 0 is normal mode. 1 is Low Power mode.
     */
    if (is_lpmode == 1) {
        cpld_val |= BIT_INDEX(attr->index);
    }
    else {
        cpld_val &= ~BIT_INDEX(attr->index);
    }

    i2c_smbus_write_byte_data(client, cpld_reg, cpld_val);
    DEBUG_PRINT("write cpld reg = 0x%x value = 0x%x", cpld_reg, cpld_val);

    mutex_unlock(&data->update_lock);

    return count;
}

static ssize_t sfp_show_ddm_implemented(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	int status;
	char ddm;
	struct i2c_client *client = to_i2c_client(dev);
	struct sfp_port_data *data = i2c_get_clientdata(client);	
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

	if (!sfp_is_port_present(client, attr->index)) {
		return -ENODEV;
	}

	status = sfp_eeprom_read(data->client, SFF8472_DIAG_MON_TYPE_ADDR, &ddm, sizeof(ddm));	
	if (status < 0) {
		return -EIO;
	}

	return sprintf(buf, "%d\n", (ddm & SFF8472_DIAG_MON_TYPE_DDM_MASK) ? 1 : 0);
}

static ssize_t sfp_show_tx_rx_status(struct device *dev, struct device_attribute *da,
			 char *buf)
{
    u8 val = 0, index = 0;
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    struct sfp_port_data *data;

    data = sfp_update_tx_rx_status(dev);
    if (!data) {
        return -EIO;
    }
    if ((attr->index >= PORT1_TX_FAULT) && (attr->index < PORT_TX_FAULT_MAX)){
        index = 0;
    }
    else if ((attr->index >= PORT1_TX_DISABLE) && (attr->index < PORT_TX_DISABLE_MAX)){
        index = 1;
    }
    if ((attr->index >= PORT1_RX_LOS) && (attr->index < PORT_RX_LOS_MAX)){
        index = 2;
    }

    if(data->port == SFPPLUS_1_PORT_NUMBER)
        val = (data->ddm->status[index] & BIT_INDEX(0)) ? 1 : 0;
    else if(data->port == SFPPLUS_2_PORT_NUMBER)
        val = (data->ddm->status[index] & BIT_INDEX(1)) ? 1 : 0;
    else
        val = (data->ddm->status[index] & BIT_INDEX(attr->index)) ? 1 : 0;
    return sprintf(buf, "%d\n", val);
}

static ssize_t qsfp_show_eeprom(struct device *dev, struct device_attribute *da,
			 char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
    char devfile[96];
    struct file *sfd;
    int i2c_index = 0;
    int result;
    int offset[] = {SFP_CPLD_REG_ADDR_MODSELECT};
    int rdlen, rc;
    mm_segment_t old_fs;
	char buffer[256];

    if (!sfp_is_port_present(client, attr->index)) {
        return 0;
    }

    snprintf(devfile, sizeof(devfile), "/sys/bus/i2c/devices/0-0070/name");
		
	/* Read SFP EEPROM */
    sfd = filp_open(devfile, O_RDONLY, 0);
    if (IS_ERR(sfd)) {
        dev_info(&client->dev, "Failed to open file(%s)#%d", devfile, __LINE__);
        portCPLDID = portCPLDID_1;
		sfpPlusID = sfpPlusID_1;
    }
	else{
        portCPLDID = portCPLDID_0;
		sfpPlusID = sfpPlusID_0;
	}

    if(strcmp(client->name, "sfpcpld33") == 0){
        if(attr->index == 0)
            i2c_index = sfpPlusID[0];
        else if(attr->index == 1)
            i2c_index = sfpPlusID[1];
    }
    else{
        if(strcmp(client->name, "sfpcpld1") == 0)
            i2c_index = portCPLDID[0];
        else if(strcmp(client->name, "sfpcpld9") == 0)
            i2c_index = portCPLDID[1];
        else if(strcmp(client->name, "sfpcpld17") == 0)
            i2c_index = portCPLDID[2];
        else if(strcmp(client->name, "sfpcpld25") == 0)
            i2c_index = portCPLDID[3];

        /* Port number is 1-8 */
        result = i2c_smbus_write_byte_data(client, offset[0], BIT_INDEX(attr->index));
        if (result < 0) {
		    dev_info(&client->dev, "%s, i2c_smbus_write_byte_data fail(%d)", __FUNCTION__, result);
        }
    }
	
    snprintf(devfile, sizeof(devfile), "/sys/bus/i2c/devices/%d-0050/sfp_eeprom", i2c_index);
		
	/* Read SFP EEPROM */
    sfd = filp_open(devfile, O_RDONLY, 0);
    if (IS_ERR(sfd)) {
        dev_info(&client->dev, "Failed to open file(%s)#%d", devfile, __LINE__);
        return 0;
    }

    if(!(sfd->f_op) || !(sfd->f_op->read) ) {
        dev_info(&client->dev, "file %s cann't readable ?\n", devfile);
        return 0;
    }

    old_fs = get_fs();
    set_fs(KERNEL_DS);
    rdlen = sfd->f_op->read(sfd, buffer, sizeof(buffer), &sfd->f_pos);
    if (rdlen == 0) {
        dev_info(&client->dev, "File(%s) empty!\n", devfile);
        rc = 0;
        goto exit;
    }
	
    rc = sizeof(buffer);
    memcpy(buf, buffer, rc);
			
    /* Reset module select register */
    if(strcmp(client->name, "sfpcpld33") != 0){
        /* Port number is 1-8 */
        result = i2c_smbus_write_byte_data(client, offset[0], 0);
        if (result < 0) {
		    dev_info(&client->dev, "%s, i2c_smbus_write_byte_data fail(%d)", __FUNCTION__, result);
        }
    }

exit:
    set_fs(old_fs);
    filp_close(sfd, 0);

	return rc;
}

#if 0
static ssize_t qsfp_set_eeprom(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct sfp_port_data *data = i2c_get_clientdata(client);	
    struct sensor_device_attribute *attr = to_sensor_dev_attr(da);

    DEBUG_PRINT("data->port:%d attr index:%d", data->port, attr->index);
    return 1;
}
#endif

/* SFP/QSFP common attributes for sysfs */
#define DECLARE_PORT_NUMBER_SENSOR_DEVICE_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
	static SENSOR_DEVICE_ATTR(sfp##PORT1##_port_number, S_IRUGO, show_port_number, NULL, PORT##PORT1##_NUMBER); \
	static SENSOR_DEVICE_ATTR(sfp##PORT2##_port_number, S_IRUGO, show_port_number, NULL, PORT##PORT2##_NUMBER); \
	static SENSOR_DEVICE_ATTR(sfp##PORT3##_port_number, S_IRUGO, show_port_number, NULL, PORT##PORT3##_NUMBER); \
	static SENSOR_DEVICE_ATTR(sfp##PORT4##_port_number, S_IRUGO, show_port_number, NULL, PORT##PORT4##_NUMBER); \
	static SENSOR_DEVICE_ATTR(sfp##PORT5##_port_number, S_IRUGO, show_port_number, NULL, PORT##PORT5##_NUMBER); \
	static SENSOR_DEVICE_ATTR(sfp##PORT6##_port_number, S_IRUGO, show_port_number, NULL, PORT##PORT6##_NUMBER); \
	static SENSOR_DEVICE_ATTR(sfp##PORT7##_port_number, S_IRUGO, show_port_number, NULL, PORT##PORT7##_NUMBER); \
	static SENSOR_DEVICE_ATTR(sfp##PORT8##_port_number, S_IRUGO, show_port_number, NULL, PORT##PORT8##_NUMBER);
#define DECLARE_PORT_NUMBER_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
    &sensor_dev_attr_sfp##PORT1##_port_number.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT2##_port_number.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT3##_port_number.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT4##_port_number.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT5##_port_number.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT6##_port_number.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT7##_port_number.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT8##_port_number.dev_attr.attr,	
DECLARE_PORT_NUMBER_SENSOR_DEVICE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)

#define DECLARE_PORT_IS_PRESENT_SENSOR_DEVICE_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
	static SENSOR_DEVICE_ATTR(sfp##PORT1##_is_present, S_IRUGO, show_present, NULL, PORT##PORT1##_PRESENT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT2##_is_present, S_IRUGO, show_present, NULL, PORT##PORT2##_PRESENT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT3##_is_present, S_IRUGO, show_present, NULL, PORT##PORT3##_PRESENT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT4##_is_present, S_IRUGO, show_present, NULL, PORT##PORT4##_PRESENT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT5##_is_present, S_IRUGO, show_present, NULL, PORT##PORT5##_PRESENT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT6##_is_present, S_IRUGO, show_present, NULL, PORT##PORT6##_PRESENT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT7##_is_present, S_IRUGO, show_present, NULL, PORT##PORT7##_PRESENT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT8##_is_present, S_IRUGO, show_present, NULL, PORT##PORT8##_PRESENT);
#define DECLARE_PORT_IS_PRESENT_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
    &sensor_dev_attr_sfp##PORT1##_is_present.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT2##_is_present.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT3##_is_present.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT4##_is_present.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT5##_is_present.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT6##_is_present.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT7##_is_present.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT8##_is_present.dev_attr.attr,
DECLARE_PORT_IS_PRESENT_SENSOR_DEVICE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)

#define DECLARE_PORT_TYPE_SENSOR_DEVICE_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
	static SENSOR_DEVICE_ATTR(sfp##PORT1##_port_type, S_IRUGO, show_port_type, NULL, PORT##PORT1##_TYPE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT2##_port_type, S_IRUGO, show_port_type, NULL, PORT##PORT2##_TYPE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT3##_port_type, S_IRUGO, show_port_type, NULL, PORT##PORT3##_TYPE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT4##_port_type, S_IRUGO, show_port_type, NULL, PORT##PORT4##_TYPE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT5##_port_type, S_IRUGO, show_port_type, NULL, PORT##PORT5##_TYPE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT6##_port_type, S_IRUGO, show_port_type, NULL, PORT##PORT6##_TYPE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT7##_port_type, S_IRUGO, show_port_type, NULL, PORT##PORT7##_TYPE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT8##_port_type, S_IRUGO, show_port_type, NULL, PORT##PORT8##_TYPE);
#define DECLARE_PORT_TYPE_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) &sensor_dev_attr_sfp##PORT1##_port_type.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT2##_port_type.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT3##_port_type.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT4##_port_type.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT5##_port_type.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT6##_port_type.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT7##_port_type.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT8##_port_type.dev_attr.attr,	
DECLARE_PORT_TYPE_SENSOR_DEVICE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)

#define DECLARE_PORT_RESET_SENSOR_DEVICE_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
	static SENSOR_DEVICE_ATTR(sfp##PORT1##_port_reset, S_IWUSR | S_IRUGO, show_port_reset, sfp_set_port_reset, PORT##PORT1##_RESET); \
	static SENSOR_DEVICE_ATTR(sfp##PORT2##_port_reset, S_IWUSR | S_IRUGO, show_port_reset, sfp_set_port_reset, PORT##PORT2##_RESET); \
	static SENSOR_DEVICE_ATTR(sfp##PORT3##_port_reset, S_IWUSR | S_IRUGO, show_port_reset, sfp_set_port_reset, PORT##PORT3##_RESET); \
	static SENSOR_DEVICE_ATTR(sfp##PORT4##_port_reset, S_IWUSR | S_IRUGO, show_port_reset, sfp_set_port_reset, PORT##PORT4##_RESET); \
	static SENSOR_DEVICE_ATTR(sfp##PORT5##_port_reset, S_IWUSR | S_IRUGO, show_port_reset, sfp_set_port_reset, PORT##PORT5##_RESET); \
	static SENSOR_DEVICE_ATTR(sfp##PORT6##_port_reset, S_IWUSR | S_IRUGO, show_port_reset, sfp_set_port_reset, PORT##PORT6##_RESET); \
	static SENSOR_DEVICE_ATTR(sfp##PORT7##_port_reset, S_IWUSR | S_IRUGO, show_port_reset, sfp_set_port_reset, PORT##PORT7##_RESET); \
	static SENSOR_DEVICE_ATTR(sfp##PORT8##_port_reset, S_IWUSR | S_IRUGO, show_port_reset, sfp_set_port_reset, PORT##PORT8##_RESET);
#define DECLARE_PORT_RESET_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) &sensor_dev_attr_sfp##PORT1##_port_reset.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT2##_port_reset.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT3##_port_reset.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT4##_port_reset.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT5##_port_reset.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT6##_port_reset.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT7##_port_reset.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT8##_port_reset.dev_attr.attr,	
DECLARE_PORT_RESET_SENSOR_DEVICE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)

/* QSFP attributes for sysfs */
#define DECLARE_PORT_RX_LOSn_SENSOR_DEVICE_ATTR(INDEX, PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
	static SENSOR_DEVICE_ATTR(sfp##PORT1##_rx_los##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT1##_RX_LOS##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT2##_rx_los##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT2##_RX_LOS##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT3##_rx_los##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT3##_RX_LOS##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT4##_rx_los##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT4##_RX_LOS##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT5##_rx_los##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT5##_RX_LOS##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT6##_rx_los##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT6##_RX_LOS##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT7##_rx_los##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT7##_RX_LOS##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT8##_rx_los##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT8##_RX_LOS##INDEX);
#define DECLARE_PORT_RX_LOSn_ATTR(INDEX, PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
    &sensor_dev_attr_sfp##PORT1##_rx_los##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT2##_rx_los##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT3##_rx_los##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT4##_rx_los##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT5##_rx_los##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT6##_rx_los##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT7##_rx_los##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT8##_rx_los##INDEX.dev_attr.attr,	
DECLARE_PORT_RX_LOSn_SENSOR_DEVICE_ATTR(1, 1, 2, 3, 4, 5, 6, 7, 8)
DECLARE_PORT_RX_LOSn_SENSOR_DEVICE_ATTR(2, 1, 2, 3, 4, 5, 6, 7, 8)
DECLARE_PORT_RX_LOSn_SENSOR_DEVICE_ATTR(3, 1, 2, 3, 4, 5, 6, 7, 8)
DECLARE_PORT_RX_LOSn_SENSOR_DEVICE_ATTR(4, 1, 2, 3, 4, 5, 6, 7, 8)

#define DECLARE_PORT_TX_DISABLEn_SENSOR_DEVICE_ATTR(INDEX, PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
	static SENSOR_DEVICE_ATTR(sfp##PORT1##_tx_disable##INDEX, S_IWUSR | S_IRUGO, qsfp_show_tx_rx_status, qsfp_set_tx_disable, PORT##PORT1##_TX_DISABLE##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT2##_tx_disable##INDEX, S_IWUSR | S_IRUGO, qsfp_show_tx_rx_status, qsfp_set_tx_disable, PORT##PORT2##_TX_DISABLE##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT3##_tx_disable##INDEX, S_IWUSR | S_IRUGO, qsfp_show_tx_rx_status, qsfp_set_tx_disable, PORT##PORT3##_TX_DISABLE##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT4##_tx_disable##INDEX, S_IWUSR | S_IRUGO, qsfp_show_tx_rx_status, qsfp_set_tx_disable, PORT##PORT4##_TX_DISABLE##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT5##_tx_disable##INDEX, S_IWUSR | S_IRUGO, qsfp_show_tx_rx_status, qsfp_set_tx_disable, PORT##PORT5##_TX_DISABLE##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT6##_tx_disable##INDEX, S_IWUSR | S_IRUGO, qsfp_show_tx_rx_status, qsfp_set_tx_disable, PORT##PORT6##_TX_DISABLE##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT7##_tx_disable##INDEX, S_IWUSR | S_IRUGO, qsfp_show_tx_rx_status, qsfp_set_tx_disable, PORT##PORT7##_TX_DISABLE##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT8##_tx_disable##INDEX, S_IWUSR | S_IRUGO, qsfp_show_tx_rx_status, qsfp_set_tx_disable, PORT##PORT8##_TX_DISABLE##INDEX);

#define DECLARE_PORT_TX_DISABLEn_ATTR(INDEX, PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
    &sensor_dev_attr_sfp##PORT1##_tx_disable##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT2##_tx_disable##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT3##_tx_disable##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT4##_tx_disable##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT5##_tx_disable##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT6##_tx_disable##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT7##_tx_disable##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT8##_tx_disable##INDEX.dev_attr.attr,	
DECLARE_PORT_TX_DISABLEn_SENSOR_DEVICE_ATTR(1, 1, 2, 3, 4, 5, 6, 7, 8)
DECLARE_PORT_TX_DISABLEn_SENSOR_DEVICE_ATTR(2, 1, 2, 3, 4, 5, 6, 7, 8)
DECLARE_PORT_TX_DISABLEn_SENSOR_DEVICE_ATTR(3, 1, 2, 3, 4, 5, 6, 7, 8)
DECLARE_PORT_TX_DISABLEn_SENSOR_DEVICE_ATTR(4, 1, 2, 3, 4, 5, 6, 7, 8)

#define DECLARE_PORT_TX_FAULTn_SENSOR_DEVICE_ATTR(INDEX, PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
	static SENSOR_DEVICE_ATTR(sfp##PORT1##_tx_fault##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT1##_TX_FAULT##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT2##_tx_fault##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT2##_TX_FAULT##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT3##_tx_fault##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT3##_TX_FAULT##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT4##_tx_fault##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT4##_TX_FAULT##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT5##_tx_fault##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT5##_TX_FAULT##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT6##_tx_fault##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT6##_TX_FAULT##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT7##_tx_fault##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT7##_TX_FAULT##INDEX); \
	static SENSOR_DEVICE_ATTR(sfp##PORT8##_tx_fault##INDEX, S_IRUGO, qsfp_show_tx_rx_status, NULL, PORT##PORT8##_TX_FAULT##INDEX);
#define DECLARE_PORT_TX_FAULTn_ATTR(INDEX, PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
    &sensor_dev_attr_sfp##PORT1##_tx_fault##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT2##_tx_fault##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT3##_tx_fault##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT4##_tx_fault##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT5##_tx_fault##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT6##_tx_fault##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT7##_tx_fault##INDEX.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT8##_tx_fault##INDEX.dev_attr.attr,	
DECLARE_PORT_TX_FAULTn_SENSOR_DEVICE_ATTR(1, 1, 2, 3, 4, 5, 6, 7, 8)
DECLARE_PORT_TX_FAULTn_SENSOR_DEVICE_ATTR(2, 1, 2, 3, 4, 5, 6, 7, 8)
DECLARE_PORT_TX_FAULTn_SENSOR_DEVICE_ATTR(3, 1, 2, 3, 4, 5, 6, 7, 8)
DECLARE_PORT_TX_FAULTn_SENSOR_DEVICE_ATTR(4, 1, 2, 3, 4, 5, 6, 7, 8)

#define DECLARE_PORT_LED_DEVICE_ATTR() \
	static SENSOR_DEVICE_ATTR(sfp_led_disable, S_IWUSR | S_IRUGO, qsfp_show_port_led, qsfp_set_port_led, PORT_LED);
#define DECLARE_PORT_LED_ATTR() \
	&sensor_dev_attr_sfp_led_disable.dev_attr.attr,	
DECLARE_PORT_LED_DEVICE_ATTR()

#define DECLARE_PORT_EEPROMn_SENSOR_DEVICE_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
	static SENSOR_DEVICE_ATTR(sfp##PORT1##_eeprom, S_IRUGO, qsfp_show_eeprom, NULL, PORT##PORT1##_EEPROM); \
	static SENSOR_DEVICE_ATTR(sfp##PORT2##_eeprom, S_IRUGO, qsfp_show_eeprom, NULL, PORT##PORT2##_EEPROM); \
	static SENSOR_DEVICE_ATTR(sfp##PORT3##_eeprom, S_IRUGO, qsfp_show_eeprom, NULL, PORT##PORT3##_EEPROM); \
	static SENSOR_DEVICE_ATTR(sfp##PORT4##_eeprom, S_IRUGO, qsfp_show_eeprom, NULL, PORT##PORT4##_EEPROM); \
	static SENSOR_DEVICE_ATTR(sfp##PORT5##_eeprom, S_IRUGO, qsfp_show_eeprom, NULL, PORT##PORT5##_EEPROM); \
	static SENSOR_DEVICE_ATTR(sfp##PORT6##_eeprom, S_IRUGO, qsfp_show_eeprom, NULL, PORT##PORT6##_EEPROM); \
	static SENSOR_DEVICE_ATTR(sfp##PORT7##_eeprom, S_IRUGO, qsfp_show_eeprom, NULL, PORT##PORT7##_EEPROM); \
	static SENSOR_DEVICE_ATTR(sfp##PORT8##_eeprom, S_IRUGO, qsfp_show_eeprom, NULL, PORT##PORT8##_EEPROM);
#define DECLARE_PORT_EEPROMTn_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
    &sensor_dev_attr_sfp##PORT1##_eeprom.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT2##_eeprom.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT3##_eeprom.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT4##_eeprom.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT5##_eeprom.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT6##_eeprom.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT7##_eeprom.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT8##_eeprom.dev_attr.attr,	
DECLARE_PORT_EEPROMn_SENSOR_DEVICE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)

#define DECLARE_PORT_LPMODE_SENSOR_DEVICE_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
	static SENSOR_DEVICE_ATTR(sfp##PORT1##_lpmode, S_IWUSR | S_IRUGO, qsfp_show_port_lpmode, qsfp_set_port_lpmode, PORT##PORT1##_LPMODE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT2##_lpmode, S_IWUSR | S_IRUGO, qsfp_show_port_lpmode, qsfp_set_port_lpmode, PORT##PORT2##_LPMODE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT3##_lpmode, S_IWUSR | S_IRUGO, qsfp_show_port_lpmode, qsfp_set_port_lpmode, PORT##PORT3##_LPMODE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT4##_lpmode, S_IWUSR | S_IRUGO, qsfp_show_port_lpmode, qsfp_set_port_lpmode, PORT##PORT4##_LPMODE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT5##_lpmode, S_IWUSR | S_IRUGO, qsfp_show_port_lpmode, qsfp_set_port_lpmode, PORT##PORT5##_LPMODE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT6##_lpmode, S_IWUSR | S_IRUGO, qsfp_show_port_lpmode, qsfp_set_port_lpmode, PORT##PORT6##_LPMODE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT7##_lpmode, S_IWUSR | S_IRUGO, qsfp_show_port_lpmode, qsfp_set_port_lpmode, PORT##PORT7##_LPMODE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT8##_lpmode, S_IWUSR | S_IRUGO, qsfp_show_port_lpmode, qsfp_set_port_lpmode, PORT##PORT8##_LPMODE);
#define DECLARE_PORT_LPMODE_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
    &sensor_dev_attr_sfp##PORT1##_lpmode.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT2##_lpmode.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT3##_lpmode.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT4##_lpmode.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT5##_lpmode.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT6##_lpmode.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT7##_lpmode.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT8##_lpmode.dev_attr.attr,	
DECLARE_PORT_LPMODE_SENSOR_DEVICE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)

#define DECLARE_PORT_CPLD_REVISION() \
	static SENSOR_DEVICE_ATTR(cpld_revision, (0660), show_cpld_version, NULL, CPLD_REVISION);
#define DECLARE_PORT_CPLD_REVISION_ATTR() \
	&sensor_dev_attr_cpld_revision.dev_attr.attr,	
DECLARE_PORT_CPLD_REVISION()

static struct attribute *qsfp_attributes[] = {
	DECLARE_PORT_NUMBER_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_TYPE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_IS_PRESENT_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_RESET_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_LPMODE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_LED_ATTR()
	DECLARE_PORT_RX_LOSn_ATTR(1, 1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_RX_LOSn_ATTR(2, 1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_RX_LOSn_ATTR(3, 1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_RX_LOSn_ATTR(4, 1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_TX_DISABLEn_ATTR(1, 1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_TX_DISABLEn_ATTR(2, 1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_TX_DISABLEn_ATTR(3, 1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_TX_DISABLEn_ATTR(4, 1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_TX_FAULTn_ATTR(1, 1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_TX_FAULTn_ATTR(2, 1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_TX_FAULTn_ATTR(3, 1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_TX_FAULTn_ATTR(4, 1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_EEPROMTn_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_CPLD_REVISION_ATTR()
	NULL
};


/* SFP msa attributes for sysfs */
static struct attribute *sfp_msa_attributes[] = {
	DECLARE_PORT_NUMBER_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_TYPE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_IS_PRESENT_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_PORT_RESET_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	NULL
};

/* SFP ddm attributes for sysfs */
#define DECLARE_RX_LOS_SENSOR_DEVICE_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
	static SENSOR_DEVICE_ATTR(sfp##PORT1##_rx_los, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT1##_RX_LOS); \
	static SENSOR_DEVICE_ATTR(sfp##PORT2##_rx_los, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT2##_RX_LOS); \
	static SENSOR_DEVICE_ATTR(sfp##PORT3##_rx_los, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT3##_RX_LOS); \
	static SENSOR_DEVICE_ATTR(sfp##PORT4##_rx_los, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT4##_RX_LOS); \
	static SENSOR_DEVICE_ATTR(sfp##PORT5##_rx_los, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT5##_RX_LOS); \
	static SENSOR_DEVICE_ATTR(sfp##PORT6##_rx_los, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT6##_RX_LOS); \
	static SENSOR_DEVICE_ATTR(sfp##PORT7##_rx_los, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT7##_RX_LOS); \
	static SENSOR_DEVICE_ATTR(sfp##PORT8##_rx_los, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT8##_RX_LOS);
#define DECLARE_RX_LOS_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) &sensor_dev_attr_sfp##PORT1##_rx_los.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT2##_rx_los.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT3##_rx_los.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT4##_rx_los.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT5##_rx_los.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT6##_rx_los.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT7##_rx_los.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT8##_rx_los.dev_attr.attr,	
DECLARE_RX_LOS_SENSOR_DEVICE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)

#define DECLARE_TX_DISABLE_SENSOR_DEVICE_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
	static SENSOR_DEVICE_ATTR(sfp##PORT1##_tx_disable, S_IWUSR | S_IRUGO, sfp_show_tx_rx_status, sfp_set_tx_disable, PORT##PORT1##_TX_DISABLE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT2##_tx_disable, S_IWUSR | S_IRUGO, sfp_show_tx_rx_status, sfp_set_tx_disable, PORT##PORT2##_TX_DISABLE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT3##_tx_disable, S_IWUSR | S_IRUGO, sfp_show_tx_rx_status, sfp_set_tx_disable, PORT##PORT3##_TX_DISABLE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT4##_tx_disable, S_IWUSR | S_IRUGO, sfp_show_tx_rx_status, sfp_set_tx_disable, PORT##PORT4##_TX_DISABLE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT5##_tx_disable, S_IWUSR | S_IRUGO, sfp_show_tx_rx_status, sfp_set_tx_disable, PORT##PORT5##_TX_DISABLE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT6##_tx_disable, S_IWUSR | S_IRUGO, sfp_show_tx_rx_status, sfp_set_tx_disable, PORT##PORT6##_TX_DISABLE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT7##_tx_disable, S_IWUSR | S_IRUGO, sfp_show_tx_rx_status, sfp_set_tx_disable, PORT##PORT7##_TX_DISABLE); \
	static SENSOR_DEVICE_ATTR(sfp##PORT8##_tx_disable, S_IWUSR | S_IRUGO, sfp_show_tx_rx_status, sfp_set_tx_disable, PORT##PORT8##_TX_DISABLE);
#define DECLARE_TX_DISABLE_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
        &sensor_dev_attr_sfp##PORT1##_tx_disable.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT2##_tx_disable.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT3##_tx_disable.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT4##_tx_disable.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT5##_tx_disable.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT6##_tx_disable.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT7##_tx_disable.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT8##_tx_disable.dev_attr.attr,	
DECLARE_TX_DISABLE_SENSOR_DEVICE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)

#define DECLARE_TX_FAULT_SENSOR_DEVICE_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
	static SENSOR_DEVICE_ATTR(sfp##PORT1##_tx_fault, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT1##_TX_FAULT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT2##_tx_fault, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT2##_TX_FAULT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT3##_tx_fault, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT3##_TX_FAULT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT4##_tx_fault, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT4##_TX_FAULT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT5##_tx_fault, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT5##_TX_FAULT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT6##_tx_fault, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT6##_TX_FAULT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT7##_tx_fault, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT7##_TX_FAULT); \
	static SENSOR_DEVICE_ATTR(sfp##PORT8##_tx_fault, S_IRUGO, sfp_show_tx_rx_status, NULL, PORT##PORT8##_TX_FAULT);
#define DECLARE_TX_FAULT_ATTR(PORT1, PORT2, PORT3, PORT4, PORT5, PORT6, PORT7, PORT8) \
        &sensor_dev_attr_sfp##PORT1##_tx_fault.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT2##_tx_fault.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT3##_tx_fault.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT4##_tx_fault.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT5##_tx_fault.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT6##_tx_fault.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT7##_tx_fault.dev_attr.attr, \
	&sensor_dev_attr_sfp##PORT8##_tx_fault.dev_attr.attr,	
DECLARE_TX_FAULT_SENSOR_DEVICE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
static struct attribute *sfp_ddm_attributes[] = {
	DECLARE_RX_LOS_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_TX_DISABLE_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	DECLARE_TX_FAULT_ATTR(1, 2, 3, 4, 5, 6, 7, 8)
	NULL
};

static ssize_t sfp_eeprom_write(struct i2c_client *client, u8 command, const char *data,
			  int data_len)
{
#if USE_I2C_BLOCK_READ
	int result, retry = I2C_RW_RETRY_COUNT;

	if (data_len > I2C_SMBUS_BLOCK_MAX) {
		data_len = I2C_SMBUS_BLOCK_MAX;
	} 

	while (retry) {
		result = i2c_smbus_write_i2c_block_data(client, command, data_len, data);
		if (result < 0) {
			msleep(I2C_RW_RETRY_INTERVAL);
			retry--;
			continue;
		}
 
		break;
	}

	if (unlikely(result < 0)) {
		return result;
	}		

	return data_len;
#else
	int result, retry = I2C_RW_RETRY_COUNT;

	while (retry) {
		result = i2c_smbus_write_byte_data(client, command, *data);
		if (result < 0) {
			msleep(I2C_RW_RETRY_INTERVAL);
			retry--;
			continue;
		}
 
		break;
	}
	
	if (unlikely(result < 0)) {
		return result;
	}

	return 1;
#endif
	
}


static ssize_t sfp_port_write(struct sfp_port_data *data, 
						  const char *buf, loff_t off, size_t count)
{
	ssize_t retval = 0;
	
	if (unlikely(!count)) {
		return count;
	}
	
	/*
	 * Write data to chip, protecting against concurrent updates
	 * from this host, but not from other I2C masters.
	 */
	mutex_lock(&data->update_lock);
	
	while (count) {
		ssize_t status;

		status = sfp_eeprom_write(data->client, off, buf, count);
		if (status <= 0) {
			if (retval == 0) {
				retval = status;
			}
			break;
		}
		buf += status;
		off += status;
		count -= status;
		retval += status;
	}
	
	mutex_unlock(&data->update_lock);
	return retval;
}


static ssize_t sfp_bin_write(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				char *buf, loff_t off, size_t count)
{
	struct sfp_port_data *data;
	DEBUG_PRINT("offset = (%d), count = (%d)", (int)off, (int)count);
	data = dev_get_drvdata(container_of(kobj, struct device, kobj));
	return sfp_port_write(data, buf, off, count);
}

static ssize_t sfp_eeprom_read(struct i2c_client *client, u8 command, u8 *data,
			  int data_len)
{
#if USE_I2C_BLOCK_READ
	int result, retry = I2C_RW_RETRY_COUNT;

	if (data_len > I2C_SMBUS_BLOCK_MAX) {
		data_len = I2C_SMBUS_BLOCK_MAX;
	}

	while (retry) {
		result = i2c_smbus_read_i2c_block_data(client, command, data_len, data);
		if (result < 0) {
			msleep(I2C_RW_RETRY_INTERVAL);
			retry--;
			continue;
		}

		break;
	}
	
	if (unlikely(result < 0))
		goto abort;
	if (unlikely(result != data_len)) {
		result = -EIO;
		goto abort;
	}
	
	/* result = data_len; */
	
abort:
	return result;
#else
	int result, retry = I2C_RW_RETRY_COUNT;

	while (retry) {
		result = i2c_smbus_read_byte_data(client, command);
		if (result < 0) {
			msleep(I2C_RW_RETRY_INTERVAL);
			retry--;
			continue;
		}
 
		break;
	}

	if (unlikely(result < 0)) {
		dev_dbg(&client->dev, "sfp read byte data failed, command(0x%2x), data(0x%2x)\r\n", command, result);
		goto abort;
	}

	*data  = (u8)result;
	result = 1;

abort:
	return result;	
#endif
}

static ssize_t sfp_port_read(struct sfp_port_data *data,
				char *buf, loff_t off, size_t count)
{
	ssize_t retval = 0;
	
	if (unlikely(!count)) {
		DEBUG_PRINT("Count = 0, return");
		return count;
	}
	
	/*
	 * Read data from chip, protecting against concurrent updates
	 * from this host, but not from other I2C masters.
	 */
	mutex_lock(&data->update_lock);

	while (count) {
		ssize_t status;
	
		status = sfp_eeprom_read(data->client, off, buf, count);
		if (status <= 0) {
			if (retval == 0) {
				retval = status;
			}
			break;
		}
		
		buf += status;
		off += status;
		count -= status;
		retval += status;
	}
	
	mutex_unlock(&data->update_lock);
	return retval;

}

static ssize_t sfp_bin_read(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct sfp_port_data *data;
	DEBUG_PRINT("offset = (%d), count = (%d)", (int)off, (int)count);
	data = dev_get_drvdata(container_of(kobj, struct device, kobj));
	return sfp_port_read(data, buf, off, count);
}

static int sfp_sysfs_eeprom_init(struct kobject *kobj, struct bin_attribute *eeprom)
{
	int err;

	sysfs_bin_attr_init(eeprom);
	eeprom->attr.name = EEPROM_NAME;
	eeprom->attr.mode = S_IWUSR | S_IRUGO;
	eeprom->read  	  = sfp_bin_read;
	eeprom->write 	  = sfp_bin_write;
	eeprom->size  	  = EEPROM_SIZE;	

	/* Create eeprom file */
	err = sysfs_create_bin_file(kobj, eeprom);
	if (err) {
		return err;
	}
	
	return 0;
}

static int sfp_sysfs_eeprom_cleanup(struct kobject *kobj, struct bin_attribute *eeprom)
{
	sysfs_remove_bin_file(kobj, eeprom);
	return 0;
}

static const struct attribute_group sfp_msa_group = {
	.attrs = sfp_msa_attributes,
};

static int sfp_i2c_check_functionality(struct i2c_client *client)
{
#if USE_I2C_BLOCK_READ
    return i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_I2C_BLOCK);
#else
    return i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA);
#endif
}

static int sfp_msa_probe(struct i2c_client *client, const struct i2c_device_id *dev_id,
							   struct sfp_msa_data **data)
{
	int status;
	struct sfp_msa_data *msa;
	
	if (!sfp_i2c_check_functionality(client)) {
        status = -EIO;
        goto exit;		
	}
	
	msa = kzalloc(sizeof(struct sfp_msa_data), GFP_KERNEL);
	if (!msa) {
		status = -ENOMEM;
		goto exit;
	}	
	
	/* Register sysfs hooks */
	status = sysfs_create_group(&client->dev.kobj, &sfp_msa_group);
	if (status) {
		goto exit_free;
	}

	/* init eeprom */
	status = sfp_sysfs_eeprom_init(&client->dev.kobj, &msa->eeprom.bin);
	if (status) {
		dev_info(&client->dev, "sfp msa '%s' (sfp_sysfs_eeprom_init fail...)\n", client->name);
		/* goto exit_remove; */
	}

	*data = msa;
	dev_info(&client->dev, "sfp msa '%s'\n", client->name);

	return 0;

/* exit_remove: */
	sysfs_remove_group(&client->dev.kobj, &sfp_msa_group);
exit_free:
	kfree(msa);
exit:

	return status;		
}

static const struct attribute_group sfp_ddm_group = {
	.attrs = sfp_ddm_attributes,
};

static int sfp_ddm_probe(struct i2c_client *client, const struct i2c_device_id *dev_id,
							   struct sfp_ddm_data **data)
{
	int status;
	struct sfp_ddm_data *ddm;
	
	if (!sfp_i2c_check_functionality(client)) {
        status = -EIO;
        goto exit;		
	}
	
	ddm = kzalloc(sizeof(struct sfp_ddm_data), GFP_KERNEL);
	if (!ddm) {
		status = -ENOMEM;
		goto exit;
	}	

	/* Register sysfs hooks */
	status = sysfs_create_group(&client->dev.kobj, &sfp_ddm_group);
	if (status) {
		goto exit_free;
	}

	/* init eeprom */
	status = sfp_sysfs_eeprom_init(&client->dev.kobj, &ddm->eeprom.bin);
	if (status) {
		goto exit_remove;
	}

	*data = ddm;
	dev_info(&client->dev, "sfp ddm '%s'\n", client->name);

	return 0;

exit_remove:
	sysfs_remove_group(&client->dev.kobj, &sfp_ddm_group);
exit_free:
	kfree(ddm);
exit:

	return status;	
}

static const struct attribute_group qsfp_group = {
	.attrs = qsfp_attributes,
};

static int qsfp_probe(struct i2c_client *client, const struct i2c_device_id *dev_id,
						  struct qsfp_data **data)
{
    int status;
    struct qsfp_data *qsfp;
    struct sfp_port_data *port_data = i2c_get_clientdata(client);


    if (!sfp_i2c_check_functionality(client)) {
        status = -EIO;
        goto exit;		
    }

    qsfp = kzalloc(sizeof(struct qsfp_data), GFP_KERNEL);
    if (!qsfp) {
        status = -ENOMEM;
        goto exit;
    }

    /* Register sysfs hooks */
    if ( ((port_data->port < SFPPLUS_1_PORT_NUMBER) && (client->addr == SFP_CPLD_I2C_ADDR)) ||
         ((port_data->port >= SFPPLUS_1_PORT_NUMBER) && (client->addr == SFP_EEPROM_A0_I2C_ADDR)) )
    {
        status = sysfs_create_group(&client->dev.kobj, &qsfp_group);
        if (status) {
            goto exit_free;
        }
    }

    if (client->addr == SFP_EEPROM_A0_I2C_ADDR){
        /* init eeprom */
        status = sfp_sysfs_eeprom_init(&client->dev.kobj, &qsfp->eeprom.bin);
        if (status) {
            goto exit_remove;
        }
    }

    /* Bring QSFPs out of reset */

    *data = qsfp;
    dev_info(&client->dev, "qsfp '%s'\n", client->name);

    return 0;

exit_remove:
    sysfs_remove_group(&client->dev.kobj, &qsfp_group);
exit_free:
    kfree(qsfp);
exit:

    return status;	
}

static int sfp_device_probe(struct i2c_client *client,
			const struct i2c_device_id *dev_id)
{
    struct sfp_port_data *data = NULL;

    data = kzalloc(sizeof(struct sfp_port_data), GFP_KERNEL);
    if (!data) {
        return -ENOMEM;
    }

    i2c_set_clientdata(client, data);
    mutex_init(&data->update_lock);
    data->port 	 = dev_id->driver_data;
    data->client = client;

    DEBUG_PRINT("data->port:%d client->addr:0x%0x\n", data->port, client->addr);
    if (client->addr == SFP_CPLD_I2C_ADDR){
        if(data->port >= SFPPLUS_1_PORT_NUMBER){
            DEBUG_PRINT("client->addr:0x%0x\n", client->addr);
            return -ENODEV;
        }
    }

    data->driver_type = DRIVER_TYPE_QSFP;
    return qsfp_probe(client, dev_id, &data->qsfp);
}

static int sfp_msa_remove(struct i2c_client *client, struct sfp_msa_data *data)
{
    if (client->addr == SFP_EEPROM_A0_I2C_ADDR)
        sfp_sysfs_eeprom_cleanup(&client->dev.kobj, &data->eeprom.bin);
    if ((client->addr == SFP_CPLD_I2C_ADDR) || (client->addr == SFP_EEPROM_A0_I2C_ADDR))
        sysfs_remove_group(&client->dev.kobj, &sfp_msa_group);	
    kfree(data);
    return 0;
}

static int sfp_ddm_remove(struct i2c_client *client, struct sfp_ddm_data *data)
{
    if (client->addr == SFP_EEPROM_A0_I2C_ADDR)
        sfp_sysfs_eeprom_cleanup(&client->dev.kobj, &data->eeprom.bin);
    if ((client->addr == SFP_CPLD_I2C_ADDR) || (client->addr == SFP_EEPROM_A0_I2C_ADDR))
        sysfs_remove_group(&client->dev.kobj, &sfp_ddm_group);
    kfree(data);
    return 0;
}

static int qfp_remove(struct i2c_client *client, struct qsfp_data *data)
{
    if (client->addr == SFP_EEPROM_A0_I2C_ADDR)
        sfp_sysfs_eeprom_cleanup(&client->dev.kobj, &data->eeprom.bin);
    if ((client->addr == SFP_CPLD_I2C_ADDR) || (client->addr == SFP_EEPROM_A0_I2C_ADDR))
        sysfs_remove_group(&client->dev.kobj, &qsfp_group);
    kfree(data);
    return 0;
}

static int sfp_device_remove(struct i2c_client *client)
{
    struct sfp_port_data *data = i2c_get_clientdata(client);

    switch (data->driver_type) {
        case DRIVER_TYPE_SFP_MSA:
            return sfp_msa_remove(client, data->msa);
        case DRIVER_TYPE_SFP_DDM:
            return sfp_ddm_remove(client, data->ddm);
        case DRIVER_TYPE_QSFP:
            return qfp_remove(client, data->qsfp);
    }

    return 0;
}

static struct i2c_driver sfp_driver = {
    .driver = {
        .name     = DRIVER_NAME,
    },
    .probe        = sfp_device_probe,
    .remove       = sfp_device_remove,
    .id_table     = sfp_device_id,
    .address_list = normal_i2c,
};

static int __init sfp_init(void)
{
	return i2c_add_driver(&sfp_driver);
}

static void __exit sfp_exit(void)
{
	i2c_del_driver(&sfp_driver);
}

MODULE_AUTHOR("Philip Wang <philip_wang@alphanetworks.com>");
MODULE_DESCRIPTION("alphanetworks snj61d0-320f driver");
MODULE_LICENSE("GPL");

module_init(sfp_init);
module_exit(sfp_exit);

