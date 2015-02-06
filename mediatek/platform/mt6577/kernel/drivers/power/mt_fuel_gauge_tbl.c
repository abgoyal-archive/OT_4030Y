#include <linux/init.h>        /* For init/exit macros */
#include <linux/module.h>      /* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>

#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/rtc.h>
#include <linux/xlog.h>

#include <asm/uaccess.h>
#include <mach/mt_typedefs.h>
#include <mach/hardware.h>

#include <cust_fuel_gauge.h>

#include <mach/pmic_mt6329_hw_bank1.h>
#include <mach/pmic_mt6329_sw_bank1.h>
#include <mach/pmic_mt6329_hw.h>
#include <mach/pmic_mt6329_sw.h>
#include <mach/upmu_common_sw.h>
#include <mach/upmu_hw.h>

#ifdef MTK_NCP1851_SUPPORT
#include "ncp1851.h"
#endif
//int Enable_FGADC_LOG = 0;
int Enable_FGADC_LOG = 1;

///////////////////////////////////////////////////////////////////////////////////////////
//// Extern Functions
///////////////////////////////////////////////////////////////////////////////////////////
#define AUXADC_BATTERY_VOLTAGE_CHANNEL  0
#define AUXADC_REF_CURRENT_CHANNEL     	1
#define AUXADC_CHARGER_VOLTAGE_CHANNEL  2
#define AUXADC_TEMPERATURE_CHANNEL     	3
#define AUXADC_HW_OCV_CHANNEL			4

extern int PMIC_IMM_GetOneChannelValue(int dwChannel, int deCount);
#ifdef MTK_NCP1851_SUPPORT
extern int PMIC_IMM_GetBatChannelValue(int deCount, int polling);
extern int g_ocv_lookup_done;
extern int g_boot_charging;
#endif
extern INT16 BattVoltToTemp(UINT32 dwVolt);
extern kal_bool upmu_is_chr_det(void);

extern int g_charger_in_flag;
extern int g_SW_CHR_OUT_EN;
extern int g_HW_Charging_Done;
extern int g_HW_stop_charging;
extern int bat_volt_check_point;
extern int gForceADCsolution;
extern kal_bool batteryBufferFirst;

///////////////////////////////////////////////////////////////////////////////////////////
//// Define
///////////////////////////////////////////////////////////////////////////////////////////
#define UNIT_FGCURRENT 	(158122) 	// 158.122 uA
#define UNIT_FGTIME 	(16) 		// 0.16s
#define UNIT_FGCHARGE 	(21961412) 	// 0.021961412 uAh //6329

#define MAX_V_CHARGER 4000
#define CHR_OUT_CURRENT	100

static DEFINE_MUTEX(FGADC_mutex);

///////////////////////////////////////////////////////////////////////////////////////////
// Common API
///////////////////////////////////////////////////////////////////////////////////////////
kal_int32 fgauge_read_r_bat_by_v(kal_int32 voltage);
kal_int32 fgauge_get_Q_max(kal_int16 temperature);
kal_int32 fgauge_get_Q_max_high_current(kal_int16 temperature);

void MTKFG_PLL_Control(kal_bool en)	   //True means turn on, False means turn off
{
	//kal_uint16 Temp_Reg=0;

    if(en == KAL_TRUE)
    {

	    xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[MTKFG_PLL_Control] MTKFG_PLL_Control ---ON \r\n");
    }
    else
    {
	    xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[MTKFG_PLL_Control] MTKFG_PLL_Control ---OFF \r\n");
    }

}

int fgauge_get_saddles(void)
{
    return sizeof(battery_profile_t2) / sizeof(BATTERY_PROFILE_STRUC);
}

int fgauge_get_saddles_r_table(void)
{
    return sizeof(r_profile_t2) / sizeof(R_PROFILE_STRUC);
}

BATTERY_PROFILE_STRUC_P fgauge_get_profile(kal_uint32 temperature)
{
    switch (temperature)
    {
        case TEMPERATURE_T0:
            return &battery_profile_t0[0];
            break;
        case TEMPERATURE_T1:
            return &battery_profile_t1[0];
            break;
        case TEMPERATURE_T2:
            return &battery_profile_t2[0];
            break;
        case TEMPERATURE_T3:
            return &battery_profile_t3[0];
            break;
        case TEMPERATURE_T:
            return &battery_profile_temperature[0];
            break;
        default:
            return NULL;
            break;
    }
}

R_PROFILE_STRUC_P fgauge_get_profile_r_table(kal_uint32 temperature)
{
    switch (temperature)
    {
    	case TEMPERATURE_T0:
            return &r_profile_t0[0];
            break;
        case TEMPERATURE_T1:
            return &r_profile_t1[0];
            break;
        case TEMPERATURE_T2:
            return &r_profile_t2[0];
            break;
        case TEMPERATURE_T3:
            return &r_profile_t3[0];
            break;
        case TEMPERATURE_T:
            return &r_profile_temperature[0];
            break;
        default:
            return NULL;
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////
// HW Test Mode
///////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////
// Global Variable
///////////////////////////////////////////////////////////////////////////////////////////
kal_int8 gFG_DOD0_update = 0;
kal_int32 gFG_DOD0 = 0;
kal_int32 gFG_DOD1 = 0;
kal_int32 gFG_DOD1_return = 0;
kal_int32 gFG_columb = 0;
kal_int32 gFG_columb_HW_reg = 0;
kal_int32 gFG_voltage = 0;
kal_int32 gFG_voltage_pre = -500;
kal_int32 gFG_current = 0;
kal_int32 gFG_capacity = 0;
kal_int32 gFG_capacity_by_c = 0;
kal_int32 gFG_capacity_by_c_init = 0;
kal_int32 gFG_capacity_by_v = 0;
kal_int32 gFG_columb_init = 0;
kal_int32 gFG_inner_R = 0;
kal_int16 gFG_temp= 100;
kal_int16 gFG_pre_temp=100;
kal_int16 gFG_T_changed=5;
kal_int32 gEstBatCapacity = 0;
kal_int32 gFG_SW_CoulombCounter = 0;
kal_bool gFG_Is_Charging = KAL_FALSE;
kal_int32 gFG_bat_temperature = 0;
kal_int32 gFG_resistance_bat = 0;
kal_int32 gFG_compensate_value = 0;
kal_int32 gFG_ori_voltage = 0;
kal_int32 gFG_booting_counter_I = 0;
kal_int32 gFG_booting_counter_I_FLAG = 0;
kal_int32 gFG_BATT_CAPACITY = 0;
int vchr_kthread_index=0;
kal_int32 gFG_voltage_init=0;
kal_int32 gFG_current_auto_detect_R_fg_total=0;
kal_int32 gFG_current_auto_detect_R_fg_count=0;
kal_int32 gFG_current_auto_detect_R_fg_result=0;
kal_int32 current_get_ori=0;
int gFG_15_vlot=3700;
kal_int32 gfg_percent_check_point=50;
#ifdef MTK_NCP1851_SUPPORT
kal_int32 gFG_BATT_CAPACITY_init_high_current = 3600;
kal_int32 gFG_BATT_CAPACITY_aging = 3600;
#else
kal_int32 gFG_BATT_CAPACITY_init_high_current = 1200;
kal_int32 gFG_BATT_CAPACITY_aging = 1200;
#endif
int volt_mode_update_timer=0;
int volt_mode_update_time_out=6; //1mins

#ifdef MTK_NCP1851_SUPPORT
#define AGING_TUNING_VALUE 102
#else
#define AGING_TUNING_VALUE 103
#endif

void FGADC_dump_parameter(void)
{
	kal_uint32 reg_val = 0;
	kal_uint32 reg_num = 0x69;
	int i=0;

	for(i=reg_num ; i<=30 ; i++)
	{
		reg_val = upmu_get_reg_value_bank1(i);
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "Reg[0x%x]=0x%x \r\n", i, reg_val);
	}
}

void FGADC_dump_register(void)
{
	kal_uint32 reg_val = 0;
	kal_uint32 reg_num = 0x69;
	int i=0;

	for(i=reg_num ; i<=30 ; i++)
	{
		reg_val = upmu_get_reg_value_bank1(i);
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "Reg[0x%x]=0x%x \r\n", i, reg_val);
	}
}

void FGADC_dump_register_csv(void)
{

}

kal_uint32 fg_get_data_ready_status(void)
{
	kal_uint32 ret=0;
	kal_uint8 temp_val=0;

	ret=pmic_bank1_read_interface(0x6A, &temp_val, 0xFF, 0x0);
#ifndef MTK_NCP1851_SUPPORT
	if (Enable_FGADC_LOG == 1) {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fg_get_data_ready_status] Reg[0x%x]=0x%x\r\n", 0x6A, temp_val);
	}
#endif
	temp_val = (temp_val & 0x04) >> 2;

	return temp_val;
}

kal_uint32 fg_get_sw_clear_status(void)
{
	kal_uint32 ret=0;
	kal_uint8 temp_val=0;

	ret=pmic_bank1_read_interface(0x6A, &temp_val, 0xFF, 0x0);
#ifndef MTK_NCP1851_SUPPORT
	if (Enable_FGADC_LOG == 1) {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fg_get_sw_clear_status] Reg[0x%x]=0x%x\r\n", 0x6A, temp_val);
	}
#endif
	temp_val = (temp_val & 0x08) >> 3;

	return temp_val;
}

///////////////////////////////////////////////////////////////////////////////////////////
//// Variables for debug UI tool
///////////////////////////////////////////////////////////////////////////////////////////
extern int bat_volt_check_point;
int g_fg_dbg_bat_volt=0;
int g_fg_dbg_bat_current=0;
int g_fg_dbg_bat_zcv=0;
int g_fg_dbg_bat_temp=0;
int g_fg_dbg_bat_r=0;
int g_fg_dbg_bat_car=0;
int g_fg_dbg_bat_qmax=0;
int g_fg_dbg_d0=0;
int g_fg_dbg_d1=0;
int g_fg_dbg_percentage=0;
int g_fg_dbg_percentage_fg=0;
int g_fg_dbg_percentage_voltmode=0;

void update_fg_dbg_tool_value(void)
{
	g_fg_dbg_bat_volt = gFG_voltage_init;

	if(gFG_Is_Charging)
		g_fg_dbg_bat_current = 1 - gFG_current - 1;
	else
		g_fg_dbg_bat_current = gFG_current;

	g_fg_dbg_bat_zcv = gFG_voltage;

	g_fg_dbg_bat_temp = gFG_temp;

	g_fg_dbg_bat_r = gFG_resistance_bat;

	g_fg_dbg_bat_car = gFG_columb;

	g_fg_dbg_bat_qmax = gFG_BATT_CAPACITY_aging;

	g_fg_dbg_d0 = gFG_DOD0;

	g_fg_dbg_d1 = gFG_DOD1;

	g_fg_dbg_percentage = bat_volt_check_point;

	g_fg_dbg_percentage_fg = gFG_capacity_by_c;

	g_fg_dbg_percentage_voltmode = gfg_percent_check_point;
}

///////////////////////////////////////////////////////////////////////////////////////////
// SW algorithm
///////////////////////////////////////////////////////////////////////////////////////////

kal_int32 fgauge_read_temperature(void)
{
	int bat_temperature_volt=0;
	int bat_temperature=0;

	bat_temperature_volt = PMIC_IMM_GetOneChannelValue(AUXADC_TEMPERATURE_CHANNEL,5);
	bat_temperature = BattVoltToTemp(bat_temperature_volt);
	gFG_bat_temperature = bat_temperature;

    return bat_temperature;
}

void dump_nter(void)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[dump_nter] nter_29_24 = 0x%x\r\n", upmu_fgadc_nter_29_24());
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[dump_nter] nter_23_16 = 0x%x\r\n", upmu_fgadc_nter_23_16());
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[dump_nter] nter_15_08 = 0x%x\r\n", upmu_fgadc_nter_15_08());
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[dump_nter] nter_07_00 = 0x%x\r\n", upmu_fgadc_nter_07_00());
}

void dump_car(void)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[dump_car] upmu_fgadc_car_35_32 = 0x%x\r\n", upmu_fgadc_car_35_32());
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[dump_car] upmu_fgadc_car_31_24 = 0x%x\r\n", upmu_fgadc_car_31_24());
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[dump_car] upmu_fgadc_car_23_16 = 0x%x\r\n", upmu_fgadc_car_23_16());
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[dump_car] upmu_fgadc_car_15_08 = 0x%x\r\n", upmu_fgadc_car_15_08());
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[dump_car] upmu_fgadc_car_07_00 = 0x%x\r\n", upmu_fgadc_car_07_00());
}

kal_int32 fgauge_read_columb(void)
{
	kal_uint32 uvalue32_CAR = 0;
	kal_uint32 uvalue32_CAR_MSB = 0;
    kal_int32 dvalue_CAR = 0;
	int m = 0;
	//kal_uint32 Temp_Reg = 0;
	int Temp_Value = 0;
	kal_uint32 ret = 0;

// HW Init
	//(1)	i2c_write (0x60, 0xC8, 0x01); // Enable VA2
	ret=pmic_config_interface(0xC8, 0x1, 0xFF, 0x0);
	//(2)	i2c_write (0x61, 0x15, 0x00); // Enable FGADC clock for digital
	//ret=pmic_bank1_config_interface(0x15, 0x0, 0xFF, 0x0);
	//(3)	i2c_write (0x61, 0x69, 0x28); // Set current mode, auto-calibration mode and 32KHz clock source
	//ret=pmic_bank1_config_interface(0x69, 0x28, 0xFF, 0x0);
	//(4)	i2c_write (0x61, 0x69, 0x29); // Enable FGADC
	//ret=pmic_bank1_config_interface(0x69, 0x29, 0xFF, 0x0);

//Read HW Raw Data
	//(1)	i2c_write (0x61, 0x6A, 0x02); // Set READ command
	ret=pmic_bank1_config_interface(0x6A, 0x02, 0xFF, 0x0);
	//(2)	i2c_read (0x61, 0x6A) // Keep i2c read when status = 1 (0x06)
	m=0;
	while ( fg_get_data_ready_status() == 0 )
    {
		m++;
		if(m>1000)
		{
			if (Enable_FGADC_LOG == 1){
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_columb] fg_get_data_ready_status timeout 1 !\r\n");
			}
			break;
		}
    }
	//(3)	Read FG_CURRENT_OUT[28:14]
	//(4)	Read FG_CURRENT_OUT[35]
	uvalue32_CAR = (upmu_fgadc_car_15_08())>>6;
	uvalue32_CAR |= (upmu_fgadc_car_23_16())<<2;
	uvalue32_CAR |= (upmu_fgadc_car_31_24())<<10;
	uvalue32_CAR = uvalue32_CAR & 0xFFFF;
	gFG_columb_HW_reg = uvalue32_CAR;
	uvalue32_CAR_MSB = (upmu_fgadc_car_35_32() & 0x0F)>>3;
#ifndef MTK_NCP1851_SUPPORT
	if (Enable_FGADC_LOG == 1) {
    	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_columb : FG_CAR = 0x%x\r\n", uvalue32_CAR);
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_columb : uvalue32_CAR_MSB = 0x%x\r\n", uvalue32_CAR_MSB);
	}
#endif
	//(5)	(Read other data)
	//(6)	i2c_write (0x61, 0x6A, 0x08); // Clear status to 0
	ret=pmic_bank1_config_interface(0x6A, 0x08, 0xFF, 0x0);
	//(7)	i2c_read (0x61, 0x6A) // Keep i2c read when status = 0 (0x08)
	//while ( fg_get_sw_clear_status() != 0 )
	m=0;
	while ( fg_get_data_ready_status() != 0 )
	{
		m++;
		if(m>1000)
		{
			if (Enable_FGADC_LOG == 1){
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_columb] fg_get_data_ready_status timeout 2 !\r\n");
			}
			break;
		}
	}
	//(8)	i2c_write (0x61, 0x6A, 0x00); // Recover original settings
	ret=pmic_bank1_config_interface(0x6A, 0x00, 0xFF, 0x0);

//calculate the real world data
    dvalue_CAR = (kal_int32) uvalue32_CAR;

	if(uvalue32_CAR == 0)
	{
		Temp_Value = 0;
	}
	else if(uvalue32_CAR == 65535) // 0xffff
	{
		Temp_Value = 0;
	}
	else if(uvalue32_CAR_MSB == 0x1)
	{
		//dis-charging
		Temp_Value = dvalue_CAR - 65535; // keep negative value
	}
	else
	{
		//charging
		Temp_Value = (int) dvalue_CAR;
	}
	Temp_Value = ( ((Temp_Value*35986)/10) + (5) )/10; //[28:14]'s LSB=359.86 uAh
	dvalue_CAR = Temp_Value / 1000; //mAh
#ifndef MTK_NCP1851_SUPPORT
	if (Enable_FGADC_LOG == 1) {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_columb : dvalue_CAR = %d\r\n", dvalue_CAR);
	}
#endif
	#if (OSR_SELECT_7 == 1)
		dvalue_CAR = dvalue_CAR * 8;
		if (Enable_FGADC_LOG == 1) {
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_columb : dvalue_CAR update to %d\r\n", dvalue_CAR);
		}
	#endif

//Auto adjust value
	if(R_FG_VALUE != 20)
	{
		if (Enable_FGADC_LOG == 1) {
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] Auto adjust value deu to the Rfg is %d\n Ori CAR=%d, ", R_FG_VALUE, dvalue_CAR);
		}
		dvalue_CAR = (dvalue_CAR*20)/R_FG_VALUE;
		if (Enable_FGADC_LOG == 1) {
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "new CAR=%d\n", dvalue_CAR);
		}
	}

	dvalue_CAR = ((dvalue_CAR*CAR_TUNE_VALUE)/100);

	if (Enable_FGADC_LOG == 1) {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_columb : final dvalue_CAR = %d\r\n", dvalue_CAR);
	}
#ifndef MTK_NCP1851_SUPPORT
	if (Enable_FGADC_LOG == 1){
		dump_nter();
		dump_car();
	}
#endif
    return dvalue_CAR;
}

kal_int32 fgauge_read_columb_reset(void)
{
	kal_uint32 uvalue32_CAR = 0;
	kal_uint32 uvalue32_CAR_MSB = 0;
    kal_int32 dvalue_CAR = 0;
	int m = 0;
	//kal_uint32 Temp_Reg = 0;
	int Temp_Value = 0;
	kal_uint32 ret = 0;

// HW Init
	//(1)	i2c_write (0x60, 0xC8, 0x01); // Enable VA2
	ret=pmic_config_interface(0xC8, 0x1, 0xFF, 0x0);
	//(2)	i2c_write (0x61, 0x15, 0x00); // Enable FGADC clock for digital
	//ret=pmic_bank1_config_interface(0x15, 0x0, 0xFF, 0x0);
	//(3)	i2c_write (0x61, 0x69, 0x28); // Set current mode, auto-calibration mode and 32KHz clock source
	//ret=pmic_bank1_config_interface(0x69, 0x28, 0xFF, 0x0);
	//(4)	i2c_write (0x61, 0x69, 0x29); // Enable FGADC
	//ret=pmic_bank1_config_interface(0x69, 0x29, 0xFF, 0x0);

//Read HW Raw Data
	//(1)	i2c_write (0x61, 0x6A, 0x02); // Set READ command
	ret=pmic_bank1_config_interface(0x6A, 0x73, 0xFF, 0x0);
	//(2)	i2c_read (0x61, 0x6A) // Keep i2c read when status = 1 (0x06)
	m=0;
	while ( fg_get_data_ready_status() == 0 )
    {
		m++;
		if(m>1000)
		{
			if (Enable_FGADC_LOG == 1){
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_columb] fg_get_data_ready_status timeout 1 !\r\n");
			}
			break;
		}
    }
	//(3)	Read FG_CURRENT_OUT[28:14]
	//(4)	Read FG_CURRENT_OUT[35]
	uvalue32_CAR = (upmu_fgadc_car_15_08())>>6;
	uvalue32_CAR |= (upmu_fgadc_car_23_16())<<2;
	uvalue32_CAR |= (upmu_fgadc_car_31_24())<<10;
	uvalue32_CAR = uvalue32_CAR & 0xFFFF;
	gFG_columb_HW_reg = uvalue32_CAR;
	uvalue32_CAR_MSB = (upmu_fgadc_car_35_32() & 0x0F)>>3;
#ifndef MTK_NCP1851_SUPPORT
	if (Enable_FGADC_LOG == 1) {
    	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_columb : FG_CAR = 0x%x\r\n", uvalue32_CAR);
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_columb : uvalue32_CAR_MSB = 0x%x\r\n", uvalue32_CAR_MSB);
	}
#endif
	//(5)	(Read other data)
	//(6)	i2c_write (0x61, 0x6A, 0x08); // Clear status to 0
	ret=pmic_bank1_config_interface(0x6A, 0x08, 0xFF, 0x0);
	//(7)	i2c_read (0x61, 0x6A) // Keep i2c read when status = 0 (0x08)
	//while ( fg_get_sw_clear_status() != 0 )
	m=0;
	while ( fg_get_data_ready_status() != 0 )
	{
		m++;
		if(m>1000)
		{
			if (Enable_FGADC_LOG == 1){
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_columb] fg_get_data_ready_status timeout 2 !\r\n");
			}
			break;
		}
	}
	//(8)	i2c_write (0x61, 0x6A, 0x00); // Recover original settings
	ret=pmic_bank1_config_interface(0x6A, 0x00, 0xFF, 0x0);

//calculate the real world data
    dvalue_CAR = (kal_int32) uvalue32_CAR;

	if(uvalue32_CAR == 0)
	{
		Temp_Value = 0;
	}
	else if(uvalue32_CAR == 65535) // 0xffff
	{
		Temp_Value = 0;
	}
	else if(uvalue32_CAR_MSB == 0x1)
	{
		//dis-charging
		Temp_Value = dvalue_CAR - 65535; // keep negative value
	}
	else
	{
		//charging
		Temp_Value = (int) dvalue_CAR;
	}
	Temp_Value = ( ((Temp_Value*35986)/10) + (5) )/10; //[28:14]'s LSB=359.86 uAh
	dvalue_CAR = Temp_Value / 1000; //mAh
#ifndef MTK_NCP1851_SUPPORT
	if (Enable_FGADC_LOG == 1) {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_columb : dvalue_CAR = %d\r\n", dvalue_CAR);
	}
#endif
	#if (OSR_SELECT_7 == 1)
		dvalue_CAR = dvalue_CAR * 8;
		if (Enable_FGADC_LOG == 1) {
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_columb : dvalue_CAR update to %d\r\n", dvalue_CAR);
		}
	#endif

//Auto adjust value
	if(R_FG_VALUE != 20)
	{
		if (Enable_FGADC_LOG == 1) {
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] Auto adjust value deu to the Rfg is %d\n Ori CAR=%d, ", R_FG_VALUE, dvalue_CAR);
		}
		dvalue_CAR = (dvalue_CAR*20)/R_FG_VALUE;
		if (Enable_FGADC_LOG == 1) {
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "new CAR=%d\n", dvalue_CAR);
		}
	}

	dvalue_CAR = ((dvalue_CAR*CAR_TUNE_VALUE)/100);

	if (Enable_FGADC_LOG == 1) {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_columb : final dvalue_CAR = %d\r\n", dvalue_CAR);
	}
#ifndef MTK_NCP1851_SUPPORT
	if (Enable_FGADC_LOG == 1){
		dump_nter();
		dump_car();
	}
#endif
    return dvalue_CAR;
}

kal_int32 fgauge_read_current(void)
{
    kal_uint16 uvalue16 = 0;
    kal_int32 dvalue = 0;
	int m = 0;
	//kal_uint16 Temp_Reg = 0;
	int Temp_Value = 0;
	kal_int32 Current_Compensate_Value=0;
	kal_uint32 ret = 0;

// HW Init
	//(1)	i2c_write (0x60, 0xC8, 0x01); // Enable VA2
	ret=pmic_config_interface(0xC8, 0x1, 0xFF, 0x0);
	//(2)	i2c_write (0x61, 0x15, 0x00); // Enable FGADC clock for digital
	//ret=pmic_bank1_config_interface(0x15, 0x0, 0xFF, 0x0);
	//(3)	i2c_write (0x61, 0x69, 0x28); // Set current mode, auto-calibration mode and 32KHz clock source
	//ret=pmic_bank1_config_interface(0x69, 0x28, 0xFF, 0x0);
	//(4)	i2c_write (0x61, 0x69, 0x29); // Enable FGADC
	//ret=pmic_bank1_config_interface(0x69, 0x29, 0xFF, 0x0);

//Read HW Raw Data
	//(1)	i2c_write (0x61, 0x6A, 0x02); // Set READ command
	ret=pmic_bank1_config_interface(0x6A, 0x02, 0xFF, 0x0);
	//(2)	i2c_read (0x61, 0x6A) // Keep i2c read when status = 1 (0x06)
	m=0;
	while ( fg_get_data_ready_status() == 0 )
    {
		m++;
		if(m>1000)
		{
			if (Enable_FGADC_LOG == 1){
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_current] fg_get_data_ready_status timeout 1 !\r\n");
			}
			break;
		}
    }
	//(3)	i2c_read (0x61, 0x78); // Read FG_CURRENT_OUT[15:08]
	//(4)	i2c_read (0x61, 0x79); // Read FG_CURRENT_OUT[07:00]
	uvalue16 = upmu_fgadc_current_out_07_00();
	uvalue16 |= (upmu_fgadc_current_out_15_08())<<8;
#ifndef MTK_NCP1851_SUPPORT
	if (Enable_FGADC_LOG == 1) {
    	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_current : FG_CURRENT = %x\r\n", uvalue16);
	}
#endif
	//(5)	(Read other data)
	//(6)	i2c_write (0x61, 0x6A, 0x08); // Clear status to 0
	ret=pmic_bank1_config_interface(0x6A, 0x08, 0xFF, 0x0);
	//(7)	i2c_read (0x61, 0x6A) // Keep i2c read when status = 0 (0x08)
	//while ( fg_get_sw_clear_status() != 0 )
	m=0;
	while ( fg_get_data_ready_status() != 0 )
	{
		m++;
		if(m>1000)
		{
			if (Enable_FGADC_LOG == 1){
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_current] fg_get_data_ready_status timeout 2 !\r\n");
			}
			break;
		}
	}
	//(8)	i2c_write (0x61, 0x6A, 0x00); // Recover original settings
	ret=pmic_bank1_config_interface(0x6A, 0x00, 0xFF, 0x0);

//calculate the real world data
    dvalue = (kal_uint32) uvalue16;
	if( dvalue == 0 )
	{
		Temp_Value = (int) dvalue;
		gFG_Is_Charging = KAL_FALSE;
	}
	else if( dvalue > 32767 ) // > 0x8000
	{
		Temp_Value = dvalue - 65535;
		Temp_Value = Temp_Value - (Temp_Value*2);
		gFG_Is_Charging = KAL_FALSE;
	}
	else
	{
		Temp_Value = (int) dvalue;
		gFG_Is_Charging = KAL_TRUE;
	}
	dvalue = (kal_uint32) ((Temp_Value * UNIT_FGCURRENT) / 100000);

	current_get_ori = dvalue;
#ifndef MTK_NCP1851_SUPPORT
	if (Enable_FGADC_LOG == 1)
	{
		if( gFG_Is_Charging == KAL_TRUE )
		{
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_current : current(charging) = %d mA\r\n", dvalue);
		}
		else
		{
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] fgauge_read_current : current(discharging) = %d mA\r\n", dvalue);
		}
	}
#endif
// Auto adjust value
	if(R_FG_VALUE != 20)
	{
		if (Enable_FGADC_LOG == 1) {
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] Auto adjust value deu to the Rfg is %d\n Ori current=%d, ", R_FG_VALUE, dvalue);
		}
		dvalue = (dvalue*20)/R_FG_VALUE;
		if (Enable_FGADC_LOG == 1) {
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "new current=%d\n", dvalue);
		}
	}

// K current
	if(R_FG_BOARD_SLOPE != R_FG_BOARD_BASE)
	{
		dvalue = ( (dvalue*R_FG_BOARD_BASE) + (R_FG_BOARD_SLOPE/2) ) / R_FG_BOARD_SLOPE;
	}

// current compensate
	if(gFG_Is_Charging==KAL_TRUE)
	{
		dvalue = dvalue + Current_Compensate_Value;
	}
	else
	{
		dvalue = dvalue - Current_Compensate_Value;
	}
#ifndef MTK_NCP1851_SUPPORT
	if (Enable_FGADC_LOG == 1) {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "ori current=%d\n", dvalue);
	}
#endif
	//dvalue = ((dvalue*94)/100);
	dvalue = ((dvalue*CAR_TUNE_VALUE)/100);
	if (Enable_FGADC_LOG == 1) {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "final current=%d (ratio=%d)\n", dvalue, CAR_TUNE_VALUE);
	}

    return dvalue;
}

kal_int32 fgauge_read_voltage(void)
{
    int vol_battery;

	vol_battery = PMIC_IMM_GetOneChannelValue(AUXADC_BATTERY_VOLTAGE_CHANNEL,15);

	if(gFG_voltage_pre == -500)
	{
		gFG_voltage_pre = vol_battery; // for init

		return vol_battery;
	}

    return vol_battery;
}

kal_int32 fgauge_compensate_battery_voltage(kal_int32 ori_voltage)
{
	kal_int32 ret_compensate_value = 0;

	gFG_ori_voltage = ori_voltage;

	gFG_resistance_bat = fgauge_read_r_bat_by_v(ori_voltage); // Ohm

	ret_compensate_value = (gFG_current * (gFG_resistance_bat + R_FG_VALUE)) / 1000;
	//ret_compensate_value = (gFG_current * (gFG_resistance_bat - R_FG_VALUE)) / 1000;
	ret_compensate_value = (ret_compensate_value+(10/2)) / 10; // 20101103

    if (gFG_Is_Charging == KAL_TRUE)
    {
    	/* charging, COMPASATE_OCV is negitive */
        //return 0;

		ret_compensate_value = ret_compensate_value - (ret_compensate_value*2);
    }
    else
    {
        /* discharging, COMPASATE_OCV is positive */
        //return COMPASATE_OCV;
    }

	gFG_compensate_value = ret_compensate_value;

	//xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[CompensateVoltage] Ori_voltage:%d, compensate_value:%d, gFG_resistance_bat:%d, gFG_current:%d\r\n",
	//	ori_voltage, ret_compensate_value, gFG_resistance_bat, gFG_current);

    return ret_compensate_value;
}

kal_int32 fgauge_compensate_battery_voltage_recursion(kal_int32 ori_voltage, kal_int32 recursion_time)
{
	kal_int32 ret_compensate_value = 0;
	kal_int32 temp_voltage_1 = ori_voltage;
	kal_int32 temp_voltage_2 = temp_voltage_1;
	int i = 0;
#ifdef OCV_USE_BATSNS
       kal_int32 vfet_status;
       vfet_status = ncp1851_get_vfet_ok();
#endif

	for(i=0 ; i < recursion_time ; i++)
	{
		gFG_resistance_bat = fgauge_read_r_bat_by_v(temp_voltage_2); // Ohm
#ifdef OCV_USE_BATSNS
              if(vfet_status == 1)
                  ret_compensate_value = (gFG_current * (gFG_resistance_bat + R_FG_VALUE + R_QFET_VALUE)) / 1000;
              else
                  ret_compensate_value = (gFG_current * (gFG_resistance_bat + R_FG_VALUE)) / 1000;
#else
		ret_compensate_value = (gFG_current * (gFG_resistance_bat + R_FG_VALUE)) / 1000;
#endif
		//ret_compensate_value = (gFG_current * (gFG_resistance_bat - R_FG_VALUE)) / 1000;
		ret_compensate_value = (ret_compensate_value+(10/2)) / 10; // 20101103

	    if (gFG_Is_Charging == KAL_TRUE)
	    {
			ret_compensate_value = ret_compensate_value - (ret_compensate_value*2);
	    }
		temp_voltage_2 = temp_voltage_1 + ret_compensate_value;

		//if(gFG_booting_counter_I_FLAG != 2)
		if (Enable_FGADC_LOG == 1)
		{
		xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[fgauge_compensate_battery_voltage_recursion] %d,%d,%d,%d\r\n",
			temp_voltage_1, temp_voltage_2, gFG_resistance_bat, ret_compensate_value);
		}

		//temp_voltage_1 = temp_voltage_2;
	}

	gFG_resistance_bat = fgauge_read_r_bat_by_v(temp_voltage_2); // Ohm
	//ret_compensate_value = (gFG_current * (gFG_resistance_bat + R_FG_VALUE)) / 1000;
#ifdef OCV_USE_BATSNS
       if(vfet_status == 1)
           ret_compensate_value = (gFG_current * (gFG_resistance_bat + R_FG_VALUE + R_QFET_VALUE + FG_METER_RESISTANCE)) / 1000;
       else
	ret_compensate_value = (gFG_current * (gFG_resistance_bat + R_FG_VALUE + FG_METER_RESISTANCE)) / 1000;
#else
       ret_compensate_value = (gFG_current * (gFG_resistance_bat + R_FG_VALUE + FG_METER_RESISTANCE)) / 1000;
#endif
	//ret_compensate_value = (gFG_current * (gFG_resistance_bat - R_FG_VALUE)) / 1000;
	ret_compensate_value = (ret_compensate_value+(10/2)) / 10; // 20101103

    if (gFG_Is_Charging == KAL_TRUE)
    {
		ret_compensate_value = ret_compensate_value - (ret_compensate_value*2);
    }

	gFG_compensate_value = ret_compensate_value;

	//if(gFG_booting_counter_I_FLAG != 2)
	if (Enable_FGADC_LOG == 1)
	{
	xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[fgauge_compensate_battery_voltage_recursion] %d,%d,%d,%d\r\n",
			temp_voltage_1, temp_voltage_2, gFG_resistance_bat, ret_compensate_value);
	}

	//xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[CompensateVoltage] Ori_voltage:%d, compensate_value:%d, gFG_resistance_bat:%d, gFG_current:%d\r\n",
	//	ori_voltage, ret_compensate_value, gFG_resistance_bat, gFG_current);

    return ret_compensate_value;
}

#ifdef MTK_NCP1851_SUPPORT
kal_int32 fgauge_read_voltage_ncp1851(void) //for OCV table lookup
{
    int vol_battery;

    vol_battery = PMIC_IMM_GetBatChannelValue(100, 0);

    if(gFG_voltage_pre == -500)
    {
        gFG_voltage_pre = vol_battery; // for init

        return vol_battery;
    }

    return vol_battery;
}

void fgauge_read_avg_I_V(void)
{
    int vol[14] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    int vol_sum = 0, vol_temp = 0;
    kal_int32 cur_sum = 0;
    int i, j;
#ifdef OCV_USE_BATSNS
    kal_int32 vfet_status;
    vfet_status = ncp1851_get_vfet_ok();
#endif

    for(i=0;i<14;i++)
    {
#ifdef OCV_USE_BATSNS
        if(vfet_status == 1)
            vol[i] = PMIC_IMM_GetOneChannelValue(AUXADC_BATTERY_VOLTAGE_CHANNEL, 1);
        else
            vol[i] = PMIC_IMM_GetBatChannelValue(1, 0);
#else
        vol[i] = PMIC_IMM_GetBatChannelValue(1, 0);
#endif
        gFG_current = fgauge_read_current();

        vol[i] = vol[i] + fgauge_compensate_battery_voltage_recursion(vol[i],5); //mV
        vol[i] = vol[i] + OCV_BOARD_COMPESATE;

        cur_sum += gFG_current;
        //xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_avg_I_V] #%d voltage[%d], current[%d]\r\n", i, vol[i], gFG_current);
    }

    //sorting vol
    for(i=0; i<14 ; i++)
    {
        for(j=i; j<14 ; j++)
        {
            if(vol[j] < vol[i])
            {
                vol_temp = vol[j];
                vol[j] = vol[i];
                vol[i] = vol_temp;
            }
        }
    }

    for(i=2;i<12;i++)
    {
        if (Enable_FGADC_LOG == 1)
        {
            //xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_avg_I_V] vol[%d] = %d\r\n", i, vol[i]);
        }
        vol_sum += vol[i];
    }

    gFG_voltage = vol_sum / 10;
    gFG_current = cur_sum / 14;

    if((g_ocv_lookup_done == 0) && (g_boot_charging == 0))
    {
	    gFG_voltage = PMIC_IMM_GetOneChannelValue(AUXADC_HW_OCV_CHANNEL,1); //use HW OCV
	    xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[fgauge_read_avg_I_V] HW OCV[%d], SW OCV[%d]\r\n", gFG_voltage, (vol_sum/10));
    }

    if (Enable_FGADC_LOG == 1)
    {
        xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[fgauge_read_avg_I_V] AVG voltage[%d], current[%d]\r\n", gFG_voltage, gFG_current);
    }
}
#endif

void fgauge_construct_battery_profile(kal_int32 temperature, BATTERY_PROFILE_STRUC_P temp_profile_p)
{
    BATTERY_PROFILE_STRUC_P low_profile_p, high_profile_p;
    kal_int32 low_temperature, high_temperature;
    int i, saddles;
	kal_int32 temp_v_1 = 0, temp_v_2 = 0;

	if (temperature <= TEMPERATURE_T1)
    {
        low_profile_p    = fgauge_get_profile(TEMPERATURE_T0);
        high_profile_p   = fgauge_get_profile(TEMPERATURE_T1);
        low_temperature  = (-10);
        high_temperature = TEMPERATURE_T1;

		if(temperature < low_temperature)
		{
			temperature = low_temperature;
		}
    }
    else if (temperature <= TEMPERATURE_T2)
    {
        low_profile_p    = fgauge_get_profile(TEMPERATURE_T1);
        high_profile_p   = fgauge_get_profile(TEMPERATURE_T2);
        low_temperature  = TEMPERATURE_T1;
        high_temperature = TEMPERATURE_T2;

		if(temperature < low_temperature)
		{
			temperature = low_temperature;
		}
    }
    else
    {
        low_profile_p    = fgauge_get_profile(TEMPERATURE_T2);
        high_profile_p   = fgauge_get_profile(TEMPERATURE_T3);
        low_temperature  = TEMPERATURE_T2;
        high_temperature = TEMPERATURE_T3;

		if(temperature > high_temperature)
		{
			temperature = high_temperature;
		}
    }

    saddles = fgauge_get_saddles();

    for (i = 0; i < saddles; i++)
    {
		if( ((high_profile_p + i)->voltage) > ((low_profile_p + i)->voltage) )
		{
			temp_v_1 = (high_profile_p + i)->voltage;
			temp_v_2 = (low_profile_p + i)->voltage;

			(temp_profile_p + i)->voltage = temp_v_2 +
			(
				(
					(temperature - low_temperature) *
					(temp_v_1 - temp_v_2)
				) /
				(high_temperature - low_temperature)
			);
		}
		else
		{
			temp_v_1 = (low_profile_p + i)->voltage;
			temp_v_2 = (high_profile_p + i)->voltage;

			(temp_profile_p + i)->voltage = temp_v_2 +
			(
				(
					(high_temperature - temperature) *
					(temp_v_1 - temp_v_2)
				) /
				(high_temperature - low_temperature)
			);
		}

        (temp_profile_p + i)->percentage = (high_profile_p + i)->percentage;
#if 0
		(temp_profile_p + i)->voltage = temp_v_2 +
			(
				(
					(temperature - low_temperature) *
					(temp_v_1 - temp_v_2)
				) /
				(high_temperature - low_temperature)
			);
#endif
    }

#ifndef MTK_NCP1851_SUPPORT
	// Dumpt new battery profile
	for (i = 0; i < saddles ; i++)
	{
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "<DOD,Voltage> at %d = <%d,%d>\r\n",temperature, (temp_profile_p+i)->percentage, (temp_profile_p+i)->voltage);
	}
#endif
}

void fgauge_construct_r_table_profile(kal_int32 temperature, R_PROFILE_STRUC_P temp_profile_p)
{
    R_PROFILE_STRUC_P low_profile_p, high_profile_p;
    kal_int32 low_temperature, high_temperature;
    int i, saddles;
	kal_int32 temp_v_1 = 0, temp_v_2 = 0;
	kal_int32 temp_r_1 = 0, temp_r_2 = 0;

	if (temperature <= TEMPERATURE_T1)
    {
        low_profile_p    = fgauge_get_profile_r_table(TEMPERATURE_T0);
        high_profile_p   = fgauge_get_profile_r_table(TEMPERATURE_T1);
        low_temperature  = (-10);
        high_temperature = TEMPERATURE_T1;

		if(temperature < low_temperature)
		{
			temperature = low_temperature;
		}
    }
    else if (temperature <= TEMPERATURE_T2)
    {
        low_profile_p    = fgauge_get_profile_r_table(TEMPERATURE_T1);
        high_profile_p   = fgauge_get_profile_r_table(TEMPERATURE_T2);
        low_temperature  = TEMPERATURE_T1;
        high_temperature = TEMPERATURE_T2;

		if(temperature < low_temperature)
		{
			temperature = low_temperature;
		}
    }
    else
    {
        low_profile_p    = fgauge_get_profile_r_table(TEMPERATURE_T2);
        high_profile_p   = fgauge_get_profile_r_table(TEMPERATURE_T3);
        low_temperature  = TEMPERATURE_T2;
        high_temperature = TEMPERATURE_T3;

		if(temperature > high_temperature)
		{
			temperature = high_temperature;
		}
    }

    saddles = fgauge_get_saddles_r_table();

	/* Interpolation for V_BAT */
    for (i = 0; i < saddles; i++)
    {
		if( ((high_profile_p + i)->voltage) > ((low_profile_p + i)->voltage) )
		{
			temp_v_1 = (high_profile_p + i)->voltage;
			temp_v_2 = (low_profile_p + i)->voltage;

			(temp_profile_p + i)->voltage = temp_v_2 +
			(
				(
					(temperature - low_temperature) *
					(temp_v_1 - temp_v_2)
				) /
				(high_temperature - low_temperature)
			);
		}
		else
		{
			temp_v_1 = (low_profile_p + i)->voltage;
			temp_v_2 = (high_profile_p + i)->voltage;

			(temp_profile_p + i)->voltage = temp_v_2 +
			(
				(
					(high_temperature - temperature) *
					(temp_v_1 - temp_v_2)
				) /
				(high_temperature - low_temperature)
			);
		}

#if 0
        //(temp_profile_p + i)->resistance = (high_profile_p + i)->resistance;

		(temp_profile_p + i)->voltage = temp_v_2 +
			(
				(
					(temperature - low_temperature) *
					(temp_v_1 - temp_v_2)
				) /
				(high_temperature - low_temperature)
			);
#endif
    }

	/* Interpolation for R_BAT */
    for (i = 0; i < saddles; i++)
    {
		if( ((high_profile_p + i)->resistance) > ((low_profile_p + i)->resistance) )
		{
			temp_r_1 = (high_profile_p + i)->resistance;
			temp_r_2 = (low_profile_p + i)->resistance;

			(temp_profile_p + i)->resistance = temp_r_2 +
			(
				(
					(temperature - low_temperature) *
					(temp_r_1 - temp_r_2)
				) /
				(high_temperature - low_temperature)
			);
		}
		else
		{
			temp_r_1 = (low_profile_p + i)->resistance;
			temp_r_2 = (high_profile_p + i)->resistance;

			(temp_profile_p + i)->resistance = temp_r_2 +
			(
				(
					(high_temperature - temperature) *
					(temp_r_1 - temp_r_2)
				) /
				(high_temperature - low_temperature)
			);
		}

#if 0
        //(temp_profile_p + i)->voltage = (high_profile_p + i)->voltage;

		(temp_profile_p + i)->resistance = temp_r_2 +
			(
				(
					(temperature - low_temperature) *
					(temp_r_1 - temp_r_2)
				) /
				(high_temperature - low_temperature)
			);
#endif
    }
#ifndef MTK_NCP1851_SUPPORT
	// Dumpt new r-table profile
	for (i = 0; i < saddles ; i++)
	{
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "<Rbat,VBAT> at %d = <%d,%d>\r\n",temperature, (temp_profile_p+i)->resistance, (temp_profile_p+i)->voltage);
	}
#endif
}


kal_int32 fgauge_get_dod0(kal_int32 voltage, kal_int32 temperature, kal_bool bOcv)
{
    kal_int32 dod0 = 0;
    int i=0, saddles=0, jj=0;
    BATTERY_PROFILE_STRUC_P profile_p;
	R_PROFILE_STRUC_P profile_p_r_table;

/* R-Table (First Time) */
    // Re-constructure r-table profile according to current temperature
    profile_p_r_table = fgauge_get_profile_r_table(TEMPERATURE_T);
    if (profile_p_r_table == NULL)
    {
		xlog_printk(ANDROID_LOG_WARN, "Power/Battery", "[FGADC] fgauge_get_profile_r_table : create table fail !\r\n");
    }
    fgauge_construct_r_table_profile(temperature, profile_p_r_table);

    // Re-constructure battery profile according to current temperature
    profile_p = fgauge_get_profile(TEMPERATURE_T);
    if (profile_p == NULL)
    {
		xlog_printk(ANDROID_LOG_WARN, "Power/Battery", "[FGADC] fgauge_get_profile : create table fail !\r\n");
        return 100;
    }
    fgauge_construct_battery_profile(temperature, profile_p);

    // Get total saddle points from the battery profile
    saddles = fgauge_get_saddles();

    // If the input voltage is not OCV, compensate to ZCV due to battery loading
    // Compasate battery voltage from current battery voltage
    jj=0;
    if (bOcv == KAL_FALSE)
    {
    	while( gFG_current == 0 )
		{
			gFG_current = fgauge_read_current();
			if(jj > 10)
				break;
			jj++;
		}
        //voltage = voltage + fgauge_compensate_battery_voltage(voltage); //mV
        voltage = voltage + fgauge_compensate_battery_voltage_recursion(voltage,5); //mV
        xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] compensate_battery_voltage, voltage=%d\r\n", voltage);
    }

    // If battery voltage is less then mimimum profile voltage, then return 100
    // If battery voltage is greater then maximum profile voltage, then return 0
	if (voltage > (profile_p+0)->voltage)
    {
        return 0;
    }
    if (voltage < (profile_p+saddles-1)->voltage)
    {
        return 100;
    }

    // get DOD0 according to current temperature
    for (i = 0; i < saddles - 1; i++)
    {
		//xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "Try <%d,%d> on %d\r\n", (profile_p+i)->voltage, (profile_p+i)->percentage, voltage);

        if ((voltage <= (profile_p+i)->voltage) && (voltage >= (profile_p+i+1)->voltage))
        {
            dod0 = (profile_p+i)->percentage +
				(
					(
						( ((profile_p+i)->voltage) - voltage ) *
						( ((profile_p+i+1)->percentage) - ((profile_p + i)->percentage) )
					) /
					( ((profile_p+i)->voltage) - ((profile_p+i+1)->voltage) )
				);

			//xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "DOD=%d\r\n", dod0);

            break;
        }
    }

#if 0
	// Dumpt new battery profile
	for (i = 0; i < saddles ; i++)
	{
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "<Voltage,DOD> at %d = <%d,%d>\r\n",gFG_bat_temperature, (profile_p+i)->voltage, (profile_p+i)->percentage);
	}
#endif

    return dod0;
}

extern int g_HW_Charging_Done;
void fg_qmax_update_for_aging(void)
{
	if(g_HW_Charging_Done == 1) // charging full
	{
		if(gFG_DOD0 > 85)
		{
			gFG_BATT_CAPACITY_aging = ( ( (gFG_columb * 1000) + 5 )  / gFG_DOD0 ) / 10;

			// tuning
			gFG_BATT_CAPACITY_aging = (gFG_BATT_CAPACITY_aging * 100) / AGING_TUNING_VALUE;

			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fg_qmax_update_for_aging] need update : gFG_columb=%d, gFG_DOD0=%d, new_qmax=%d\r\n",
				gFG_columb, gFG_DOD0, gFG_BATT_CAPACITY_aging);
		}
		else
		{
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fg_qmax_update_for_aging] no update : gFG_columb=%d, gFG_DOD0=%d, new_qmax=%d\r\n",
				gFG_columb, gFG_DOD0, gFG_BATT_CAPACITY_aging);
		}
	}
	else
	{
		if (Enable_FGADC_LOG == 1){
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fg_qmax_update_for_aging] g_HW_Charging_Done=%d\r\n", g_HW_Charging_Done);
		}
	}
}

int g_update_qmax_flag=1;
kal_int32 fgauge_update_dod(void)
{
    kal_int32 FG_dod_1 = 0;
	int adjust_coulomb_counter=CAR_TUNE_VALUE;

	if(gFG_DOD0 > 100)
	{
		gFG_DOD0=100;
		if (Enable_FGADC_LOG == 1){
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_update_dod] gFG_DOD0 set to 100, gFG_columb=%d\r\n", gFG_columb);
	}
	}
	else if(gFG_DOD0 < 0)
	{
		gFG_DOD0=0;
		if (Enable_FGADC_LOG == 1){
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_update_dod] gFG_DOD0 set to 0, gFG_columb=%d\r\n", gFG_columb);
	}
	}
	else
	{
	}

	gFG_temp = fgauge_read_temperature();

	if(g_update_qmax_flag == 1)
	{
	gFG_BATT_CAPACITY = fgauge_get_Q_max(gFG_temp);
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_update_dod] gFG_BATT_CAPACITY=%d, gFG_BATT_CAPACITY_aging=%d, gFG_BATT_CAPACITY_init_high_current=%d\r\n",
			gFG_BATT_CAPACITY, gFG_BATT_CAPACITY_aging, gFG_BATT_CAPACITY_init_high_current);
		g_update_qmax_flag = 0;
	}

	//FG_dod_1 =  gFG_DOD0 - ((( (gFG_columb*1000*adjust_coulomb_counter)/100 )/gFG_BATT_CAPACITY)+5)/10;
	//FG_dod_1 =  gFG_DOD0 - ((gFG_columb*100)/gFG_BATT_CAPACITY);
	FG_dod_1 =  gFG_DOD0 - ((gFG_columb*100)/gFG_BATT_CAPACITY_aging);

	if (Enable_FGADC_LOG == 1){
		xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[fgauge_update_dod] FG_dod_1=%d, adjust_coulomb_counter=%d, gFG_columb=%d, gFG_DOD0=%d, gFG_temp=%d, gFG_BATT_CAPACITY=%d\r\n",
		FG_dod_1, adjust_coulomb_counter, gFG_columb, gFG_DOD0, gFG_temp, gFG_BATT_CAPACITY);
	}

	if(FG_dod_1 > 100)
	{
		FG_dod_1=100;
		if (Enable_FGADC_LOG == 1){
		xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[fgauge_update_dod] FG_dod_1 set to 100, gFG_columb=%d\r\n", gFG_columb);
	}
	}
	else if(FG_dod_1 < 0)
	{
		FG_dod_1=0;
		if (Enable_FGADC_LOG == 1){
		xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[fgauge_update_dod] FG_dod_1 set to 0, gFG_columb=%d\r\n", gFG_columb);
	}
	}
	else
	{
	}

    return FG_dod_1;
}

kal_int32 fgauge_read_capacity(kal_int32 type)
{
    kal_int32 voltage;
    kal_int32 temperature;
    kal_int32 dvalue = 0;

	kal_int32 C_0mA=0;
	kal_int32 C_400mA=0;
	kal_int32 dvalue_new=0;

    if (type == 0) // for initialization
    {
        // Use voltage to calculate capacity
#ifdef MTK_NCP1851_SUPPORT
        voltage = fgauge_read_voltage_ncp1851();
#else
        voltage = fgauge_read_voltage(); // in unit of mV
#endif
        temperature = fgauge_read_temperature();
        //dvalue = fgauge_get_dod0(voltage, temperature, KAL_TRUE); // need not compensate
        dvalue = fgauge_get_dod0(voltage, temperature, KAL_FALSE); // need compensate vbat
    }
    else
    {
        // Use DOD0 and columb counter to calculate capacity
        dvalue = fgauge_update_dod(); // DOD1 = DOD0 + (-CAR)/Qmax
    }
	//xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_capacity] %d\r\n", dvalue);

	gFG_DOD1 = dvalue;

	//User View on HT~LT----------------------------------------------------------
	gFG_temp = fgauge_read_temperature();
	C_0mA = fgauge_get_Q_max(gFG_temp);
	C_400mA = fgauge_get_Q_max_high_current(gFG_temp);
	if(C_0mA > C_400mA)
	{
		dvalue_new = (100-dvalue) - ( ( (C_0mA-C_400mA) * (dvalue) ) / C_400mA );
		dvalue = 100 - dvalue_new;
	}
	if (Enable_FGADC_LOG == 1){
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_capacity] %d,%d,%d,%d,%d,D1=%d,D0=%d\r\n",
			gFG_temp, C_0mA, C_400mA, dvalue, dvalue_new, gFG_DOD1, gFG_DOD0);
	}
	//----------------------------------------------------------------------------

	#if 0
    //Battery Aging update ----------------------------------------------------------
    dvalue_new = dvalue;
    dvalue = ( (dvalue_new * gFG_BATT_CAPACITY_init_high_current * 100) / gFG_BATT_CAPACITY_aging ) / 100;
    if (Enable_FGADC_LOG >= 1){
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_capacity] dvalue=%d, dvalue_new=%d, gFG_BATT_CAPACITY_init_high_current=%d, gFG_BATT_CAPACITY_aging=%d\r\n",
			dvalue, dvalue_new, gFG_BATT_CAPACITY_init_high_current, gFG_BATT_CAPACITY_aging);
	}
    //----------------------------------------------------------------------------
	#endif

	gFG_DOD1_return = dvalue;
	dvalue = 100 - gFG_DOD1_return;

        //Smooth capacity change
        if(dvalue > bat_volt_check_point + 1)
            dvalue --;
        else if(bat_volt_check_point > dvalue + 1)
            dvalue ++;

	if(dvalue <= 1)
	{
		dvalue=1;
		if (Enable_FGADC_LOG == 1){
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_capacity] dvalue<=1 and set dvalue=1 !!\r\n");
		}
	}

    return dvalue;
}

kal_int32 fgauge_read_capacity_by_v(void)
{
	int i = 0, saddles = 0;
	BATTERY_PROFILE_STRUC_P profile_p;
	kal_int32 ret_percent = 0;

	profile_p = fgauge_get_profile(TEMPERATURE_T);
    if (profile_p == NULL)
    {
		xlog_printk(ANDROID_LOG_WARN, "Power/Battery", "[FGADC] fgauge get ZCV profile : fail !\r\n");
        return 100;
    }

	saddles = fgauge_get_saddles();

	if (gFG_voltage > (profile_p+0)->voltage)
    {
    	//xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_capacity_by_v] 100:%d,%d\r\n", gFG_voltage, (profile_p+0)->voltage);
        return 100; // battery capacity, not dod
        //return 0;
    }
    if (gFG_voltage < (profile_p+saddles-1)->voltage)
    {
    	//xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_capacity_by_v] 0:%d,%d\r\n", gFG_voltage, (profile_p+saddles-1)->voltage);
        return 0; // battery capacity, not dod
        //return 100;
    }

    for (i = 0; i < saddles - 1; i++)
    {
        if ((gFG_voltage <= (profile_p+i)->voltage) && (gFG_voltage >= (profile_p+i+1)->voltage))
        {
            ret_percent = (profile_p+i)->percentage +
				(
					(
						( ((profile_p+i)->voltage) - gFG_voltage ) *
						( ((profile_p+i+1)->percentage) - ((profile_p + i)->percentage) )
					) /
					( ((profile_p+i)->voltage) - ((profile_p+i+1)->voltage) )
				);

            break;
        }

		//xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_capacity_by_v] gFG_voltage=%d\r\n", gFG_voltage);
		//xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_capacity_by_v] (profile_p+i)->percentag=%d\r\n", (profile_p+i)->percentage);
		//xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_capacity_by_v] ((profile_p+i+1)->percentage)=%d\r\n", ((profile_p+i+1)->percentage));
		//xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_capacity_by_v] ((profile_p+i)->voltage)=%d\r\n", ((profile_p+i)->voltage));
		//xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_capacity_by_v] ((profile_p+i+1)->voltage) =%d\r\n", ((profile_p+i+1)->voltage));
    }
	ret_percent = 100 - ret_percent;

	return ret_percent;
}

kal_int32 fgauge_read_r_bat_by_v(kal_int32 voltage)
{
	int i = 0, saddles = 0;
	R_PROFILE_STRUC_P profile_p;
	kal_int32 ret_r = 0;

	profile_p = fgauge_get_profile_r_table(TEMPERATURE_T);
    if (profile_p == NULL)
    {
		xlog_printk(ANDROID_LOG_WARN, "Power/Battery", "[FGADC] fgauge get R-Table profile : fail !\r\n");
        return (profile_p+0)->resistance;
    }

	saddles = fgauge_get_saddles_r_table();

	if (voltage > (profile_p+0)->voltage)
    {
        return (profile_p+0)->resistance;
    }
    if (voltage < (profile_p+saddles-1)->voltage)
    {
        return (profile_p+saddles-1)->resistance;
    }

    for (i = 0; i < saddles - 1; i++)
    {
        if ((voltage <= (profile_p+i)->voltage) && (voltage >= (profile_p+i+1)->voltage))
        {
            ret_r = (profile_p+i)->resistance +
				(
					(
						( ((profile_p+i)->voltage) - voltage ) *
						( ((profile_p+i+1)->resistance) - ((profile_p + i)->resistance) )
					) /
					( ((profile_p+i)->voltage) - ((profile_p+i+1)->voltage) )
				);
            break;
        }
    }

	return ret_r;
}

kal_int32 fgauge_read_v_by_capacity(int bat_capacity)
{
	int i = 0, saddles = 0;
	BATTERY_PROFILE_STRUC_P profile_p;
	kal_int32 ret_volt = 0;

	profile_p = fgauge_get_profile(TEMPERATURE_T);
    if (profile_p == NULL)
    {
		xlog_printk(ANDROID_LOG_WARN, "Power/Battery", "[fgauge_read_v_by_capacity] fgauge get ZCV profile : fail !\r\n");
        return 3700;
    }

	saddles = fgauge_get_saddles();

	if (bat_capacity < (profile_p+0)->percentage)
    {
        return 3700;
    }
    if (bat_capacity > (profile_p+saddles-1)->percentage)
    {
        return 3700;
    }

    for (i = 0; i < saddles - 1; i++)
    {
        if ((bat_capacity >= (profile_p+i)->percentage) && (bat_capacity <= (profile_p+i+1)->percentage))
        {
            ret_volt = (profile_p+i)->voltage -
				(
					(
						( bat_capacity - ((profile_p+i)->percentage) ) *
						( ((profile_p+i)->voltage) - ((profile_p+i+1)->voltage) )
					) /
					( ((profile_p+i+1)->percentage) - ((profile_p+i)->percentage) )
				);

			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_v_by_capacity] ret_volt=%d\r\n", ret_volt);
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_v_by_capacity] (profile_p+i)->percentag=%d\r\n", (profile_p+i)->percentage);
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_v_by_capacity] ((profile_p+i+1)->percentage)=%d\r\n", ((profile_p+i+1)->percentage));
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_v_by_capacity] ((profile_p+i)->voltage)=%d\r\n", ((profile_p+i)->voltage));
			xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[fgauge_read_v_by_capacity] ((profile_p+i+1)->voltage) =%d\r\n", ((profile_p+i+1)->voltage));

            break;
        }
    }

	return ret_volt;
}

#define FG_VBAT_AVERAGE_SIZE 36 // 36*5s=180s=3mins
//#define MinErrorOffset 30 //30mA
//#define MinErrorOffset 50 //50mA
//#define MinErrorOffset 200 //200mV
#define MinErrorOffset 1000 //1000mV
kal_bool gFGvbatBufferFirst = KAL_FALSE;
static unsigned short FGvbatVoltageBuffer[FG_VBAT_AVERAGE_SIZE];
static int FGbatteryIndex = 0;
static int FGbatteryVoltageSum = 0;
kal_int32 gFG_voltage_AVG = 0;
kal_int32 gFG_vbat_offset=0;
kal_int32 gFG_voltageVBAT=0;

void fgauge_Normal_Mode_Work(void)
{
	int i=0;

//1. Get Raw Data
	gFG_current = fgauge_read_current();
#ifndef MTK_NCP1851_SUPPORT
    gFG_voltage = fgauge_read_voltage();
#endif
	gFG_voltage_init = gFG_voltage;
#ifdef MTK_NCP1851_SUPPORT
       fgauge_read_avg_I_V();
#endif

#ifndef MTK_NCP1851_SUPPORT
	if (gFG_Is_Charging == KAL_TRUE)
	{
		gFG_voltage = gFG_voltage + fgauge_compensate_battery_voltage_recursion(gFG_voltage,5); //mV
	}
	else
	{
		gFG_voltage = gFG_voltage + fgauge_compensate_battery_voltage_recursion(gFG_voltage,5); //mV
	}

	gFG_voltage = gFG_voltage + OCV_BOARD_COMPESATE;
#endif

	gFG_current = fgauge_read_current();
	gFG_columb = fgauge_read_columb();

//1.1 Average FG_voltage
	/**************** Averaging : START ****************/
	if(gFG_booting_counter_I_FLAG != 0)
	{
	    if (!gFGvbatBufferFirst)
	    {
	        for (i=0; i<FG_VBAT_AVERAGE_SIZE; i++) {
	            FGvbatVoltageBuffer[i] = gFG_voltage;
	        }

	        FGbatteryVoltageSum = gFG_voltage * FG_VBAT_AVERAGE_SIZE;

			gFG_voltage_AVG = gFG_voltage;

			gFGvbatBufferFirst = KAL_TRUE;
	    }

		if(gFG_voltage >= gFG_voltage_AVG)
		{
			gFG_vbat_offset = (gFG_voltage - gFG_voltage_AVG);
		}
		else
		{
			gFG_vbat_offset = (gFG_voltage_AVG - gFG_voltage);
		}

		if(gFG_vbat_offset <= MinErrorOffset)
		{
		    FGbatteryVoltageSum -= FGvbatVoltageBuffer[FGbatteryIndex];
		    FGbatteryVoltageSum += gFG_voltage;
		    FGvbatVoltageBuffer[FGbatteryIndex] = gFG_voltage;

		    gFG_voltage_AVG = FGbatteryVoltageSum / FG_VBAT_AVERAGE_SIZE;
			gFG_voltage = gFG_voltage_AVG;

		    FGbatteryIndex++;
		    if (FGbatteryIndex >= FG_VBAT_AVERAGE_SIZE)
		        FGbatteryIndex = 0;
#ifndef MTK_NCP1851_SUPPORT
			if (Enable_FGADC_LOG == 1)
			{
				xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG_BUFFER] ");
				for (i=0; i<FG_VBAT_AVERAGE_SIZE; i++) {
		            xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "%d,", FGvbatVoltageBuffer[i]);
		        }
				xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "\r\n");
			}
#endif
		}
		else
		{
			if (Enable_FGADC_LOG == 1){
				xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] Over MinErrorOffset:V=%d,Avg_V=%d, ", gFG_voltage, gFG_voltage_AVG);
			}

			gFG_voltage = gFG_voltage_AVG;

			if (Enable_FGADC_LOG == 1){
				xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "Avg_V need write back to V : V=%d,Avg_V=%d.\r\n", gFG_voltage, gFG_voltage_AVG);
			}
		}
	}
    /**************** Averaging : END ****************/
	gFG_voltageVBAT = gFG_voltage;

//2. Calculate battery capacity by VBAT
	gFG_capacity_by_v = fgauge_read_capacity_by_v();

//3. Calculate battery capacity by Coulomb Counter
	gFG_capacity_by_c = fgauge_read_capacity(1);
	gEstBatCapacity = gFG_capacity_by_c;

//4. update DOD0 after booting Xs
	if(gFG_booting_counter_I_FLAG == 1)
	{
		gFG_booting_counter_I_FLAG = 2;

#ifdef MTK_NCP1851_SUPPORT
		if(g_ocv_lookup_done == 0)
		{
			gFG_capacity = gFG_capacity_by_v;

			gFG_capacity_by_c_init = gFG_capacity;
			gFG_capacity_by_c = gFG_capacity;
			gFG_pre_temp = gFG_temp;

			gFG_DOD0 = 100 - gFG_capacity;
			gFG_DOD1=gFG_DOD0;
			g_ocv_lookup_done = 1;
		}
		g_boot_charging = 0;		
#else
		gFG_capacity = gFG_capacity_by_v;

	    gFG_capacity_by_c_init = gFG_capacity;
	    gFG_capacity_by_c = gFG_capacity;
	    gFG_pre_temp = gFG_temp;

	    gFG_DOD0 = 100 - gFG_capacity;
		gFG_DOD1=gFG_DOD0;
#endif
		bat_volt_check_point = gFG_capacity;
		gfg_percent_check_point = bat_volt_check_point;

		xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[FGADC] update DOD0 = %d after booting %d s, (%d)\r\n", gFG_DOD0, (MAX_BOOTING_TIME_FGCURRENT), gFG_current);

		gFG_15_vlot = fgauge_read_v_by_capacity(86); //14%
		xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[FGADC] gFG_15_vlot = %dmV\r\n", gFG_15_vlot);
		if( (gFG_15_vlot > 3800) || (gFG_15_vlot < 3600) )
		{
			xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[FGADC] gFG_15_vlot(%d) over range, reset to 3700\r\n", gFG_15_vlot);
			gFG_15_vlot = 3700;
		}

		gFG_current_auto_detect_R_fg_result = gFG_current_auto_detect_R_fg_total / gFG_current_auto_detect_R_fg_count;
		if(gFG_current_auto_detect_R_fg_result <= CURRENT_DETECT_R_FG)
		{
			gForceADCsolution=1;

			batteryBufferFirst = KAL_FALSE; // for init array values when measuring by AUXADC

			xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[FGADC] Detect NO Rfg, use AUXADC report. (%d=%d/%d)(%d)\r\n",
				gFG_current_auto_detect_R_fg_result, gFG_current_auto_detect_R_fg_total,
				gFG_current_auto_detect_R_fg_count, gForceADCsolution);
		}
		else
		{
			if(gForceADCsolution == 0)
			{
				gForceADCsolution=0;

				xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[FGADC] Detect Rfg, use FG report. (%d=%d/%d)(%d)\r\n",
				gFG_current_auto_detect_R_fg_result, gFG_current_auto_detect_R_fg_total,
				gFG_current_auto_detect_R_fg_count, gForceADCsolution);
		}
			else
			{
				xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[FGADC] Detect Rfg, but use AUXADC report. due to gForceADCsolution=%d \r\n",
					gForceADCsolution);
			}
		}
		Enable_FGADC_LOG = 0;
	}

}

kal_int32 fgauge_get_Q_max(kal_int16 temperature)
{
	kal_int32 ret_Q_max=0;
	kal_int32 low_temperature = 0, high_temperature = 0;
	kal_int32 low_Q_max = 0, high_Q_max = 0;

	if (temperature <= TEMPERATURE_T1)
    {
        low_temperature = (-10);
		low_Q_max = Q_MAX_NEG_10;
        high_temperature = TEMPERATURE_T1;
		high_Q_max = Q_MAX_POS_0;

		if(temperature < low_temperature)
		{
			temperature = low_temperature;
		}
    }
	else if (temperature <= TEMPERATURE_T2)
    {
        low_temperature = TEMPERATURE_T1;
		low_Q_max = Q_MAX_POS_0;
        high_temperature = TEMPERATURE_T2;
		high_Q_max = Q_MAX_POS_25;

		if(temperature < low_temperature)
		{
			temperature = low_temperature;
		}
    }
    else
    {
    	low_temperature  = TEMPERATURE_T2;
		low_Q_max = Q_MAX_POS_25;
        high_temperature = TEMPERATURE_T3;
		high_Q_max = Q_MAX_POS_50;

		if(temperature > high_temperature)
		{
			temperature = high_temperature;
		}
    }

	ret_Q_max = low_Q_max +
	(
		(
			(temperature - low_temperature) *
			(high_Q_max - low_Q_max)
		) /
		(high_temperature - low_temperature)
	);

	if (Enable_FGADC_LOG == 1){
		xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[fgauge_get_Q_max] Q_max = %d\r\n", ret_Q_max);
	}

	return ret_Q_max;
}

kal_int32 fgauge_get_Q_max_high_current(kal_int16 temperature)
{
	kal_int32 ret_Q_max=0;
	kal_int32 low_temperature = 0, high_temperature = 0;
	kal_int32 low_Q_max = 0, high_Q_max = 0;

	if (temperature <= TEMPERATURE_T1)
    {
        low_temperature = (-10);
		low_Q_max = Q_MAX_NEG_10_H_CURRENT;
        high_temperature = TEMPERATURE_T1;
		high_Q_max = Q_MAX_POS_0_H_CURRENT;

		if(temperature < low_temperature)
		{
			temperature = low_temperature;
		}
    }
	else if (temperature <= TEMPERATURE_T2)
    {
        low_temperature = TEMPERATURE_T1;
		low_Q_max = Q_MAX_POS_0_H_CURRENT;
        high_temperature = TEMPERATURE_T2;
		high_Q_max = Q_MAX_POS_25_H_CURRENT;

		if(temperature < low_temperature)
		{
			temperature = low_temperature;
		}
    }
    else
    {
    	low_temperature  = TEMPERATURE_T2;
		low_Q_max = Q_MAX_POS_25_H_CURRENT;
        high_temperature = TEMPERATURE_T3;
		high_Q_max = Q_MAX_POS_50_H_CURRENT;

		if(temperature > high_temperature)
		{
			temperature = high_temperature;
		}
    }

	ret_Q_max = low_Q_max +
	(
		(
			(temperature - low_temperature) *
			(high_Q_max - low_Q_max)
		) /
		(high_temperature - low_temperature)
	);

	if (Enable_FGADC_LOG == 1){
		xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[fgauge_get_Q_max_high_current] Q_max = %d\r\n", ret_Q_max);
	}

	return ret_Q_max;
}

#ifdef MTK_NCP1851_SUPPORT
void fgauge_precharge_init(void)
{
    kal_uint32 ret = 0;
    kal_int32 current_val = 0;
    int i;

    //(1)	i2c_write (0x60, 0xC8, 0x01); // Enable VA2
    ret=pmic_config_interface(0xC8, 0x1, 0xFF, 0x0);
    //(2)	i2c_write (0x61, 0x15, 0x00); // Enable FGADC clock for digital
    ret=pmic_bank1_config_interface(0x15, 0x0, 0xFF, 0x0);
    //(3)	i2c_write (0x61, 0x69, 0x28); // Set current mode, auto-calibration mode and 32KHz clock source
    ret=pmic_bank1_config_interface(0x69, 0x28, 0xFF, 0x0);
    //(4)	i2c_write (0x61, 0x69, 0x29); // Enable FGADC
    ret=pmic_bank1_config_interface(0x69, 0x29, 0xFF, 0x0);

    i=0;
    while( current_val == 0 )
    {
        current_val = fgauge_read_current();
        if(i > 10)
        break;
        i++;
    }

    if(current_val == 0)
        xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[fgauge_precharge_init] current value is still 0\r\n");
}

void fgauge_precharge_uninit(void)
{
    volatile kal_uint16 val_car = 1;
    kal_uint32 ret = 0;
    BATTERY_PROFILE_STRUC_P profile_p = NULL;
    R_PROFILE_STRUC_P profile_p_r_table = NULL;

    while(val_car != 0x0)
    {
        ret=pmic_bank1_config_interface(0x6A, 0x71, 0xFF, 0x0);
        val_car = fgauge_read_columb_reset();
        xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "#");
    }

    //(1)	i2c_write (0x61, 0x69, 0x29); // Disable FGADC
    ret=pmic_bank1_config_interface(0x69, 0x28, 0xFF, 0x0);

    //reset parameters to initial state
    gFG_Is_Charging = KAL_FALSE;
    current_get_ori = 0;
    gFG_bat_temperature = 0;
    gFG_voltage_pre = -500;

    if(g_ocv_lookup_done == 0)
    {
        profile_p = fgauge_get_profile(TEMPERATURE_T);
        if(profile_p != NULL)
            memset(profile_p, 0, sizeof(battery_profile_t2));

        profile_p_r_table = fgauge_get_profile_r_table(TEMPERATURE_T);
        if(profile_p_r_table != NULL)
            memset(profile_p_r_table, 0, sizeof(r_profile_t2));
    }
}

kal_int32 fgauge_precharge_compensated_voltage(kal_int32 recursion_time)
{
    int i=0;
    kal_int32 bat_vol, bat_temp, bat_res, temp_voltage_1, temp_voltage_2;
    kal_int32 ret_compensate_value = 0, current_val = 0;
    BATTERY_PROFILE_STRUC_P profile_p;
    R_PROFILE_STRUC_P profile_p_r_table;

    bat_vol = fgauge_read_voltage_ncp1851();
    bat_temp = fgauge_read_temperature();

    // Re-constructure r-table profile according to current temperature
    profile_p_r_table = fgauge_get_profile_r_table(TEMPERATURE_T);
    fgauge_construct_r_table_profile(bat_temp, profile_p_r_table);

    // Re-constructure battery profile according to current temperature
    profile_p = fgauge_get_profile(TEMPERATURE_T);
    fgauge_construct_battery_profile(bat_temp, profile_p);

    current_val = fgauge_read_current();

    temp_voltage_1 = bat_vol;
    temp_voltage_2 = temp_voltage_1;

    for(i=0 ; i < recursion_time ; i++)
    {
        bat_res = fgauge_read_r_bat_by_v(temp_voltage_2); // Ohm
        ret_compensate_value = (current_val * (bat_res + R_FG_VALUE)) / 1000;
        ret_compensate_value = (ret_compensate_value+(10/2)) / 10; // 20101103

        if (gFG_Is_Charging == KAL_TRUE)
        {
            ret_compensate_value = ret_compensate_value - (ret_compensate_value*2);
        }
        temp_voltage_2 = temp_voltage_1 + ret_compensate_value;

        if (Enable_FGADC_LOG == 1)
        {
            xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[fgauge_precharge_compensated_voltage] %d,%d,%d,%d\r\n",
            temp_voltage_1, temp_voltage_2, bat_res, ret_compensate_value);
        }
    }

    bat_res = fgauge_read_r_bat_by_v(temp_voltage_2); // Ohm
    ret_compensate_value = (current_val * (bat_res + R_FG_VALUE + FG_METER_RESISTANCE)) / 1000;
    ret_compensate_value = (ret_compensate_value+(10/2)) / 10; // 20101103

    if (gFG_Is_Charging == KAL_TRUE)
    {
        ret_compensate_value = ret_compensate_value - (ret_compensate_value*2);
    }

    if (Enable_FGADC_LOG == 1)
    {
        xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[fgauge_precharge_compensated_voltage] %d,%d,%d,%d\r\n",
        temp_voltage_1, temp_voltage_2, bat_res, ret_compensate_value);
    }

    bat_vol = temp_voltage_1 + ret_compensate_value;

    return bat_vol;
}
#endif

void fgauge_initialization(void)
{
	//kal_uint16 Temp_Reg = 0;
	int i = 0;
	kal_uint32 ret=0;

	gFG_BATT_CAPACITY_init_high_current = Q_MAX_POS_25_H_CURRENT;
	//gFG_BATT_CAPACITY_aging = gFG_BATT_CAPACITY_init_high_current;
	gFG_BATT_CAPACITY_aging = Q_MAX_POS_25;

// 1. HW initialization
//FGADC clock is 32768Hz from RTC
	//Enable FGADC in current mode at 32768Hz with auto-calibration
	#if 0
	//write @ RG_VA2_EN (bank0, 0x0C8[0]) = 0x1
	ret=pmic_config_interface(0xC8, 0x1, 0xFF, 0x0);
	//write @ RG_FGADC_CK_PDN (bank1, 0x015[4]) = 0x0
	ret=pmic_bank1_config_interface(0x15, 0x0, 0xFF, 0x0);
	//write @ FG_VMODE (bank1, 0x069[1]) = 0x0
	upmu_fgadc_vmode(0x0);
	//write @ FG_CLKSRC (bank1, 0x069[7]) = 0x0
	upmu_fgadc_clksrc(0x0);
	//write @ FG_CAL (bank1, 0x069 [3:2]) = 0x2
	upmu_fgadc_cal(0x2);
	//write @ FG_ON (bank1, 0x069 [0]) = 0x1
	upmu_fgadc_on(0x1);
	#endif
	//(1)	i2c_write (0x60, 0xC8, 0x01); // Enable VA2
	ret=pmic_config_interface(0xC8, 0x1, 0xFF, 0x0);
	//(2)	i2c_write (0x61, 0x15, 0x00); // Enable FGADC clock for digital
	ret=pmic_bank1_config_interface(0x15, 0x0, 0xFF, 0x0);
	//(3)	i2c_write (0x61, 0x69, 0x28); // Set current mode, auto-calibration mode and 32KHz clock source
	ret=pmic_bank1_config_interface(0x69, 0x28, 0xFF, 0x0);
	//(4)	i2c_write (0x61, 0x69, 0x29); // Enable FGADC
	ret=pmic_bank1_config_interface(0x69, 0x29, 0xFF, 0x0);

       //reset HW FG
       ret=pmic_bank1_config_interface(0x6A, 0x71, 0xFF, 0x0);
       xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "******** [fgauge_initialization] reset HW FG!\n" );

// 2. SW algorithm initialization
#ifdef MTK_NCP1851_SUPPORT
    gFG_voltage = fgauge_read_voltage_ncp1851();
    //Tim, for TBAT
    upmu_auxadc_buf_pwd_b(0x1);   //RG_BUF_PWD_B
    upmu_auxadc_adc_pwd_b(0x1);
    upmu_chr_baton_tdet_en(1);
    msleep(50);
#else
    gFG_voltage = fgauge_read_voltage();
#endif

    //gFG_current = fgauge_read_current();
    i=0;
	while( gFG_current == 0 )
	{
		gFG_current = fgauge_read_current();
		if(i > 10)
			break;
		i++;
	}

	gFG_columb = fgauge_read_columb();
	gFG_temp = fgauge_read_temperature();
#ifdef MTK_NCP1851_SUPPORT
    if(g_ocv_lookup_done == 0)
    {
        gFG_capacity = fgauge_read_capacity(0);
        xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "g_ocv_loopup_done is 0, do fgauge_read_capacity(0)\n" );
    }
#else
    gFG_capacity = fgauge_read_capacity(0);
#endif


    gFG_columb_init = gFG_columb;
    gFG_capacity_by_c_init = gFG_capacity;
    gFG_capacity_by_c = gFG_capacity;
    gFG_capacity_by_v = gFG_capacity;
    gFG_pre_temp = gFG_temp;

    gFG_inner_R = (COMPASATE_OCV/1000) / gFG_current; // mOhm
    gFG_DOD0 = 100 - gFG_capacity;

	gFG_BATT_CAPACITY = fgauge_get_Q_max(gFG_temp);

	//FGADC_dump_register();

	xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "******** [fgauge_initialization] Done!\n" );

}

///////////////////////////////////////////////////////////////////////////////////////////
//// External API
///////////////////////////////////////////////////////////////////////////////////////////
kal_int32 FGADC_Get_BatteryCapacity_CoulombMothod(void)
{
	return gFG_capacity_by_c;
}

kal_int32 FGADC_Get_BatteryCapacity_VoltageMothod(void)
{
	return gfg_percent_check_point;
	//return gFG_capacity_by_v;
}

kal_int32 FGADC_Get_FG_Voltage(void)
{
	return gFG_voltageVBAT;
}

extern int g_Calibration_FG;

void FGADC_Reset_SW_Parameter(void)
{
	//volatile kal_uint16 Temp_Reg = 0;
	volatile kal_uint16 val_car = 1;
	kal_uint32 ret = 0;

#if 0
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] FGADC_Reset_SW_Parameter : Todo \r\n");
#else
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] FGADC_Reset_SW_Parameter : Start \r\n");
	gFG_SW_CoulombCounter = 0;
	while(val_car != 0x0)
	{
		ret=pmic_bank1_config_interface(0x6A, 0x71, 0xFF, 0x0);
		gFG_columb = fgauge_read_columb_reset();
		val_car = gFG_columb;
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "#");
	}
	gFG_columb = 0;
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] FGADC_Reset_SW_Parameter : Done \r\n");

	if(g_Calibration_FG==1)
	{
		gFG_DOD0 = 0;
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] FG Calibration DOD0=%d and DOD1=%d \r\n", gFG_DOD0, gFG_DOD1);
	}
	else
	{
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] Update DOD0(%d) by %d \r\n", gFG_DOD0, gFG_DOD1);
		gFG_DOD0 = gFG_DOD1;
	}
#endif

}

extern int g_switch_to_i2c_polling_mode;
kal_int32 g_car_instant=0;
kal_int32 g_current_instant=0;
kal_int32 g_car_sleep=0;
kal_int32 g_car_wakeup=0;
kal_int32 g_last_time=0;

kal_int32 get_dynamic_period(int first_use, int first_wakeup_time, int battery_capacity_level)
{
	kal_int32 ret_val=-1;
	int check_fglog=0;

#ifdef CONFIG_MTK_SMART_BATTERY
	kal_int32 I_sleep=0;
	kal_int32 new_time=0;

	if(gForceADCsolution==1)
	{
		return first_wakeup_time;
	}

	g_switch_to_i2c_polling_mode=1;
	check_fglog=Enable_FGADC_LOG;
	if(check_fglog==0)
	{
		//Enable_FGADC_LOG=1;
	}
	g_current_instant = fgauge_read_current();
	g_car_instant = fgauge_read_columb();
	g_switch_to_i2c_polling_mode=0;
	if(check_fglog==0)
	{
		Enable_FGADC_LOG=0;
	}
	if(g_car_instant < 0)
	{
		g_car_instant = g_car_instant - (g_car_instant*2);
	}

	if(first_use == 1)
	{
		//ret_val = 30*60; /* 30 mins */
		ret_val = first_wakeup_time;
		g_last_time = ret_val;
		g_car_sleep = g_car_instant;
	}
	else
	{
		g_car_wakeup = g_car_instant;

		if(g_last_time==0)
			g_last_time=1;

		if(g_car_sleep > g_car_wakeup)
		{
			g_car_sleep = g_car_wakeup;
			xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[get_dynamic_period] reset g_car_sleep\r\n");
		}

		I_sleep = ((g_car_wakeup-g_car_sleep)*3600)/g_last_time; // unit: second

		if(I_sleep==0)
		{
			g_switch_to_i2c_polling_mode=1;
			if(check_fglog==0)
			{
				//Enable_FGADC_LOG=1;
			}
			I_sleep = fgauge_read_current();
			I_sleep = I_sleep / 10;
			g_switch_to_i2c_polling_mode=0;
			if(check_fglog==0)
			{
				Enable_FGADC_LOG=0;
			}
		}

        if(I_sleep == 0)
        {
            new_time = first_wakeup_time;
        }
        else
        {
            new_time = ((gFG_BATT_CAPACITY*battery_capacity_level*3600)/100)/I_sleep;    
        }        
		ret_val = new_time;

		if(ret_val == 0)
			ret_val = first_wakeup_time;

		xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[get_dynamic_period] g_car_instant=%d, g_car_wakeup=%d, g_car_sleep=%d, I_sleep=%d, gFG_BATT_CAPACITY=%d, g_last_time=%d, new_time=%d\r\n", 
			g_car_instant, g_car_wakeup, g_car_sleep, I_sleep, gFG_BATT_CAPACITY, g_last_time, new_time);

		//update parameter
		g_car_sleep = g_car_wakeup;
		g_last_time = ret_val;
	}
#else
	xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[get_dynamic_period] no use\r\n");
#endif

	return ret_val;
}

///////////////////////////////////////////////////////////////////////////////////////////
//// Internal API
///////////////////////////////////////////////////////////////////////////////////////////
void fg_voltage_mode(void)
{
	if( upmu_is_chr_det()==KAL_TRUE )
	{
		/* SOC only UP when charging */
        if ( gFG_capacity_by_v > gfg_percent_check_point ) {
			gfg_percent_check_point++;
        }
	}
	else
	{
		/* SOC only Done when dis-charging */
        if ( gFG_capacity_by_v < gfg_percent_check_point ) {
			gfg_percent_check_point--;
        }
	}

	if (Enable_FGADC_LOG == 1)
	{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC_VoltageMothod] gFG_capacity_by_v=%ld,gfg_percent_check_point=%ld\r\n",
			gFG_capacity_by_v, gfg_percent_check_point);
	}

}

void FGADC_thread_kthread(void)
{
	int i=0;

    mutex_lock(&FGADC_mutex);

	fgauge_Normal_Mode_Work();

	if(volt_mode_update_timer >= volt_mode_update_time_out)
	{
		volt_mode_update_timer=0;

		fg_voltage_mode();
	}
	else
	{
		volt_mode_update_timer++;
	}

	//if (Enable_FGADC_LOG >= 1)
	//{
		xlog_printk(ANDROID_LOG_INFO, "Power/Battery", "[FGADC] %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n",
			gFG_Is_Charging,gFG_current,
		    gFG_SW_CoulombCounter,gFG_columb,gFG_voltage,gFG_capacity_by_v,gFG_capacity_by_c,gFG_capacity_by_c_init,
			gFG_BATT_CAPACITY,gFG_BATT_CAPACITY_aging,gFG_compensate_value,gFG_ori_voltage,OCV_BOARD_COMPESATE,R_FG_BOARD_SLOPE,
			ENABLE_SW_COULOMB_COUNTER,gFG_voltage_init,MinErrorOffset,gFG_DOD0,gFG_DOD1,current_get_ori,
			CAR_TUNE_VALUE,AGING_TUNING_VALUE);
	//}
	update_fg_dbg_tool_value();

	if(gFG_booting_counter_I >= MAX_BOOTING_TIME_FGCURRENT)
	{
		gFG_booting_counter_I = 0;
		gFG_booting_counter_I_FLAG = 1;
	}
	else
	{
		if(gFG_booting_counter_I_FLAG == 0)
		{
			gFG_booting_counter_I+=10;
			for(i=0;i<10;i++)
			{
				gFG_current_auto_detect_R_fg_total+= fgauge_read_current();
				gFG_current_auto_detect_R_fg_count++;
			}
//			Enable_FGADC_LOG = 0;
		}
	}

	mutex_unlock(&FGADC_mutex);
}

///////////////////////////////////////////////////////////////////////////////////////////
//// Logging System
///////////////////////////////////////////////////////////////////////////////////////////
static struct proc_dir_entry *proc_entry_fgadc;
static char proc_fgadc_data[32];

ssize_t fgadc_log_write( struct file *filp, const char __user *buff,
                        unsigned long len, void *data )
{
	if (copy_from_user( &proc_fgadc_data, buff, len )) {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "fgadc_log_write error.\n");
		return -EFAULT;
	}

	if (proc_fgadc_data[0] == '1') {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "enable FGADC driver log system\n");
		Enable_FGADC_LOG = 1;
	} else if (proc_fgadc_data[0] == '2') {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "enable FGADC driver log system:2\n");
		Enable_FGADC_LOG = 2;
	} else {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "Disable FGADC driver log system\n");
		Enable_FGADC_LOG = 0;
	}

	return len;
}

int init_proc_log_fg(void)
{
	int ret=0;
	proc_entry_fgadc = create_proc_entry( "fgadc_log", 0644, NULL );

	if (proc_entry_fgadc == NULL) {
		ret = -ENOMEM;
	  	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "init_proc_log_fg: Couldn't create proc entry\n");
	} else {
		proc_entry_fgadc->write_proc = fgadc_log_write;
		//proc_entry->owner = THIS_MODULE;
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "init_proc_log_fg loaded.\n");
	}

	return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////
//// Create File For Power Consumption Profile : FG_Current
///////////////////////////////////////////////////////////////////////////////////////////
kal_int32 gFG_current_inout_battery = 0;
static ssize_t show_FG_Current(struct device *dev,struct device_attribute *attr, char *buf)
{
	// Power Consumption Profile---------------------
	gFG_current = fgauge_read_current();
	if(gFG_Is_Charging==KAL_TRUE)
	{
		gFG_current_inout_battery = 0 - gFG_current;
	}
	else
	{
		gFG_current_inout_battery = gFG_current;
	}
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] %d\r\n", gFG_current_inout_battery);
	//-----------------------------------------------
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] gFG_current_inout_battery : %d\n", gFG_current_inout_battery);
	return sprintf(buf, "%d\n", gFG_current_inout_battery);
}
static ssize_t store_FG_Current(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_Current, 0664, show_FG_Current, store_FG_Current);

///////////////////////////////////////////////////////////////////////////////////////////
//// Create File For FG UI DEBUG
///////////////////////////////////////////////////////////////////////////////////////////
static ssize_t show_FG_g_fg_dbg_bat_volt(struct device *dev,struct device_attribute *attr, char *buf)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] g_fg_dbg_bat_volt : %d\n", g_fg_dbg_bat_volt);
	return sprintf(buf, "%d\n", g_fg_dbg_bat_volt);
}
static ssize_t store_FG_g_fg_dbg_bat_volt(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_g_fg_dbg_bat_volt, 0664, show_FG_g_fg_dbg_bat_volt, store_FG_g_fg_dbg_bat_volt);
//-------------------------------------------------------------------------------------------
static ssize_t show_FG_g_fg_dbg_bat_current(struct device *dev,struct device_attribute *attr, char *buf)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] g_fg_dbg_bat_current : %d\n", g_fg_dbg_bat_current);
	return sprintf(buf, "%d\n", g_fg_dbg_bat_current);
}
static ssize_t store_FG_g_fg_dbg_bat_current(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_g_fg_dbg_bat_current, 0664, show_FG_g_fg_dbg_bat_current, store_FG_g_fg_dbg_bat_current);
//-------------------------------------------------------------------------------------------
static ssize_t show_FG_g_fg_dbg_bat_zcv(struct device *dev,struct device_attribute *attr, char *buf)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] g_fg_dbg_bat_zcv : %d\n", g_fg_dbg_bat_zcv);
	return sprintf(buf, "%d\n", g_fg_dbg_bat_zcv);
}
static ssize_t store_FG_g_fg_dbg_bat_zcv(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_g_fg_dbg_bat_zcv, 0664, show_FG_g_fg_dbg_bat_zcv, store_FG_g_fg_dbg_bat_zcv);
//-------------------------------------------------------------------------------------------
static ssize_t show_FG_g_fg_dbg_bat_temp(struct device *dev,struct device_attribute *attr, char *buf)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] g_fg_dbg_bat_temp : %d\n", g_fg_dbg_bat_temp);
	return sprintf(buf, "%d\n", g_fg_dbg_bat_temp);
}
static ssize_t store_FG_g_fg_dbg_bat_temp(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_g_fg_dbg_bat_temp, 0664, show_FG_g_fg_dbg_bat_temp, store_FG_g_fg_dbg_bat_temp);
//-------------------------------------------------------------------------------------------
static ssize_t show_FG_g_fg_dbg_bat_r(struct device *dev,struct device_attribute *attr, char *buf)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] g_fg_dbg_bat_r : %d\n", g_fg_dbg_bat_r);
	return sprintf(buf, "%d\n", g_fg_dbg_bat_r);
}
static ssize_t store_FG_g_fg_dbg_bat_r(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_g_fg_dbg_bat_r, 0664, show_FG_g_fg_dbg_bat_r, store_FG_g_fg_dbg_bat_r);
//-------------------------------------------------------------------------------------------
static ssize_t show_FG_g_fg_dbg_bat_car(struct device *dev,struct device_attribute *attr, char *buf)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] g_fg_dbg_bat_car : %d\n", g_fg_dbg_bat_car);
	return sprintf(buf, "%d\n", g_fg_dbg_bat_car);
}
static ssize_t store_FG_g_fg_dbg_bat_car(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_g_fg_dbg_bat_car, 0664, show_FG_g_fg_dbg_bat_car, store_FG_g_fg_dbg_bat_car);
//-------------------------------------------------------------------------------------------
static ssize_t show_FG_g_fg_dbg_bat_qmax(struct device *dev,struct device_attribute *attr, char *buf)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] g_fg_dbg_bat_qmax : %d\n", g_fg_dbg_bat_qmax);
	return sprintf(buf, "%d\n", g_fg_dbg_bat_qmax);
}
static ssize_t store_FG_g_fg_dbg_bat_qmax(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_g_fg_dbg_bat_qmax, 0664, show_FG_g_fg_dbg_bat_qmax, store_FG_g_fg_dbg_bat_qmax);
//-------------------------------------------------------------------------------------------
static ssize_t show_FG_g_fg_dbg_d0(struct device *dev,struct device_attribute *attr, char *buf)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] g_fg_dbg_d0 : %d\n", g_fg_dbg_d0);
	return sprintf(buf, "%d\n", g_fg_dbg_d0);
}
static ssize_t store_FG_g_fg_dbg_d0(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_g_fg_dbg_d0, 0664, show_FG_g_fg_dbg_d0, store_FG_g_fg_dbg_d0);
//-------------------------------------------------------------------------------------------
static ssize_t show_FG_g_fg_dbg_d1(struct device *dev,struct device_attribute *attr, char *buf)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] g_fg_dbg_d1 : %d\n", g_fg_dbg_d1);
	return sprintf(buf, "%d\n", g_fg_dbg_d1);
}
static ssize_t store_FG_g_fg_dbg_d1(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_g_fg_dbg_d1, 0664, show_FG_g_fg_dbg_d1, store_FG_g_fg_dbg_d1);
//-------------------------------------------------------------------------------------------
static ssize_t show_FG_g_fg_dbg_percentage(struct device *dev,struct device_attribute *attr, char *buf)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] g_fg_dbg_percentage : %d\n", g_fg_dbg_percentage);
	return sprintf(buf, "%d\n", g_fg_dbg_percentage);
}
static ssize_t store_FG_g_fg_dbg_percentage(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_g_fg_dbg_percentage, 0664, show_FG_g_fg_dbg_percentage, store_FG_g_fg_dbg_percentage);
//-------------------------------------------------------------------------------------------
static ssize_t show_FG_g_fg_dbg_percentage_fg(struct device *dev,struct device_attribute *attr, char *buf)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] g_fg_dbg_percentage_fg : %d\n", g_fg_dbg_percentage_fg);
	return sprintf(buf, "%d\n", g_fg_dbg_percentage_fg);
}
static ssize_t store_FG_g_fg_dbg_percentage_fg(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_g_fg_dbg_percentage_fg, 0664, show_FG_g_fg_dbg_percentage_fg, store_FG_g_fg_dbg_percentage_fg);
//-------------------------------------------------------------------------------------------
static ssize_t show_FG_g_fg_dbg_percentage_voltmode(struct device *dev,struct device_attribute *attr, char *buf)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FG] g_fg_dbg_percentage_voltmode : %d\n", g_fg_dbg_percentage_voltmode);
	return sprintf(buf, "%d\n", g_fg_dbg_percentage_voltmode);
}
static ssize_t store_FG_g_fg_dbg_percentage_voltmode(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	return size;
}
static DEVICE_ATTR(FG_g_fg_dbg_percentage_voltmode, 0664, show_FG_g_fg_dbg_percentage_voltmode, store_FG_g_fg_dbg_percentage_voltmode);

///////////////////////////////////////////////////////////////////////////////////////////
//// platform_driver API
///////////////////////////////////////////////////////////////////////////////////////////
static int mt6577_fgadc_probe(struct platform_device *dev)
{
	int ret_device_file = 0;

#if defined(CONFIG_POWER_EXT)
//#if 0
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] INIT : EVB \n");
#else
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] mt6577 FGADC driver probe!! \n" );

	/* FG driver init */
	//fgauge_initialization();

	/*LOG System Set*/
	init_proc_log_fg();
#endif

	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_Current);

	//Create File For FG UI DEBUG
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_g_fg_dbg_bat_volt);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_g_fg_dbg_bat_current);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_g_fg_dbg_bat_zcv);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_g_fg_dbg_bat_temp);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_g_fg_dbg_bat_r);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_g_fg_dbg_bat_car);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_g_fg_dbg_bat_qmax);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_g_fg_dbg_d0);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_g_fg_dbg_d1);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_g_fg_dbg_percentage);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_g_fg_dbg_percentage_fg);
	ret_device_file = device_create_file(&(dev->dev), &dev_attr_FG_g_fg_dbg_percentage_voltmode);

	return 0;
}

static int mt6577_fgadc_remove(struct platform_device *dev)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] mt6577 FGADC driver remove!! \n" );

	return 0;
}

static void mt6577_fgadc_shutdown(struct platform_device *dev)
{
	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "[FGADC] mt6577 FGADC driver shutdown!! \n" );
}

kal_uint32 gRTC_time_suspend=0;
kal_uint32 gRTC_time_resume=0;
kal_uint32 gFG_capacity_before=0;
kal_uint32 gFG_capacity_after=0;
kal_uint32 gFG_RTC_time_MAX=3600; //60mins
//kal_uint32 gFG_RTC_time_MAX=60; //1mins

static int mt6577_fgadc_suspend(struct platform_device *dev, pm_message_t state)
{

#if defined(CONFIG_POWER_EXT)
//#if 0
	xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_suspend] EVB !!\n");

#else

	//struct rtc_device *rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	//struct rtc_time tm;
	//unsigned long time;

	xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_suspend] TODO !!\n");

	#if 0
	xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_suspend] MT6573 FGADC driver suspend!!\n");

	FGADC_Reset_SW_Parameter();
	//Turn Off FG
	MTKFG_PLL_Control(KAL_FALSE);

	rtc_read_time(rtc, &tm);
	rtc_tm_to_time(&tm, &time);
	gRTC_time_suspend=time;
	gFG_capacity_before=gFG_capacity_by_c;

	xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_suspend] gRTC_time_suspend=%d, gFG_capacity_before=%d\n",
		gRTC_time_suspend, gFG_capacity_before);
	#endif

#endif

	return 0;
}

static int mt6577_fgadc_resume(struct platform_device *dev)
{
#if defined(CONFIG_POWER_EXT)
//#if 0
	xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_RESUME] EVB !!\n");

#else

	xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_RESUME] TODO !!\n");

	#if 0
	kal_uint16 Temp_Reg = 0;
	int i=0;
	int index=1;
	kal_int32 FG_voltage_sum=0;

	struct rtc_device *rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	struct rtc_time tm;
	unsigned long time;

	//unsigned long RTC_BatteryPercent=0;
	kal_int32 temp_RTC=0;
	kal_int32 temp_FG=0;
	//kal_int32 temp_offset=0;
	kal_uint32 gRTC_time_offset=0;
	kal_int32 gFG_percent_resume=0;

	kal_uint16 val_car=0;

	if(get_chip_eco_ver()!=CHIP_E1)
	{
		xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_RESUME] MT6573 FGADC driver resume, gFG_DOD0=%d!!\n", gFG_DOD0);

		//Turn On FG
		MTKFG_PLL_Control(KAL_TRUE);

		rtc_read_time(rtc, &tm);
		rtc_tm_to_time(&tm, &time);
		gRTC_time_resume=time;

		xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_RESUME] FGADC_Reset some SW_Parameter \r\n");
		gFG_columb = 0;
		gFG_SW_CoulombCounter = 0;
		Temp_Reg = 0x18F0;
    	OUTREG16(FGADC_CON0, Temp_Reg);
		val_car = FG_DRV_ReadReg16(FGADC_CON1);
		#if 0
		val_car = FG_DRV_ReadReg16(FGADC_CON1);
		while(val_car!=0x0)
		{
			OUTREG16(FGADC_CON0, Temp_Reg);
			xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", ".");
			val_car = FG_DRV_ReadReg16(FGADC_CON1);
		}
		#endif

		gFGvbatBufferFirst = KAL_FALSE;

		gRTC_time_offset=(gRTC_time_resume-gRTC_time_suspend);

		if(gRTC_time_offset > gFG_RTC_time_MAX)
		{
			// Use AUXADC to update DOD
			//gFG_voltage = fgauge_read_voltage();
			for(i=0 ; i<index ; i++)
			{
				FG_voltage_sum += fgauge_read_voltage();
			}
			gFG_voltage = (FG_voltage_sum/index);

			gFG_current = fgauge_read_current();
			gFG_voltage = gFG_voltage + fgauge_compensate_battery_voltage_recursion(gFG_voltage,5); //mV
			gFG_voltage = gFG_voltage + OCV_BOARD_COMPESATE;
			gFG_capacity_by_v = fgauge_read_capacity_by_v();
			gFG_capacity_by_c=gFG_capacity_by_v;
			gEstBatCapacity = gFG_capacity_by_c;
			gFG_DOD0 = 100 - gFG_capacity_by_v;

			xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_RESUME] %d,%d,%d,%d\n", gFG_current, gFG_voltage, gFG_capacity_by_v, bat_volt_check_point);

			gFG_percent_resume=100-gFG_DOD0;
			if(gFG_percent_resume > bat_volt_check_point)
			{
				//restore
				gFG_capacity_by_v = bat_volt_check_point;
				gFG_capacity_by_c=gFG_capacity_by_v;
				gEstBatCapacity = gFG_capacity_by_c;
				gFG_DOD0 = 100 - gFG_capacity_by_v;
				gFG_DOD1 = gFG_DOD0;
			}

			xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_RESUME] Sleep Time(%d) > %d, gFG_capacity_by_v=%d, bat_volt_check_point=%d, gFG_DOD0=%d, gFG_DOD1=%d, gFG_percent_resume=%d\n",
				gRTC_time_offset, gFG_RTC_time_MAX, gFG_capacity_by_v, bat_volt_check_point, gFG_DOD0, gFG_DOD1, gFG_percent_resume);
		}
		else
		{
			xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_RESUME] Need update DOD0(%d) by bat_volt_check_point(%d)\n",
				gFG_DOD0, bat_volt_check_point);
			//restore
			gFG_capacity_by_v = bat_volt_check_point;
			gFG_capacity_by_c=gFG_capacity_by_v;
			gEstBatCapacity = gFG_capacity_by_c;
			gFG_DOD0 = 100 - gFG_capacity_by_v;
			gFG_DOD1 = gFG_DOD0;

			xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_RESUME] Sleep Time(%d) <= %d, gFG_capacity_by_v=%d, bat_volt_check_point=%d, gFG_DOD0=%d, gFG_DOD1=%d\n",
				gRTC_time_offset, gFG_RTC_time_MAX, gFG_capacity_by_v, bat_volt_check_point, gFG_DOD0, gFG_DOD1);
		}

		xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_RESUME] gRTC_time_suspend=%d, gRTC_time_resume=%d, gFG_capacity_before=%d, gFG_capacity_after=%d, temp_RTC=%d, temp_FG=%d\n",
			gRTC_time_suspend, gRTC_time_resume, gFG_capacity_before, gFG_capacity_after, temp_RTC, temp_FG);

		xlog_printk(ANDROID_LOG_VERBOSE, "Power/Battery", "[FGADC_RESUME] %d,%d,%d,%d,%d,gFG_temp=%d,gFG_DOD1_return=%d,val_car=%d\n",
			gFG_current, gFG_voltage, gFG_capacity_by_v, FG_voltage_sum, index, gFG_temp, gFG_DOD1_return,val_car);
	}
	#endif

#endif

	return 0;
}

struct platform_device mt6577_fgadc_device = {
		.name				= "mt6577-fgadc",
		.id					= -1,
};

static struct platform_driver mt6577_fgadc_driver = {
	.probe		= mt6577_fgadc_probe,
	.remove		= mt6577_fgadc_remove,
	.shutdown	= mt6577_fgadc_shutdown,
	//#ifdef CONFIG_PM
	.suspend	= mt6577_fgadc_suspend,
	.resume		= mt6577_fgadc_resume,
	//#endif
	.driver         = {
        .name = "mt6577-fgadc",
    },
};

static int __init mt6577_fgadc_init(void)
{
	int ret;

	ret = platform_device_register(&mt6577_fgadc_device);
	if (ret) {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "****[mt6577_fgadc_driver] Unable to device register(%d)\n", ret);
		return ret;
	}

	ret = platform_driver_register(&mt6577_fgadc_driver);
	if (ret) {
		xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "****[mt6577_fgadc_driver] Unable to register driver (%d)\n", ret);
		return ret;
	}

	xlog_printk(ANDROID_LOG_DEBUG, "Power/Battery", "****[mt6577_fgadc_driver] Initialization : DONE \n");

	return 0;

}

static void __exit mt6577_fgadc_exit (void)
{
}

module_init(mt6577_fgadc_init);
module_exit(mt6577_fgadc_exit);

MODULE_AUTHOR("James Lo");
MODULE_DESCRIPTION("mt6577 FGADC Device Driver");
MODULE_LICENSE("GPL");
