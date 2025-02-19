/*
  xdrv_03_esp32_energy.ino - Energy sensor support for Tasmota

  Copyright (C) 2023  Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef ESP32
#ifdef USE_ENERGY_SENSOR
//#define USE_ENERGY_SENSOR_ESP32
#endif  // USE_ENERGY_SENSOR
#endif  // ESP32

#ifdef USE_ENERGY_SENSOR_ESP32
/*********************************************************************************************\
 * Energy for ESP32 with max eight phases/channels using more RAM and Settings from filesystem
\*********************************************************************************************/
//#warning **** USE_ENERGY_SENSOR_ESP32 ****

#define XDRV_03                   3
#define XSNS_03                   3

#define ENERGY_NONE               0
#define ENERGY_WATCHDOG           4        // Allow up to 4 seconds before deciding no valid data present

#undef ENERGY_MAX_PHASES
#define ENERGY_MAX_PHASES         8        // Support max eight phases/channels

#include <Ticker.h>

#define D_CMND_POWERCAL "PowerCal"
#define D_CMND_VOLTAGECAL "VoltageCal"
#define D_CMND_CURRENTCAL "CurrentCal"
#define D_CMND_FREQUENCYCAL "FrequencyCal"
#define D_CMND_TARIFF "Tariff"
#define D_CMND_MODULEADDRESS "ModuleAddress"

enum EnergyCalibration {
  ENERGY_POWER_CALIBRATION, ENERGY_VOLTAGE_CALIBRATION, ENERGY_CURRENT_CALIBRATION, ENERGY_FREQUENCY_CALIBRATION };

enum EnergyCommands {
  CMND_POWERCAL, CMND_VOLTAGECAL, CMND_CURRENTCAL, CMND_FREQUENCYCAL,
  CMND_POWERSET, CMND_VOLTAGESET, CMND_CURRENTSET, CMND_FREQUENCYSET, CMND_MODULEADDRESS, CMND_ENERGYCONFIG };

const char kEnergyCommands[] PROGMEM = "|"  // No prefix
  D_CMND_POWERCAL "|" D_CMND_VOLTAGECAL "|" D_CMND_CURRENTCAL "|" D_CMND_FREQUENCYCAL "|"
  D_CMND_POWERSET "|" D_CMND_VOLTAGESET "|" D_CMND_CURRENTSET "|" D_CMND_FREQUENCYSET "|" D_CMND_MODULEADDRESS "|" D_CMND_ENERGYCONFIG "|"
  D_CMND_POWERDELTA "|" D_CMND_POWERLOW "|" D_CMND_POWERHIGH "|" D_CMND_VOLTAGELOW "|" D_CMND_VOLTAGEHIGH "|" D_CMND_CURRENTLOW "|" D_CMND_CURRENTHIGH "|"
  D_CMND_MAXENERGY "|" D_CMND_MAXENERGYSTART "|"
  D_CMND_MAXPOWER "|" D_CMND_MAXPOWERHOLD "|" D_CMND_MAXPOWERWINDOW "|"
  D_CMND_SAFEPOWER "|" D_CMND_SAFEPOWERHOLD "|"  D_CMND_SAFEPOWERWINDOW "|"
  D_CMND_ENERGYTODAY "|" D_CMND_ENERGYYESTERDAY "|" D_CMND_ENERGYTOTAL "|" D_CMND_ENERGYEXPORTACTIVE "|" D_CMND_ENERGYUSAGE "|" D_CMND_ENERGYEXPORT "|" D_CMND_TARIFF;

void (* const EnergyCommand[])(void) PROGMEM = {
  &CmndPowerCal, &CmndVoltageCal, &CmndCurrentCal, &CmndFrequencyCal,
  &CmndPowerSet, &CmndVoltageSet, &CmndCurrentSet, &CmndFrequencySet, &CmndModuleAddress, &CmndEnergyConfig,
  &CmndPowerDelta, &CmndPowerLow, &CmndPowerHigh, &CmndVoltageLow, &CmndVoltageHigh, &CmndCurrentLow, &CmndCurrentHigh,
  &CmndMaxEnergy, &CmndMaxEnergyStart,
  &CmndMaxPower, &CmndMaxPowerHold, &CmndMaxPowerWindow,
  &CmndSafePower, &CmndSafePowerHold, &CmndSafePowerWindow,
  &CmndEnergyToday, &CmndEnergyYesterday, &CmndEnergyTotal, &CmndEnergyExportActive, &CmndEnergyUsage, &CmndEnergyExport, &CmndTariff};

/********************************************************************************************/

typedef struct {
  float usage_total_kWh[4];
  float return_total_kWh[4];
  float last_return_total_kWh;
  float last_usage_total_kWh;
} tEnergyUsage;

typedef struct {
  uint32_t crc32;                                            // To detect file changes
  uint16_t version;                                          // To detect driver function changes
  uint16_t energy_kWhdoy;
  uint32_t energy_kWhtotal_time;
  uint32_t spare1;
  uint32_t spare2;
  uint32_t spare3;
  uint32_t spare4;
  uint32_t spare5;

  uint32_t power_calibration[ENERGY_MAX_PHASES];
  uint32_t voltage_calibration[ENERGY_MAX_PHASES];
  uint32_t current_calibration[ENERGY_MAX_PHASES];
  uint32_t frequency_calibration[ENERGY_MAX_PHASES];

  float energy_today_kWh[ENERGY_MAX_PHASES];          // Energy today in kWh - float allows up to 262143.99 kWh
  float energy_yesterday_kWh[ENERGY_MAX_PHASES];      // Energy yesterday in kWh - float allows up to 262143.99 kWh
  float energy_total_kWh[ENERGY_MAX_PHASES];          // Total energy in kWh - float allows up to 262143.99 kWh
  float energy_export_kWh[ENERGY_MAX_PHASES];         // Export energy in kWh - float allows up to 262143.99 kWh

  uint16_t power_delta[ENERGY_MAX_PHASES];            // PowerDelta

  uint16_t tariff[4][2];
  tEnergyUsage energy_usage;
} tEnergySettings;

typedef struct {
  tEnergySettings Settings;

  // Global updated / accessed
  float voltage[ENERGY_MAX_PHASES];             // 123.1 V
  float current[ENERGY_MAX_PHASES];             // 123.123 A
  float active_power[ENERGY_MAX_PHASES];        // 123.1 W
  float apparent_power[ENERGY_MAX_PHASES];      // 123.1 VA
  float reactive_power[ENERGY_MAX_PHASES];      // 123.1 VAr
  float power_factor[ENERGY_MAX_PHASES];        // 0.12
  float frequency[ENERGY_MAX_PHASES];           // 123.1 Hz
  float import_active[ENERGY_MAX_PHASES];       // 123.123 kWh
  float export_active[ENERGY_MAX_PHASES];       // 123.123 kWh
  float start_energy[ENERGY_MAX_PHASES];        // 12345.12345 kWh total previous
  float total[ENERGY_MAX_PHASES];               // 12345.12345 kWh total energy
  float daily_sum;                              // 123.123 kWh
  float total_sum;                              // 12345.12345 kWh total energy
  float yesterday_sum;                          // 123.123 kWh

  int32_t kWhtoday_delta[ENERGY_MAX_PHASES];    // 1212312345 Wh 10^-5 (deca micro Watt hours) - Overflows to Energy->kWhtoday (HLW and CSE only)
  int32_t kWhtoday[ENERGY_MAX_PHASES];          // 12312312 Wh * 10^-2 (deca milli Watt hours) - 5764 = 0.05764 kWh = 0.058 kWh = Energy->daily

  // Local only
  float daily_kWh[ENERGY_MAX_PHASES];           // 123.123 kWh
  float energy_today_offset_kWh[ENERGY_MAX_PHASES];  // 12312312 Wh * 10^-2 (deca milli Watt hours) - 5764 = 0.05764 kWh = 0.058 kWh = Energy->daily
  float period_kWh[ENERGY_MAX_PHASES];          // 12312312 Wh * 10^-2 (deca milli Watt hours) - 5764 = 0.05764 kWh = 0.058 kWh = Energy->daily
  float daily_sum_import_balanced;              // 123.123 kWh
  float daily_sum_export_balanced;              // 123.123 kWh

  uint8_t fifth_second;
  uint8_t command_code;
  uint8_t data_valid[ENERGY_MAX_PHASES];

  uint8_t phase_count;                          // Number of phases active
  bool voltage_common;                          // Use common voltage
  bool frequency_common;                        // Use common frequency
  bool use_overtemp;                            // Use global temperature as overtemp trigger on internal energy monitor hardware
  bool kWhtoday_offset_init;

  bool voltage_available;                       // Enable if voltage is measured
  bool current_available;                       // Enable if current is measured
  bool local_energy_active_export;              // Enable if support for storing energy_active

  bool type_dc;
  bool power_on;

  uint16_t power_history[ENERGY_MAX_PHASES][3];
  uint8_t power_steady_counter;                 // Allow for power on stabilization
  bool min_power_flag;
  bool max_power_flag;
  bool min_voltage_flag;
  bool max_voltage_flag;
  bool min_current_flag;
  bool max_current_flag;

  uint16_t mplh_counter;
  uint16_t mplw_counter;
  uint8_t mplr_counter;
  uint8_t max_energy_state;
} tEnergy;

tEnergy *Energy = nullptr;

Ticker ticker_energy;

/*********************************************************************************************\
 * RTC Energy memory
\*********************************************************************************************/

const uint16_t RTC_ENERGY_MEM_VALID = 0xA55A;

typedef struct {
  uint16_t valid;
  tEnergyUsage energy_usage;
  float energy_today_kWh[ENERGY_MAX_PHASES];
  float energy_total_kWh[ENERGY_MAX_PHASES];
  float energy_export_kWh[ENERGY_MAX_PHASES];
} tRtcEnergySettings;
tRtcEnergySettings RtcEnergySettings;
static RTC_NOINIT_ATTR tRtcEnergySettings RtcDataEnergySettings;

uint32_t energy_rtc_settings_crc = 0;

uint32_t EnergyGetRtcSettingsCrc(void) {
  uint32_t crc = 0;
  uint8_t *bytes = (uint8_t*)&RtcEnergySettings;

  for (uint32_t i = 0; i < sizeof(RtcEnergySettings); i++) {
    crc += bytes[i]*(i+1);
  }
  return crc;
}

void EnergyRtcSettingsSave(void) {
  if (EnergyGetRtcSettingsCrc() != energy_rtc_settings_crc) {

    if (RTC_ENERGY_MEM_VALID != RtcEnergySettings.valid) {
      memset(&RtcEnergySettings, 0, sizeof(RtcEnergySettings));
      RtcEnergySettings.valid = RTC_ENERGY_MEM_VALID;
      RtcEnergySettings.energy_usage = Energy->Settings.energy_usage;
      for (uint32_t i = 0; i < ENERGY_MAX_PHASES; i++) {
        RtcEnergySettings.energy_today_kWh[i] = Energy->Settings.energy_today_kWh[i];
        RtcEnergySettings.energy_total_kWh[i] = Energy->Settings.energy_total_kWh[i];
        RtcEnergySettings.energy_export_kWh[i] = Energy->Settings.energy_export_kWh[i];
      }
    }

//    AddLog(LOG_LEVEL_INFO, PSTR("DBG: energy_today_kWh[0] %3_f/%3_f"), RtcEnergySettings.energy_today_kWh[0], Energy->Settings.energy_today_kWh[0]);

    RtcDataEnergySettings = RtcEnergySettings;
    energy_rtc_settings_crc = EnergyGetRtcSettingsCrc();
  }
}

bool EnergyRtcSettingsLoad(void) {
  RtcEnergySettings = RtcDataEnergySettings;

  bool read_valid = (RTC_ENERGY_MEM_VALID == RtcEnergySettings.valid);
  if (!read_valid) {
    EnergyRtcSettingsSave();
  }
  return read_valid;
}

bool EnergyRtcSettingsValid(void) {
  return (RTC_ENERGY_MEM_VALID == RtcEnergySettings.valid);
}

/*********************************************************************************************\
 * Driver Settings load and save using filesystem
\*********************************************************************************************/

const uint32_t XDRV_03_VERSION = 0x0101;              // Latest driver version (See settings deltas below)

void EnergySettingsLoad(void) {
  // *** Start init default values in case file is not found ***
  memset(&Energy->Settings, 0x00, sizeof(tEnergySettings));
  Energy->Settings.version = XDRV_03_VERSION;
  // Init any other parameter in struct
  for (uint32_t i = 0; i < ENERGY_MAX_PHASES; i++) {
    Energy->Settings.power_calibration[i] = Settings->energy_power_calibration;
    Energy->Settings.voltage_calibration[i] = Settings->energy_voltage_calibration;;
    Energy->Settings.current_calibration[i] = Settings->energy_current_calibration;;
    Energy->Settings.frequency_calibration[i] = Settings->energy_frequency_calibration;
  }
  Energy->Settings.power_calibration[1] = Settings->energy_power_calibration2;
  Energy->Settings.voltage_calibration[1] = Settings->energy_voltage_calibration2;
  Energy->Settings.current_calibration[1] = Settings->energy_current_calibration2;
/*
  RtcEnergySettings.energy_total_kWh[0] = 0;
  RtcEnergySettings.energy_total_kWh[1] = 0;
  RtcEnergySettings.energy_total_kWh[2] = 0;
  memset((char*)&RtcEnergySettings.energy_usage, 0x00, sizeof(RtcEnergySettings.energy_usage));
*/
  for (uint32_t i = 0; i < 3; i++) {
    Energy->Settings.energy_today_kWh[i] = (float)(Settings->energy_kWhtoday_ph[i]) / 10000;
    Energy->Settings.energy_yesterday_kWh[i] = (float)(Settings->energy_kWhyesterday_ph[i]) / 10000;
    Energy->Settings.energy_total_kWh[i] = (float)(Settings->energy_kWhtotal_ph[i]) / 10000;
    Energy->Settings.energy_export_kWh[i] = (float)(Settings->energy_kWhexport_ph[i]) / 10000;

    Energy->Settings.power_delta[i] = (float)(Settings->energy_power_delta[i]);
  }
  // *** End Init default values ***

#ifndef USE_UFILESYS
  AddLog(LOG_LEVEL_DEBUG, PSTR("CFG: Energy use defaults as file system not enabled"));
#else
  // Try to load file /.drvset003
  char filename[20];
  // Use for drivers:
  snprintf_P(filename, sizeof(filename), PSTR(TASM_FILE_DRIVER), XDRV_03);
  if (TfsLoadFile(filename, (uint8_t*)&Energy->Settings, sizeof(tEnergySettings))) {
    if (Energy->Settings.version != XDRV_03_VERSION) {      // Fix version dependent changes

      // *** Start fix possible setting deltas ***

      // *** End setting deltas ***

      // Set current version and save settings
      Energy->Settings.version = XDRV_03_VERSION;
      EnergySettingsSave();
    }
    AddLog(LOG_LEVEL_INFO, PSTR("CFG: Energy loaded from file"));
  } else {
    // File system not ready: No flash space reserved for file system
    AddLog(LOG_LEVEL_DEBUG, PSTR("CFG: Energy use defaults as file system not ready or file not found"));
  }
#endif  // USE_UFILESYS
}

void EnergySettingsSave(void) {
#ifdef USE_UFILESYS
  // Called from FUNC_SAVE_SETTINGS every SaveData second and at restart
  uint32_t crc32 = GetCfgCrc32((uint8_t*)&Energy->Settings +4, sizeof(tEnergySettings) -4);  // Skip crc32
  if (crc32 != Energy->Settings.crc32) {
    // Try to save file /.drvset003
    Energy->Settings.crc32 = crc32;

    char filename[20];
    // Use for drivers:
    snprintf_P(filename, sizeof(filename), PSTR(TASM_FILE_DRIVER), XDRV_03);
    if (TfsSaveFile(filename, (const uint8_t*)&Energy->Settings, sizeof(tEnergySettings))) {
      AddLog(LOG_LEVEL_DEBUG, PSTR("CFG: Energy saved to file"));
    } else {
      // File system not ready: No flash space reserved for file system
      AddLog(LOG_LEVEL_DEBUG, PSTR("CFG: ERROR Energy file system not ready or unable to save file"));
    }
  }
#endif  // USE_UFILESYS
}

/********************************************************************************************/

char* EnergyFormat(char* result, float* input, uint32_t resolution, uint32_t single = 0);
char* EnergyFormat(char* result, float* input, uint32_t resolution, uint32_t single) {
  // single = 0 - Energy->phase_count - xx or [xx,xx] or [xx,xx,xx]
  // single = 1 - Energy->voltage_common or Energy->frequency_common - xx
  // single = 2 - Sum of Energy->phase_count if SO129 0 - xx or if SO129 1 - [xx,xx,xx]
  // single = 5 - single &0x03 = 1 - xx
  // single = 6 - single &0x03 = 2 - [xx,xx] - used by tarriff
  // single = 7 - single &0x03 = 3 - [xx,xx,xx]
  uint32_t index = (single > 3) ? single &0x03 : (0 == single) ? Energy->phase_count : 1;  // 1,2,3
  if (single > 2) { single = 0; }                        // 0,1,2
  float input_sum = 0.0f;
  if (single > 1) {
    if (!Settings->flag5.energy_phase) {                 // SetOption129 - (Energy) Show phase information
      for (uint32_t i = 0; i < Energy->phase_count; i++) {
        if (!isnan(input[i])) {
          input_sum += input[i];
        }
      }
      input = &input_sum;
    } else {
      index = Energy->phase_count;
    }
  }
  result[0] = '\0';
  for (uint32_t i = 0; i < index; i++) {
    ext_snprintf_P(result, TOPSZ, PSTR("%s%s%*_f%s"), result, (0==i)?(1==index)?"":"[":",", resolution, &input[i], (index-1==i)?(1==index)?"":"]":"");
  }
  return result;
}

#ifdef USE_WEBSERVER
char* WebEnergyFormat(char* result, float* input, uint32_t resolution, uint32_t single = 0);
char* WebEnergyFormat(char* result, float* input, uint32_t resolution, uint32_t single) {
  // single = 0 - Energy->phase_count - xx / xx / xx or multi column
  // single = 1 - Energy->voltage_common or Energy->frequency_common - xx or single column using colspan (if needed)
  // single = 2 - Sum of Energy->phase_count if SO129 0 - xx or single column using colspan (if needed) or if SO129 1 - xx / xx / xx or multi column
  float input_sum = 0.0f;
  if (single > 1) {                                      // Sum and/or Single column
    if (!Settings->flag5.energy_phase) {                 // SetOption129 - (Energy) Show phase information
      for (uint32_t i = 0; i < Energy->phase_count; i++) {
        if (!isnan(input[i])) {
          input_sum += input[i];
        }
      }
      input = &input_sum;
    } else {
      single = 0;
    }
  }
#ifdef USE_ENERGY_COLUMN_GUI
  ext_snprintf_P(result, GUISZ, PSTR("</td>"));       // Skip first column
  if ((Energy->phase_count > 1) && single) {              // Need to set colspan so need new columns
    // </td><td colspan='3' style='text-align:right'>1.23</td><td>&nbsp;</td><td>
    // </td><td colspan='5' style='text-align:right'>1.23</td><td>&nbsp;</td><td>
    // </td><td colspan='7' style='text-align:right'>1.23</td><td>&nbsp;</td><td>
    ext_snprintf_P(result, GUISZ, PSTR("%s<td colspan='%d' style='text-align:%s'>%*_f</td><td>&nbsp;</td>"),
      result, (Energy->phase_count *2) -1, (Settings->flag5.gui_table_align)?PSTR("right"):PSTR("center"), resolution, &input[0]);
  } else {
    // </td><td style='text-align:right'>1.23</td><td>&nbsp;</td><td>
    // </td><td style='text-align:right'>1.23</td><td>&nbsp;</td><td style='text-align:right'>1.23</td><td>&nbsp;</td><td>
    // </td><td style='text-align:right'>1.23</td><td>&nbsp;</td><td style='text-align:right'>1.23</td><td>&nbsp;</td><td style='text-align:right'>1.23</td><td>&nbsp;</td><td>
    // </td><td style='text-align:right'>1.23</td><td>&nbsp;</td><td style='text-align:right'>1.23</td><td>&nbsp;</td><td style='text-align:right'>1.23</td><td>&nbsp;</td><td style='text-align:right'>1.23</td><td>&nbsp;</td><td>
    for (uint32_t i = 0; i < Energy->phase_count; i++) {
      ext_snprintf_P(result, GUISZ, PSTR("%s<td style='text-align:%s'>%*_f</td><td>&nbsp;</td>"),
        result, (Settings->flag5.gui_table_align)?PSTR("right"):PSTR("left"), resolution, &input[i]);
    }
  }
  ext_snprintf_P(result, GUISZ, PSTR("%s<td>"), result);
#else  // not USE_ENERGY_COLUMN_GUI
  uint32_t index = (single) ? 1 : Energy->phase_count;    // 1,2,3
  result[0] = '\0';
  for (uint32_t i = 0; i < index; i++) {
    ext_snprintf_P(result, GUISZ, PSTR("%s%s%*_f"), result, (i)?" / ":"", resolution, &input[i]);
  }
#endif  // USE_ENERGY_COLUMN_GUI
  return result;
}
#endif  // USE_WEBSERVER

/********************************************************************************************/

bool EnergyTariff1Active() { // Off-Peak hours
  uint8_t dst = 0;
  if (IsDst() && (Energy->Settings.tariff[0][1] != Energy->Settings.tariff[1][1])) {
    dst = 1;
  }
  if (Energy->Settings.tariff[0][dst] != Energy->Settings.tariff[1][dst]) {
    if (Settings->flag3.energy_weekend && ((RtcTime.day_of_week == 1) ||   // CMND_TARIFF
                                          (RtcTime.day_of_week == 7))) {
      return true;
    }
    uint32_t minutes = MinutesPastMidnight();
    if (Energy->Settings.tariff[0][dst] > Energy->Settings.tariff[1][dst]) {
      // {"Tariff":{"Off-Peak":{"STD":"22:00","DST":"23:00"},"Standard":{"STD":"06:00","DST":"07:00"},"Weekend":"OFF"}}
      return ((minutes >= Energy->Settings.tariff[0][dst]) || (minutes < Energy->Settings.tariff[1][dst]));
    } else {
      // {"Tariff":{"Off-Peak":{"STD":"00:29","DST":"01:29"},"Standard":{"STD":"07:29","DST":"08:29"},"Weekend":"OFF"}}
      return ((minutes >= Energy->Settings.tariff[0][dst]) && (minutes < Energy->Settings.tariff[1][dst]));
    }
  } else {
    return false;
  }
}

void EnergyUpdateToday(void) {
  // Energy->kWhtoday_delta[]: int32_t x = 0.0000x kWh change
  // Energy->kWhtoday[]      : int32_t y = 0.0000y kWh

  Energy->total_sum = 0.0f;
  Energy->yesterday_sum = 0.0f;
  Energy->daily_sum = 0.0f;
  int32_t delta_sum_balanced = 0;

  for (uint32_t i = 0; i < Energy->phase_count; i++) {
    if (abs(Energy->kWhtoday_delta[i]) > 1000) {
      int32_t delta = Energy->kWhtoday_delta[i] / 1000;
      delta_sum_balanced += delta;
      Energy->kWhtoday_delta[i] -= (delta * 1000);
      Energy->kWhtoday[i] += delta;
      if (delta < 0) {     // Export energy
        RtcEnergySettings.energy_export_kWh[i] += ((float)(delta / 100) *-1) / 1000;
      }
    }

    RtcEnergySettings.energy_today_kWh[i] = Energy->energy_today_offset_kWh[i] + ((float)(Energy->kWhtoday[i]) / 100000);
    Energy->daily_kWh[i] = RtcEnergySettings.energy_today_kWh[i];
    Energy->total[i] = RtcEnergySettings.energy_total_kWh[i] + RtcEnergySettings.energy_today_kWh[i];
    if (Energy->local_energy_active_export) {
      Energy->export_active[i] = RtcEnergySettings.energy_export_kWh[i];
    }

    Energy->total_sum += Energy->total[i];
    Energy->yesterday_sum += Energy->Settings.energy_yesterday_kWh[i];
    Energy->daily_sum += Energy->daily_kWh[i];
  }

  if (delta_sum_balanced > 0) {
    Energy->daily_sum_import_balanced += (float)delta_sum_balanced / 100000;
  } else {
    Energy->daily_sum_export_balanced += (float)abs(delta_sum_balanced) / 100000;
  }

  if (RtcTime.valid){ // We calc the difference only if we have a valid RTC time.

    float energy_diff = Energy->total_sum - RtcEnergySettings.energy_usage.last_usage_total_kWh;
    RtcEnergySettings.energy_usage.last_usage_total_kWh = Energy->total_sum;

    float return_diff = 0;
    if (!isnan(Energy->export_active[0])) {
//      return_diff = Energy->export_active - RtcEnergySettings.energy_usage.last_return_total_kWh;
//      RtcEnergySettings.energy_usage.last_return_total_kWh = Energy->export_active;

      float export_active = 0.0f;
      for (uint32_t i = 0; i < Energy->phase_count; i++) {
        if (!isnan(Energy->export_active[i])) {
          export_active += Energy->export_active[i];
        }
      }
      return_diff = export_active - RtcEnergySettings.energy_usage.last_return_total_kWh;
      RtcEnergySettings.energy_usage.last_return_total_kWh = export_active;
    }

    uint32_t index = (EnergyTariff1Active()) ? 0 : 1;  // Tarrif1 = Off-Peak
    RtcEnergySettings.energy_usage.usage_total_kWh[index] += energy_diff;
    RtcEnergySettings.energy_usage.return_total_kWh[index] += return_diff;
  }
}

void EnergyUpdateTotal(void) {
  // Provide total import active energy as float Energy->import_active[phase] in kWh: 98Wh = 0.098kWh

  for (uint32_t i = 0; i < Energy->phase_count; i++) {
    AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("NRG: EnergyTotal[%d] %4_f kWh"), i, &Energy->import_active[i]);

    // Try to fix instable input by verifying allowed bandwidth (#17659)
    if ((Energy->start_energy[i] != 0) &&
        (Settings->param[P_CSE7766_INVALID_POWER] > 0) &&
        (Settings->param[P_CSE7766_INVALID_POWER] < 128)) {        // SetOption39 1..127 kWh
      int total = abs((int)Energy->total[i]);                      // We only use kWh
      int import_active = abs((int)Energy->import_active[i]);
      if ((import_active < (total - Settings->param[P_CSE7766_INVALID_POWER])) ||
          (import_active > (total + Settings->param[P_CSE7766_INVALID_POWER]))) {
        AddLog(LOG_LEVEL_DEBUG_MORE, PSTR("NRG: Outside bandwidth"));
        continue;                                                  // No valid energy value received
      }
    }

    if (0 == Energy->start_energy[i] || (Energy->import_active[i] < Energy->start_energy[i])) {
      Energy->start_energy[i] = Energy->import_active[i];            // Init after restart and handle roll-over if any
    }
    else if (Energy->import_active[i] != Energy->start_energy[i]) {
      Energy->kWhtoday[i] = (int32_t)((Energy->import_active[i] - Energy->start_energy[i]) * 100000);
    }

    if ((Energy->total[i] < (Energy->import_active[i] - 0.01f)) &&   // We subtract a little offset of 10Wh to avoid continuous updates
        Settings->flag3.hardware_energy_total) {                   // SetOption72 - Enable hardware energy total counter as reference (#6561)
      // The following calculation allows total usage (Energy->import_active[i]) up to +/-2147483.647 kWh
      RtcEnergySettings.energy_total_kWh[i] = Energy->import_active[i] - (Energy->energy_today_offset_kWh[i] + ((float)(Energy->kWhtoday[i]) / 100000));
      Energy->Settings.energy_total_kWh[i] = RtcEnergySettings.energy_total_kWh[i];
      Energy->total[i] = Energy->import_active[i];
      Energy->Settings.energy_kWhtotal_time = (!Energy->energy_today_offset_kWh[i]) ? LocalTime() : Midnight();
  //    AddLog(LOG_LEVEL_DEBUG, PSTR("NRG: Energy Total updated with hardware value"));
    }
  }

  EnergyUpdateToday();
}

/*********************************************************************************************/

void Energy200ms(void) {
  Energy->power_on = (TasmotaGlobal.power != 0) | Settings->flag.no_power_on_check;  // SetOption21 - Show voltage even if powered off

  Energy->fifth_second++;
  if (5 == Energy->fifth_second) {
    Energy->fifth_second = 0;

    XnrgCall(FUNC_ENERGY_EVERY_SECOND);

    if (RtcTime.valid) {

      if (!Energy->kWhtoday_offset_init && (RtcTime.day_of_year == Energy->Settings.energy_kWhdoy)) {
        Energy->kWhtoday_offset_init = true;
        for (uint32_t i = 0; i < 3; i++) {
          Energy->energy_today_offset_kWh[i] = Energy->Settings.energy_today_kWh[i];
//          RtcEnergySettings.energy_today_kWh[i] = 0;
        }
      }

      bool midnight = (LocalTime() == Midnight());
      if (midnight || (RtcTime.day_of_year > Energy->Settings.energy_kWhdoy)) {
        Energy->kWhtoday_offset_init = true;
        Energy->Settings.energy_kWhdoy = RtcTime.day_of_year;

        for (uint32_t i = 0; i < 3; i++) {
          Energy->Settings.energy_yesterday_kWh[i] = RtcEnergySettings.energy_today_kWh[i];

          RtcEnergySettings.energy_total_kWh[i] += RtcEnergySettings.energy_today_kWh[i];
          Energy->Settings.energy_total_kWh[i] = RtcEnergySettings.energy_total_kWh[i];
          Energy->Settings.energy_export_kWh[i] = RtcEnergySettings.energy_export_kWh[i];

          Energy->period_kWh[i] -= RtcEnergySettings.energy_today_kWh[i];     // this becomes a large unsigned, effectively a negative for EnergyShow calculation
          Energy->kWhtoday[i] = 0;
          Energy->energy_today_offset_kWh[i] = 0;
          RtcEnergySettings.energy_today_kWh[i] = 0;
          Energy->Settings.energy_today_kWh[i] = 0;

          Energy->start_energy[i] = 0;
//        Energy->kWhtoday_delta = 0;                                 // dont zero this, we need to carry the remainder over to tomorrow
          Energy->daily_sum_import_balanced = 0.0;
          Energy->daily_sum_export_balanced = 0.0;
        }
        EnergyUpdateToday();
      }
      if (midnight) {
        Energy->max_energy_state  = 3;
      }
      if ((RtcTime.hour == Settings->energy_max_energy_start) && (3 == Energy->max_energy_state )) {
        Energy->max_energy_state  = 0;
      }

    }
  }

  XnrgCall(FUNC_EVERY_200_MSECOND);
}

void EnergySaveState(void) {
  Energy->Settings.energy_kWhdoy = (RtcTime.valid) ? RtcTime.day_of_year : 0;

  for (uint32_t i = 0; i < 3; i++) {
    Energy->Settings.energy_today_kWh[i] = RtcEnergySettings.energy_today_kWh[i];
    Energy->Settings.energy_total_kWh[i] = RtcEnergySettings.energy_total_kWh[i];
    Energy->Settings.energy_export_kWh[i] = RtcEnergySettings.energy_export_kWh[i];
  }

  Energy->Settings.energy_usage = RtcEnergySettings.energy_usage;
}

bool EnergyMargin(bool type, uint16_t margin, uint16_t value, bool &flag, bool &save_flag) {
  bool change;

  if (!margin) return false;
  change = save_flag;
  if (type) {
    flag = (value > margin);
  } else {
    flag = (value < margin);
  }
  save_flag = flag;
  return (change != save_flag);
}

void EnergyMarginCheck(void) {
  if (!Energy->phase_count || (TasmotaGlobal.uptime < 8)) { return; }
  if (Energy->power_steady_counter) {
    Energy->power_steady_counter--;
    return;
  }

  bool jsonflg = false;
  Response_P(PSTR("{\"" D_RSLT_MARGINS "\":{"));

  int16_t power_diff[ENERGY_MAX_PHASES] = { 0 };
  for (uint32_t phase = 0; phase < Energy->phase_count; phase++) {
    uint16_t active_power = (uint16_t)(Energy->active_power[phase]);

//    AddLog(LOG_LEVEL_DEBUG, PSTR("NRG: APower %d, HPower0 %d, HPower1 %d, HPower2 %d"), active_power, Energy->power_history[phase][0], Energy->power_history[phase][1], Energy->power_history[phase][2]);

    if (Energy->Settings.power_delta[phase]) {
      power_diff[phase] = active_power - Energy->power_history[phase][0];
      uint16_t delta = abs(power_diff[phase]);
      bool threshold_met = false;
      if (delta > 0) {
        if (Energy->Settings.power_delta[phase] < 101) {  // 1..100 = Percentage
          uint16_t min_power = (Energy->power_history[phase][0] > active_power) ? active_power : Energy->power_history[phase][0];
          if (0 == min_power) { min_power++; }    // Fix divide by 0 exception (#6741)
          delta = (delta * 100) / min_power;
          if (delta >= Energy->Settings.power_delta[phase]) {
            threshold_met = true;
          }
        } else {                                  // 101..32000 = Absolute
          if (delta >= (Energy->Settings.power_delta[phase] -100)) {
            threshold_met = true;
          }
        }
      }
      if (threshold_met) {
        Energy->power_history[phase][1] = active_power;  // We only want one report so reset history
        Energy->power_history[phase][2] = active_power;
        jsonflg = true;
      } else {
        power_diff[phase] = 0;
      }
    }
    Energy->power_history[phase][0] = Energy->power_history[phase][1];  // Shift in history every second allowing power changes to settle for up to three seconds
    Energy->power_history[phase][1] = Energy->power_history[phase][2];
    Energy->power_history[phase][2] = active_power;
  }
  if (jsonflg) {
    float power_diff_f[Energy->phase_count];
    for (uint32_t phase = 0; phase < Energy->phase_count; phase++) {
      power_diff_f[phase] = power_diff[phase];
    }
    char value_chr[TOPSZ];
    ResponseAppend_P(PSTR("\"" D_CMND_POWERDELTA "\":%s"), EnergyFormat(value_chr, power_diff_f, 0));
  }

  uint16_t energy_power_u = (uint16_t)(Energy->active_power[0]);

  if (Energy->power_on && (Settings->energy_min_power || Settings->energy_max_power || Settings->energy_min_voltage || Settings->energy_max_voltage || Settings->energy_min_current || Settings->energy_max_current)) {
    uint16_t energy_voltage_u = (uint16_t)(Energy->voltage[0]);
    uint16_t energy_current_u = (uint16_t)(Energy->current[0] * 1000);

    DEBUG_DRIVER_LOG(PSTR("NRG: W %d, U %d, I %d"), energy_power_u, energy_voltage_u, energy_current_u);

    bool flag;
    if (EnergyMargin(false, Settings->energy_min_power, energy_power_u, flag, Energy->min_power_flag)) {
      ResponseAppend_P(PSTR("%s\"" D_CMND_POWERLOW "\":\"%s\""), (jsonflg)?",":"", GetStateText(flag));
      jsonflg = true;
    }
    if (EnergyMargin(true, Settings->energy_max_power, energy_power_u, flag, Energy->max_power_flag)) {
      ResponseAppend_P(PSTR("%s\"" D_CMND_POWERHIGH "\":\"%s\""), (jsonflg)?",":"", GetStateText(flag));
      jsonflg = true;
    }
    if (EnergyMargin(false, Settings->energy_min_voltage, energy_voltage_u, flag, Energy->min_voltage_flag)) {
      ResponseAppend_P(PSTR("%s\"" D_CMND_VOLTAGELOW "\":\"%s\""), (jsonflg)?",":"", GetStateText(flag));
      jsonflg = true;
    }
    if (EnergyMargin(true, Settings->energy_max_voltage, energy_voltage_u, flag, Energy->max_voltage_flag)) {
      ResponseAppend_P(PSTR("%s\"" D_CMND_VOLTAGEHIGH "\":\"%s\""), (jsonflg)?",":"", GetStateText(flag));
      jsonflg = true;
    }
    if (EnergyMargin(false, Settings->energy_min_current, energy_current_u, flag, Energy->min_current_flag)) {
      ResponseAppend_P(PSTR("%s\"" D_CMND_CURRENTLOW "\":\"%s\""), (jsonflg)?",":"", GetStateText(flag));
      jsonflg = true;
    }
    if (EnergyMargin(true, Settings->energy_max_current, energy_current_u, flag, Energy->max_current_flag)) {
      ResponseAppend_P(PSTR("%s\"" D_CMND_CURRENTHIGH "\":\"%s\""), (jsonflg)?",":"", GetStateText(flag));
      jsonflg = true;
    }
  }
  if (jsonflg) {
    ResponseJsonEndEnd();
    MqttPublishPrefixTopicRulesProcess_P(TELE, PSTR(D_RSLT_MARGINS), MQTT_TELE_RETAIN);
    EnergyMqttShow();
  }

  // Max Power
  if (Settings->energy_max_power_limit) {
    if (Energy->active_power[0] > Settings->energy_max_power_limit) {
      if (!Energy->mplh_counter) {
        Energy->mplh_counter = Settings->energy_max_power_limit_hold;
      } else {
        Energy->mplh_counter--;
        if (!Energy->mplh_counter) {
          ResponseTime_P(PSTR(",\"" D_JSON_MAXPOWERREACHED "\":%d}"), energy_power_u);
          MqttPublishPrefixTopicRulesProcess_P(STAT, S_RSLT_WARNING);
          EnergyMqttShow();
          SetAllPower(POWER_ALL_OFF, SRC_MAXPOWER);
          if (!Energy->mplr_counter) {
            Energy->mplr_counter = Settings->param[P_MAX_POWER_RETRY] +1;  // SetOption33 - Max Power Retry count
          }
          Energy->mplw_counter = Settings->energy_max_power_limit_window;
        }
      }
    }
    else if (TasmotaGlobal.power && (energy_power_u <= Settings->energy_max_power_limit)) {
      Energy->mplh_counter = 0;
      Energy->mplr_counter = 0;
      Energy->mplw_counter = 0;
    }
    if (!TasmotaGlobal.power) {
      if (Energy->mplw_counter) {
        Energy->mplw_counter--;
      } else {
        if (Energy->mplr_counter) {
          Energy->mplr_counter--;
          if (Energy->mplr_counter) {
            ResponseTime_P(PSTR(",\"" D_JSON_POWERMONITOR "\":\"%s\"}"), GetStateText(1));
            MqttPublishPrefixTopicRulesProcess_P(RESULT_OR_STAT, PSTR(D_JSON_POWERMONITOR));
            RestorePower(true, SRC_MAXPOWER);
          } else {
            ResponseTime_P(PSTR(",\"" D_JSON_MAXPOWERREACHEDRETRY "\":\"%s\"}"), GetStateText(0));
            MqttPublishPrefixTopicRulesProcess_P(STAT, S_RSLT_WARNING);
            EnergyMqttShow();
            SetAllPower(POWER_ALL_OFF, SRC_MAXPOWER);
          }
        }
      }
    }
  }

  // Max Energy
  if (Settings->energy_max_energy) {
    uint16_t energy_daily_u = (uint16_t)(Energy->daily_sum * 1000);
    if (!Energy->max_energy_state  && (RtcTime.hour == Settings->energy_max_energy_start)) {
      Energy->max_energy_state  = 1;
      ResponseTime_P(PSTR(",\"" D_JSON_ENERGYMONITOR "\":\"%s\"}"), GetStateText(1));
      MqttPublishPrefixTopicRulesProcess_P(RESULT_OR_STAT, PSTR(D_JSON_ENERGYMONITOR));
      RestorePower(true, SRC_MAXENERGY);
    }
    else if ((1 == Energy->max_energy_state ) && (energy_daily_u >= Settings->energy_max_energy)) {
      Energy->max_energy_state  = 2;
      ResponseTime_P(PSTR(",\"" D_JSON_MAXENERGYREACHED "\":%3_f}"), &Energy->daily_sum);
      MqttPublishPrefixTopicRulesProcess_P(STAT, S_RSLT_WARNING);
      EnergyMqttShow();
      SetAllPower(POWER_ALL_OFF, SRC_MAXENERGY);
    }
  }
}

void EnergyMqttShow(void) {
// {"Time":"2017-12-16T11:48:55","ENERGY":{"Total":0.212,"Yesterday":0.000,"Today":0.014,"Period":2.0,"Power":22.0,"Factor":1.00,"Voltage":213.6,"Current":0.100}}
  int tele_period_save = TasmotaGlobal.tele_period;
  TasmotaGlobal.tele_period = 2;
  ResponseClear();
  ResponseAppendTime();
  EnergyShow(true);
  TasmotaGlobal.tele_period = tele_period_save;
  ResponseJsonEnd();
  MqttPublishTeleSensor();
}

void EnergyEverySecond(void) {
  // Overtemp check
  if (Energy->use_overtemp && TasmotaGlobal.global_update) {
    if (TasmotaGlobal.power && !isnan(TasmotaGlobal.temperature_celsius) && (TasmotaGlobal.temperature_celsius > (float)Settings->param[P_OVER_TEMP])) {  // SetOption42 Device overtemp, turn off relays

      AddLog(LOG_LEVEL_DEBUG, PSTR("NRG: Temperature %1_f"), &TasmotaGlobal.temperature_celsius);

      SetAllPower(POWER_ALL_OFF, SRC_OVERTEMP);
    }
  }

  // Invalid data reset
  if (TasmotaGlobal.uptime > ENERGY_WATCHDOG) {
    uint32_t data_valid = Energy->phase_count;
    for (uint32_t i = 0; i < Energy->phase_count; i++) {
      if (Energy->data_valid[i] <= ENERGY_WATCHDOG) {
        Energy->data_valid[i]++;
        if (Energy->data_valid[i] > ENERGY_WATCHDOG) {
          // Reset energy registers
          Energy->voltage[i] = 0;
          Energy->current[i] = 0;
          Energy->active_power[i] = 0;
          if (!isnan(Energy->apparent_power[i])) { Energy->apparent_power[i] = 0; }
          if (!isnan(Energy->reactive_power[i])) { Energy->reactive_power[i] = 0; }
          if (!isnan(Energy->frequency[i])) { Energy->frequency[i] = 0; }
          if (!isnan(Energy->power_factor[i])) { Energy->power_factor[i] = 0; }
          if (!isnan(Energy->export_active[i])) { Energy->export_active[i] = 0; }

          data_valid--;
        }
      }
    }
    if (!data_valid) {
      //Energy->start_energy = 0;
      AddLog(LOG_LEVEL_DEBUG, PSTR("NRG: Energy reset by invalid data"));

      XnrgCall(FUNC_ENERGY_RESET);
    }
  }

  EnergyMarginCheck();
}

/*********************************************************************************************\
 * Commands
\*********************************************************************************************/

void ResponseCmndEnergyTotalYesterdayToday(void) {
  char value_chr[TOPSZ];   // Used by EnergyFormatIndex
  char value2_chr[TOPSZ];
  char value3_chr[TOPSZ];

  float energy_yesterday_kWh[3];
  for (uint32_t i = 0; i < Energy->phase_count; i++) {
    energy_yesterday_kWh[i] = Energy->Settings.energy_yesterday_kWh[i];
    Energy->total[i] = RtcEnergySettings.energy_total_kWh[i] + Energy->energy_today_offset_kWh[i] + ((float)(Energy->kWhtoday[i]) / 100000);
    if (Energy->local_energy_active_export) {
      Energy->export_active[i] = RtcEnergySettings.energy_export_kWh[i];
    }
  }

  Response_P(PSTR("{\"%s\":{\"" D_JSON_TOTAL "\":%s,\"" D_JSON_YESTERDAY "\":%s,\"" D_JSON_TODAY "\":%s"),
    XdrvMailbox.command,
    EnergyFormat(value_chr, Energy->total, Settings->flag2.energy_resolution),
    EnergyFormat(value2_chr, energy_yesterday_kWh, Settings->flag2.energy_resolution),
    EnergyFormat(value3_chr, Energy->daily_kWh, Settings->flag2.energy_resolution));
  if (Energy->local_energy_active_export) {
    ResponseAppend_P(PSTR(",\"" D_JSON_EXPORT_ACTIVE "\":%s"),
      EnergyFormat(value_chr, Energy->export_active, Settings->flag2.energy_resolution));
  }
  ResponseJsonEndEnd();
}

void CmndEnergyTotal(void) {
  uint32_t values[2] = { 0 };
  uint32_t params = ParseParameters(2, values);

  if ((XdrvMailbox.index > 0) && (XdrvMailbox.index <= Energy->phase_count) && (params > 0)) {
    uint32_t phase = XdrvMailbox.index -1;
    // Reset Energy Total
    RtcEnergySettings.energy_total_kWh[phase] = (float)(values[0]) / 1000;
    Energy->Settings.energy_total_kWh[phase] = RtcEnergySettings.energy_total_kWh[phase];
    if (params > 1) {
      Energy->Settings.energy_kWhtotal_time = values[1];
    } else {
      Energy->Settings.energy_kWhtotal_time = (!Energy->energy_today_offset_kWh[phase]) ? LocalTime() : Midnight();
    }
    RtcEnergySettings.energy_usage.last_usage_total_kWh = Energy->total[phase];
  }
  ResponseCmndEnergyTotalYesterdayToday();
}

void CmndEnergyYesterday(void) {
  uint32_t values[2] = { 0 };
  uint32_t params = ParseParameters(2, values);

  if ((XdrvMailbox.index > 0) && (XdrvMailbox.index <= Energy->phase_count) && (params > 0)) {
    uint32_t phase = XdrvMailbox.index -1;
    // Reset Energy Yesterday
    Energy->Settings.energy_yesterday_kWh[phase] = (float)(values[0]) / 1000;
    if (params > 1) {
      Energy->Settings.energy_kWhtotal_time = values[1];
    }
  }
  ResponseCmndEnergyTotalYesterdayToday();
}

void CmndEnergyToday(void) {
  // EnergyToday 22 = 0.022 kWh
  uint32_t values[2] = { 0 };
  uint32_t params = ParseParameters(2, values);

  if ((XdrvMailbox.index > 0) && (XdrvMailbox.index <= Energy->phase_count) && (params > 0)) {
    uint32_t phase = XdrvMailbox.index -1;
    // Reset Energy Today
    Energy->energy_today_offset_kWh[phase] = (float)(values[0]) / 1000;
    Energy->kWhtoday[phase] = 0;
    Energy->kWhtoday_delta[phase] = 0;
    Energy->start_energy[phase] = 0;
    Energy->period_kWh[phase] = Energy->energy_today_offset_kWh[phase];
    Energy->Settings.energy_today_kWh[phase] = Energy->energy_today_offset_kWh[phase];
    RtcEnergySettings.energy_today_kWh[phase] = Energy->energy_today_offset_kWh[phase];
    Energy->daily_kWh[phase] = Energy->energy_today_offset_kWh[phase];
    if (params > 1) {
      Energy->Settings.energy_kWhtotal_time = values[1];
    }
    else if (!RtcEnergySettings.energy_total_kWh[phase] && !Energy->energy_today_offset_kWh[phase]) {
      Energy->Settings.energy_kWhtotal_time = LocalTime();
    }
  }
  ResponseCmndEnergyTotalYesterdayToday();
}

void CmndEnergyExportActive(void) {
  if (Energy->local_energy_active_export) {
    // EnergyExportActive1 24
    // EnergyExportActive1 24,1650111291
    uint32_t values[2] = { 0 };
    uint32_t params = ParseParameters(2, values);

    if ((XdrvMailbox.index > 0) && (XdrvMailbox.index <= Energy->phase_count) && (params > 0)) {
      uint32_t phase = XdrvMailbox.index -1;
      // Reset Energy Export Active
      RtcEnergySettings.energy_export_kWh[phase] = (float)(values[0]) / 1000;
      Energy->Settings.energy_export_kWh[phase] = RtcEnergySettings.energy_export_kWh[phase];
      if (params > 1) {
        Energy->Settings.energy_kWhtotal_time = values[1];
      }
    }
    ResponseCmndEnergyTotalYesterdayToday();
  }
}

void ResponseCmndEnergyUsageExport(void) {
  Response_P(PSTR("{\"%s\":{\"" D_JSON_USAGE "\":[%*_f,%*_f],\"" D_JSON_EXPORT "\":[%*_f,%*_f]}}"),
    XdrvMailbox.command,
    Settings->flag2.energy_resolution, &Energy->Settings.energy_usage.usage_total_kWh[0],
    Settings->flag2.energy_resolution, &Energy->Settings.energy_usage.usage_total_kWh[1],
    Settings->flag2.energy_resolution, &Energy->Settings.energy_usage.return_total_kWh[0],
    Settings->flag2.energy_resolution, &Energy->Settings.energy_usage.return_total_kWh[1]);
}

void CmndEnergyUsage(void) {
  uint32_t values[2] = { 0 };
  uint32_t params = ParseParameters(2, values);
  if (params > 0) {
    // Reset energy_usage.usage totals
    RtcEnergySettings.energy_usage.usage_total_kWh[0] = (float)(values[0]) / 1000;
    if (params > 1) {
      RtcEnergySettings.energy_usage.usage_total_kWh[1] = (float)(values[1]) / 1000;
    }
    Energy->Settings.energy_usage.usage_total_kWh[0] = RtcEnergySettings.energy_usage.usage_total_kWh[0];
    Energy->Settings.energy_usage.usage_total_kWh[1] = RtcEnergySettings.energy_usage.usage_total_kWh[1];
  }
  ResponseCmndEnergyUsageExport();
}

void CmndEnergyExport(void) {
  uint32_t values[2] = { 0 };
  uint32_t params = ParseParameters(2, values);
  if (params > 0) {
    // Reset energy_usage.return totals
    RtcEnergySettings.energy_usage.return_total_kWh[0] = (float)(values[0]) / 1000;
    if (params > 1) {
      RtcEnergySettings.energy_usage.return_total_kWh[1] = (float)(values[1]) / 1000;
    }
    Energy->Settings.energy_usage.return_total_kWh[0] = RtcEnergySettings.energy_usage.return_total_kWh[0];
    Energy->Settings.energy_usage.return_total_kWh[1] = RtcEnergySettings.energy_usage.return_total_kWh[1];
  }
  ResponseCmndEnergyUsageExport();
}

void CmndTariff(void) {
  // Tariff1 22:00,23:00 - Tariff1 start hour for Standard Time and Daylight Savings Time
  // Tariff2 6:00,7:00   - Tariff2 start hour for Standard Time and Daylight Savings Time
  // Tariffx 1320, 1380  = minutes and also 22:00, 23:00
  // Tariffx 22, 23      = hours and also 22:00, 23:00
  // Tariff9 0/1

  if ((XdrvMailbox.index > 0) && (XdrvMailbox.index <= 2)) {
    uint32_t tariff = XdrvMailbox.index -1;
    uint32_t time_type = 0;
    char *p;
    char *str = strtok_r(XdrvMailbox.data, ", ", &p);  // 23:15, 22:30
    while ((str != nullptr) && (time_type < 2)) {
      char *q;
      uint32_t value = strtol(str, &q, 10);            // 23 or 22
      Energy->Settings.tariff[tariff][time_type] = value;
      if (value < 24) {                                // Below 24 is hours
        Energy->Settings.tariff[tariff][time_type] *= 60;      // Multiply hours by 60 minutes
        char *minute = strtok_r(nullptr, ":", &q);
        if (minute) {
          value = strtol(minute, nullptr, 10);         // 15 or 30
          if (value > 59) {
            value = 59;
          }
          Energy->Settings.tariff[tariff][time_type] += value;
        }
      }
      if (Energy->Settings.tariff[tariff][time_type] > 1439) {
        Energy->Settings.tariff[tariff][time_type] = 1439;     // Max is 23:59
      }
      str = strtok_r(nullptr, ", ", &p);
      time_type++;
    }
  }
  else if (XdrvMailbox.index == 9) {
    Settings->flag3.energy_weekend = XdrvMailbox.payload & 1;  // CMND_TARIFF
  }
  Response_P(PSTR("{\"%s\":{\"Off-Peak\":{\"STD\":\"%s\",\"DST\":\"%s\"},\"Standard\":{\"STD\":\"%s\",\"DST\":\"%s\"},\"Weekend\":\"%s\"}}"),
    XdrvMailbox.command,
    GetMinuteTime(Energy->Settings.tariff[0][0]).c_str(),GetMinuteTime(Energy->Settings.tariff[0][1]).c_str(),
    GetMinuteTime(Energy->Settings.tariff[1][0]).c_str(),GetMinuteTime(Energy->Settings.tariff[1][1]).c_str(),
    GetStateText(Settings->flag3.energy_weekend));             // CMND_TARIFF
}

uint32_t EnergyGetCalibration(uint32_t cal_type, uint32_t chan = 0) {
//  uint32_t channel = ((1 == chan) && (2 == Energy->phase_count)) ? 1 : 0;
  uint32_t channel = chan;
  switch (cal_type) {
    case ENERGY_POWER_CALIBRATION: return Energy->Settings.power_calibration[channel];
    case ENERGY_VOLTAGE_CALIBRATION: return Energy->Settings.voltage_calibration[channel];
    case ENERGY_CURRENT_CALIBRATION: return Energy->Settings.current_calibration[channel];
    case ENERGY_FREQUENCY_CALIBRATION: return Energy->Settings.frequency_calibration[channel];
  }
  return 0;
}

void EnergySetCalibration(uint32_t cal_type, uint32_t value, uint32_t chan = 0) {
//  uint32_t channel = ((1 == chan) && (2 == Energy->phase_count)) ? 1 : 0;
  uint32_t channel = chan;
  switch (cal_type) {
    case ENERGY_POWER_CALIBRATION: Energy->Settings.power_calibration[channel] = value; return;
    case ENERGY_VOLTAGE_CALIBRATION: Energy->Settings.voltage_calibration[channel] = value; return;
    case ENERGY_CURRENT_CALIBRATION: Energy->Settings.current_calibration[channel] = value; return;
    case ENERGY_FREQUENCY_CALIBRATION: Energy->Settings.frequency_calibration[channel] = value; return;
  }
}

void EnergyCommandCalSetResponse(uint32_t cal_type) {
  if (XdrvMailbox.payload > 99) {
    EnergySetCalibration(cal_type, XdrvMailbox.payload, XdrvMailbox.index -1);
  }
  if (Energy->phase_count > 1) {
    ResponseAppend_P(PSTR("["));
    for (uint32_t i = 0; i < Energy->phase_count; i++) {
      ResponseAppend_P(PSTR("%s%d"), (i>0)?",":"", EnergyGetCalibration(cal_type, i));
    }
    ResponseAppend_P(PSTR("]}"));
  } else {
    ResponseAppend_P(PSTR("%d}"), EnergyGetCalibration(cal_type));
  }
}

void EnergyCommandCalResponse(uint32_t cal_type) {
  Response_P(PSTR("{\"%s\":"), XdrvMailbox.command);
  EnergyCommandCalSetResponse(cal_type);
}

void EnergyCommandSetCalResponse(uint32_t cal_type) {
  Response_P(PSTR("{\"%sCal\":"), XdrvMailbox.command);
  EnergyCommandCalSetResponse(cal_type);
}

void CmndPowerCal(void) {
  Energy->command_code = CMND_POWERCAL;
  if (XnrgCall(FUNC_COMMAND)) {  // microseconds
    EnergyCommandCalResponse(ENERGY_POWER_CALIBRATION);
  }
}

void CmndVoltageCal(void) {
  Energy->command_code = CMND_VOLTAGECAL;
  if (XnrgCall(FUNC_COMMAND)) {  // microseconds
    EnergyCommandCalResponse(ENERGY_VOLTAGE_CALIBRATION);
  }
}

void CmndCurrentCal(void) {
  Energy->command_code = CMND_CURRENTCAL;
  if (XnrgCall(FUNC_COMMAND)) {  // microseconds
    EnergyCommandCalResponse(ENERGY_CURRENT_CALIBRATION);
  }
}

void CmndFrequencyCal(void) {
  Energy->command_code = CMND_FREQUENCYCAL;
  if (XnrgCall(FUNC_COMMAND)) {  // microseconds
    EnergyCommandCalResponse(ENERGY_FREQUENCY_CALIBRATION);
  }
}

void CmndPowerSet(void) {
  Energy->command_code = CMND_POWERSET;
  if (XnrgCall(FUNC_COMMAND)) {  // Watt
    EnergyCommandSetCalResponse(ENERGY_POWER_CALIBRATION);
  }
}

void CmndVoltageSet(void) {
  Energy->command_code = CMND_VOLTAGESET;
  if (XnrgCall(FUNC_COMMAND)) {  // Volt
    EnergyCommandSetCalResponse(ENERGY_VOLTAGE_CALIBRATION);
  }
}

void CmndCurrentSet(void) {
  Energy->command_code = CMND_CURRENTSET;
  if (XnrgCall(FUNC_COMMAND)) {  // milliAmpere
    EnergyCommandSetCalResponse(ENERGY_CURRENT_CALIBRATION);
  }
}

void CmndFrequencySet(void) {
  Energy->command_code = CMND_FREQUENCYSET;
  if (XnrgCall(FUNC_COMMAND)) {  // Hz
    EnergyCommandSetCalResponse(ENERGY_FREQUENCY_CALIBRATION);
  }
}

void CmndModuleAddress(void) {
  if ((XdrvMailbox.payload > 0) && (XdrvMailbox.payload <= 8) && (1 == Energy->phase_count)) {
    Energy->command_code = CMND_MODULEADDRESS;
    if (XnrgCall(FUNC_COMMAND)) {  // Module address
      ResponseCmndDone();
    }
  }
}

void CmndEnergyConfig(void) {
  Energy->command_code = CMND_ENERGYCONFIG;
  ResponseClear();
  if (XnrgCall(FUNC_COMMAND)) {
    if (!ResponseLength()) {
      ResponseCmndDone();
    }
  }
}

/*********************************************************************************************\
 * USE_ENERGY_MARGIN_DETECTION and USE_ENERGY_POWER_LIMIT
\*********************************************************************************************/

void EnergyMarginStatus(void) {
  Response_P(PSTR("{\"" D_CMND_STATUS D_STATUS9_MARGIN "\":{\"" D_CMND_POWERDELTA "\":["));
  for (uint32_t i = 0; i < ENERGY_MAX_PHASES; i++) {
    ResponseAppend_P(PSTR("%s%d"), (i>0)?",":"", Energy->Settings.power_delta[i]);
  }
  ResponseAppend_P(PSTR("],\"" D_CMND_POWERLOW "\":%d,\"" D_CMND_POWERHIGH "\":%d,\""
                        D_CMND_VOLTAGELOW "\":%d,\"" D_CMND_VOLTAGEHIGH "\":%d,\"" D_CMND_CURRENTLOW "\":%d,\"" D_CMND_CURRENTHIGH "\":%d}}"),
                        Settings->energy_min_power, Settings->energy_max_power,
                        Settings->energy_min_voltage, Settings->energy_max_voltage, Settings->energy_min_current, Settings->energy_max_current);
}

void CmndPowerDelta(void) {
  if ((XdrvMailbox.index > 0) && (XdrvMailbox.index <= ENERGY_MAX_PHASES)) {
    if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 32000)) {
      Energy->Settings.power_delta[XdrvMailbox.index -1] = XdrvMailbox.payload;
    }
    ResponseCmndIdxNumber(Energy->Settings.power_delta[XdrvMailbox.index -1]);
  }
}

void CmndPowerLow(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 6000)) {
    Settings->energy_min_power = XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_min_power);
}

void CmndPowerHigh(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 6000)) {
    Settings->energy_max_power = XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_max_power);
}

void CmndVoltageLow(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 500)) {
    Settings->energy_min_voltage = XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_min_voltage);
}

void CmndVoltageHigh(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 500)) {
    Settings->energy_max_voltage = XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_max_voltage);
}

void CmndCurrentLow(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 25000)) {
    Settings->energy_min_current = XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_min_current);
}

void CmndCurrentHigh(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 25000)) {
    Settings->energy_max_current = XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_max_current);
}

void CmndMaxPower(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 6000)) {
    Settings->energy_max_power_limit = XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_max_power_limit);
}

void CmndMaxPowerHold(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 6000)) {
    Settings->energy_max_power_limit_hold = (1 == XdrvMailbox.payload) ? MAX_POWER_HOLD : XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_max_power_limit_hold);
}

void CmndMaxPowerWindow(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 6000)) {
    Settings->energy_max_power_limit_window = (1 == XdrvMailbox.payload) ? MAX_POWER_WINDOW : XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_max_power_limit_window);
}

void CmndSafePower(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 6000)) {
    Settings->energy_max_power_safe_limit = XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_max_power_safe_limit);
}

void CmndSafePowerHold(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 6000)) {
    Settings->energy_max_power_safe_limit_hold = (1 == XdrvMailbox.payload) ? SAFE_POWER_HOLD : XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_max_power_safe_limit_hold);
}

void CmndSafePowerWindow(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload < 1440)) {
    Settings->energy_max_power_safe_limit_window = (1 == XdrvMailbox.payload) ? SAFE_POWER_WINDOW : XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_max_power_safe_limit_window);
}

void CmndMaxEnergy(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload <= 6000)) {
    Settings->energy_max_energy = XdrvMailbox.payload;
    Energy->max_energy_state  = 3;
  }
  ResponseCmndNumber(Settings->energy_max_energy);
}

void CmndMaxEnergyStart(void) {
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload < 24)) {
    Settings->energy_max_energy_start = XdrvMailbox.payload;
  }
  ResponseCmndNumber(Settings->energy_max_energy_start);
}

/********************************************************************************************/

void EnergyDrvInit(void) {
  Energy = (tEnergy*)calloc(sizeof(tEnergy), 1);    // Need calloc to reset registers to 0/false
  if (!Energy) { return; }

  EnergySettingsLoad();
  EnergyRtcSettingsLoad();

//  Energy->voltage_common = false;
//  Energy->frequency_common = false;
//  Energy->use_overtemp = false;
  for (uint32_t phase = 0; phase < ENERGY_MAX_PHASES; phase++) {
    Energy->apparent_power[phase] = NAN;
    Energy->reactive_power[phase] = NAN;
    Energy->power_factor[phase] = NAN;
    Energy->frequency[phase] = NAN;
    Energy->export_active[phase] = NAN;
  }
  Energy->phase_count = 1;              // Number of phases active
  Energy->voltage_available = true;     // Enable if voltage is measured
  Energy->current_available = true;     // Enable if current is measured
  Energy->power_on = true;

  TasmotaGlobal.energy_driver = ENERGY_NONE;
  XnrgCall(FUNC_PRE_INIT);             // Find first energy driver
  if (TasmotaGlobal.energy_driver) {
    AddLog(LOG_LEVEL_INFO, PSTR("NRG: Init driver %d"), TasmotaGlobal.energy_driver);
  }
}

void EnergySnsInit(void) {
  XnrgCall(FUNC_INIT);

  if (TasmotaGlobal.energy_driver) {
/*
    AddLog(LOG_LEVEL_DEBUG, PSTR("NRG: Rtc valid %d, kWhtoday_ph Rtc %3_f/%3_f/%3_f, Set %3_f/%3_f/%3_f"),
      EnergyRtcSettingsValid(),
      &RtcEnergySettings.energy_today_kWh[0],&RtcEnergySettings.energy_today_kWh[1],&RtcEnergySettings.energy_today_kWh[2],
      &Energy->Settings.energy_today_kWh[0],&Energy->Settings.energy_today_kWh[1],&Energy->Settings.energy_today_kWh[2]
    );
*/
    for (uint32_t i = 0; i < 3; i++) {
//    Energy->energy_today_offset_kWh[i] = 0;   // Reset by EnergyDrvInit()
      // 20220805 - Change from https://github.com/arendst/Tasmota/issues/16118
      if (EnergyRtcSettingsValid()) {
        Energy->energy_today_offset_kWh[i] = RtcEnergySettings.energy_today_kWh[i];
        RtcEnergySettings.energy_today_kWh[i] = 0;
        Energy->kWhtoday_offset_init = true;
      }
//    Energy->kWhtoday_ph[i] = 0;       // Reset by EnergyDrvInit()
//    Energy->kWhtoday_delta[i] = 0;    // Reset by EnergyDrvInit()
      Energy->period_kWh[i] = Energy->energy_today_offset_kWh[i];
      if (Energy->local_energy_active_export) {
        Energy->export_active[i] = 0;   // Was set to NAN by EnergyDrvInit()
      }
    }
    EnergyUpdateToday();
    ticker_energy.attach_ms(200, Energy200ms);
  }
}

#ifdef USE_WEBSERVER
const char HTTP_ENERGY_SNS1[] PROGMEM =
  "{s}" D_POWERUSAGE_APPARENT "{m}%s " D_UNIT_VA "{e}"
  "{s}" D_POWERUSAGE_REACTIVE "{m}%s " D_UNIT_VAR "{e}"
  "{s}" D_POWER_FACTOR "{m}%s{e}";

const char HTTP_ENERGY_SNS2[] PROGMEM =
  "{s}" D_ENERGY_TODAY "{m}%s " D_UNIT_KILOWATTHOUR "{e}"
  "{s}" D_ENERGY_YESTERDAY "{m}%s " D_UNIT_KILOWATTHOUR "{e}"
  "{s}" D_ENERGY_TOTAL "{m}%s " D_UNIT_KILOWATTHOUR "{e}";      // {s} = <tr><th>, {m} = </th><td>, {e} = </td></tr>

const char HTTP_ENERGY_SNS3[] PROGMEM =
  "{s}" D_EXPORT_ACTIVE "{m}%s " D_UNIT_KILOWATTHOUR "{e}";
#endif  // USE_WEBSERVER

void EnergyShow(bool json) {
  if (Energy->voltage_common) {
    for (uint32_t i = 0; i < Energy->phase_count; i++) {
      Energy->voltage[i] = Energy->voltage[0];
    }
  }

  float apparent_power[Energy->phase_count];
  float reactive_power[Energy->phase_count];
  float power_factor[Energy->phase_count];
  if (!Energy->type_dc) {
    if (Energy->current_available && Energy->voltage_available) {
      for (uint32_t i = 0; i < Energy->phase_count; i++) {
        apparent_power[i] = Energy->apparent_power[i];
        if (isnan(apparent_power[i])) {
          apparent_power[i] = Energy->voltage[i] * Energy->current[i];
        }
        if (apparent_power[i] < Energy->active_power[i]) {  // Should be impossible
          Energy->active_power[i] = apparent_power[i];
        }

        power_factor[i] = Energy->power_factor[i];
        if (isnan(power_factor[i])) {
          power_factor[i] = (Energy->active_power[i] && apparent_power[i]) ? Energy->active_power[i] / apparent_power[i] : 0;
          if (power_factor[i] > 1) {
            power_factor[i] = 1;
          }
        }

        reactive_power[i] = Energy->reactive_power[i];
        if (isnan(reactive_power[i])) {
          reactive_power[i] = 0;
          uint32_t difference = ((uint32_t)(apparent_power[i] * 100) - (uint32_t)(Energy->active_power[i] * 100)) / 10;
          if ((Energy->current[i] > 0.005f) && ((difference > 15) || (difference > (uint32_t)(apparent_power[i] * 100 / 1000)))) {
            // calculating reactive power only if current is greater than 0.005A and
            // difference between active and apparent power is greater than 1.5W or 1%
            //reactive_power[i] = (float)(RoundSqrtInt((uint64_t)(apparent_power[i] * apparent_power[i] * 100) - (uint64_t)(Energy->active_power[i] * Energy->active_power[i] * 100))) / 10;
            float power_diff = apparent_power[i] * apparent_power[i] - Energy->active_power[i] * Energy->active_power[i];
            if (power_diff < 10737418) // 2^30 / 100 (RoundSqrtInt is limited to 2^30-1)
              reactive_power[i] = (float)(RoundSqrtInt((uint32_t)(power_diff * 100.0f))) / 10.0f;
            else
              reactive_power[i] = (float)(SqrtInt((uint32_t)(power_diff)));
          }
        }

      }
    }
  }

  float active_power_sum = 0.0f;
  float energy_yesterday_kWh[Energy->phase_count];
  for (uint32_t i = 0; i < Energy->phase_count; i++) {
    energy_yesterday_kWh[i] = Energy->Settings.energy_yesterday_kWh[i];

    active_power_sum += Energy->active_power[i];
  }

  bool energy_tariff = false;
  float energy_usage_kWh[2];
  float energy_return_kWh[2];
  if (Energy->Settings.tariff[0][0] != Energy->Settings.tariff[1][0]) {
    energy_usage_kWh[0] = RtcEnergySettings.energy_usage.usage_total_kWh[0];    // Tariff1
    energy_usage_kWh[1] = RtcEnergySettings.energy_usage.usage_total_kWh[1];    // Tariff2
    energy_return_kWh[0] = RtcEnergySettings.energy_usage.return_total_kWh[0];  // Tariff1
    energy_return_kWh[1] = RtcEnergySettings.energy_usage.return_total_kWh[1];  // Tariff2
    energy_tariff = true;
  }

  char value_chr[GUISZ];   // Used by EnergyFormatIndex
  char value2_chr[GUISZ];
  char value3_chr[GUISZ];

  if (json) {
    bool show_energy_period = (0 == TasmotaGlobal.tele_period);

    ResponseAppend_P(PSTR(",\"" D_RSLT_ENERGY "\":{\"" D_JSON_TOTAL_START_TIME "\":\"%s\",\"" D_JSON_TOTAL "\":%s"),
      GetDateAndTime(DT_ENERGY).c_str(),
      EnergyFormat(value_chr, Energy->total, Settings->flag2.energy_resolution, 2));

    if (energy_tariff) {
      ResponseAppend_P(PSTR(",\"" D_JSON_TOTAL D_CMND_TARIFF "\":%s"),
        EnergyFormat(value_chr, energy_usage_kWh, Settings->flag2.energy_resolution, 6));
    }

    ResponseAppend_P(PSTR(",\"" D_JSON_YESTERDAY "\":%s,\"" D_JSON_TODAY "\":%s"),
      EnergyFormat(value_chr, energy_yesterday_kWh, Settings->flag2.energy_resolution, 2),
      EnergyFormat(value2_chr, Energy->daily_kWh, Settings->flag2.energy_resolution, 2));

/*
 #if defined(SDM630_IMPORT) || defined(SDM72_IMPEXP)
    if (!isnan(Energy->import_active[0])) {
      ResponseAppend_P(PSTR(",\"" D_JSON_IMPORT_ACTIVE "\":%s"),
        EnergyFormat(value_chr, Energy->import_active, Settings->flag2.energy_resolution));
      if (energy_tariff) {
        ResponseAppend_P(PSTR(",\"" D_JSON_IMPORT D_CMND_TARIFF "\":%s"),
          EnergyFormat(value_chr, energy_return_kWh, Settings->flag2.energy_resolution, 6));
      }
    }
#endif  // SDM630_IMPORT || SDM72_IMPEXP
*/

    if (!isnan(Energy->export_active[0])) {
      uint32_t single = (!isnan(Energy->export_active[1]) && !isnan(Energy->export_active[2])) ? 0 : 1;
      ResponseAppend_P(PSTR(",\"" D_JSON_TODAY_SUM_IMPORT "\":%s,\"" D_JSON_TODAY_SUM_EXPORT "\":%s,\"" D_JSON_EXPORT_ACTIVE "\":%s"),
        EnergyFormat(value_chr, &Energy->daily_sum_import_balanced, Settings->flag2.energy_resolution, 1),
        EnergyFormat(value2_chr, &Energy->daily_sum_export_balanced, Settings->flag2.energy_resolution, 1),
        EnergyFormat(value3_chr, Energy->export_active, Settings->flag2.energy_resolution, single));
      if (energy_tariff) {
        ResponseAppend_P(PSTR(",\"" D_JSON_EXPORT D_CMND_TARIFF "\":%s"),
          EnergyFormat(value_chr, energy_return_kWh, Settings->flag2.energy_resolution, 6));
      }
    }

    if (show_energy_period) {
      float energy_period[Energy->phase_count];
      for (uint32_t i = 0; i < Energy->phase_count; i++) {
        energy_period[i] = RtcEnergySettings.energy_today_kWh[i] - Energy->period_kWh[i];
        Energy->period_kWh[i] = RtcEnergySettings.energy_today_kWh[i];
      }
      ResponseAppend_P(PSTR(",\"" D_JSON_PERIOD "\":%s"),
        EnergyFormat(value_chr, energy_period, Settings->flag2.wattage_resolution));
    }

    ResponseAppend_P(PSTR(",\"" D_JSON_POWERUSAGE "\":%s"),
        EnergyFormat(value_chr, Energy->active_power, Settings->flag2.wattage_resolution));
    if (!Energy->type_dc) {
      if (Energy->current_available && Energy->voltage_available) {
        ResponseAppend_P(PSTR(",\"" D_JSON_APPARENT_POWERUSAGE "\":%s,\"" D_JSON_REACTIVE_POWERUSAGE "\":%s,\"" D_JSON_POWERFACTOR "\":%s"),
          EnergyFormat(value_chr, apparent_power, Settings->flag2.wattage_resolution),
          EnergyFormat(value2_chr, reactive_power, Settings->flag2.wattage_resolution),
          EnergyFormat(value3_chr, power_factor, 2));
      }
      if (!isnan(Energy->frequency[0])) {
        ResponseAppend_P(PSTR(",\"" D_JSON_FREQUENCY "\":%s"),
          EnergyFormat(value_chr, Energy->frequency, Settings->flag2.frequency_resolution, Energy->frequency_common));
      }
    }
    if (Energy->voltage_available) {
      ResponseAppend_P(PSTR(",\"" D_JSON_VOLTAGE "\":%s"),
        EnergyFormat(value_chr, Energy->voltage, Settings->flag2.voltage_resolution, Energy->voltage_common));
    }
    if (Energy->current_available) {
      ResponseAppend_P(PSTR(",\"" D_JSON_CURRENT "\":%s"),
        EnergyFormat(value_chr, Energy->current, Settings->flag2.current_resolution));
    }
    XnrgCall(FUNC_JSON_APPEND);
    ResponseJsonEnd();

#ifdef USE_DOMOTICZ
    if (show_energy_period) {  // Only send if telemetry
      char temp_chr[FLOATSZ];
      if (Energy->voltage_available) {
        dtostrfd(Energy->voltage[0], Settings->flag2.voltage_resolution, temp_chr);
        DomoticzSensor(DZ_VOLTAGE, temp_chr);  // Voltage
      }
      if (Energy->current_available) {
        dtostrfd(Energy->current[0], Settings->flag2.current_resolution, temp_chr);
        DomoticzSensor(DZ_CURRENT, temp_chr);  // Current
      }
      dtostrfd(Energy->total_sum * 1000, 1, temp_chr);
      DomoticzSensorPowerEnergy((int)active_power_sum, temp_chr);  // PowerUsage, EnergyToday

      char energy_usage_chr[2][FLOATSZ];
      char energy_return_chr[2][FLOATSZ];
      dtostrfd(RtcEnergySettings.energy_usage.usage_total_kWh[0], 1, energy_usage_chr[0]);  // Tariff1
      dtostrfd(RtcEnergySettings.energy_usage.usage_total_kWh[1], 1, energy_usage_chr[1]);  // Tariff2
      dtostrfd(RtcEnergySettings.energy_usage.return_total_kWh[0], 1, energy_return_chr[0]);
      dtostrfd(RtcEnergySettings.energy_usage.return_total_kWh[1], 1, energy_return_chr[1]);
      DomoticzSensorP1SmartMeter(energy_usage_chr[0], energy_usage_chr[1], energy_return_chr[0], energy_return_chr[1], (int)active_power_sum);

    }
#endif  // USE_DOMOTICZ
#ifdef USE_KNX
    if (show_energy_period) {
      if (Energy->voltage_available) {
        KnxSensor(KNX_ENERGY_VOLTAGE, Energy->voltage[0]);
      }
      if (Energy->current_available) {
        KnxSensor(KNX_ENERGY_CURRENT, Energy->current[0]);
      }
      KnxSensor(KNX_ENERGY_POWER, active_power_sum);
      if (!Energy->type_dc) {
        KnxSensor(KNX_ENERGY_POWERFACTOR, power_factor[0]);
      }
      KnxSensor(KNX_ENERGY_DAILY, Energy->daily_sum);
      KnxSensor(KNX_ENERGY_TOTAL, Energy->total_sum);
      KnxSensor(KNX_ENERGY_YESTERDAY, Energy->yesterday_sum);
    }
#endif  // USE_KNX
#ifdef USE_WEBSERVER
  } else {
#ifdef USE_ENERGY_COLUMN_GUI
    // Need a new table supporting more columns using empty columns (with &nbsp; in data rows) as easy column spacing
    // {s}</th><th></th><th>Head1</th><th></th><td>{e}
    // {s}</th><th></th><th>Head1</th><th></th><th>Head2</th><th></th><td>{e}
    // {s}</th><th></th><th>Head1</th><th></th><th>Head2</th><th></th><th>Head3</th><th></th><td>{e}
    // {s}</th><th></th><th>Head1</th><th></th><th>Head2</th><th></th><th>Head3</th><th></th><th>Head4</th><th></th><td>{e}
    WSContentSend_P(PSTR("</table><hr/>{t}{s}</th><th></th>")); // First column is empty ({t} = <table style='width:100%'>, {s} = <tr><th>)
    bool label_o = Energy->voltage_common;
    bool no_label = (1 == Energy->phase_count);
    for (uint32_t i = 0; i < Energy->phase_count; i++) {
      WSContentSend_P(PSTR("<th style='text-align:center'>%s%s<th></th>"), (no_label)?"":(label_o)?"O":"L", (no_label)?"":itoa(i +1, value_chr, 10));
    }
    WSContentSend_P(PSTR("<td>{e}"));   // Last column is units ({e} = </td></tr>)
#endif  // USE_ENERGY_COLUMN_GUI
    if (Energy->voltage_available) {
      WSContentSend_PD(HTTP_SNS_VOLTAGE, WebEnergyFormat(value_chr, Energy->voltage, Settings->flag2.voltage_resolution, Energy->voltage_common));
    }
    if (!Energy->type_dc) {
      if (!isnan(Energy->frequency[0])) {
        WSContentSend_PD(PSTR("{s}" D_FREQUENCY "{m}%s " D_UNIT_HERTZ "{e}"),
          WebEnergyFormat(value_chr, Energy->frequency, Settings->flag2.frequency_resolution, Energy->frequency_common));
      }
    }
    if (Energy->current_available) {
      WSContentSend_PD(HTTP_SNS_CURRENT, WebEnergyFormat(value_chr, Energy->current, Settings->flag2.current_resolution));
    }
    WSContentSend_PD(HTTP_SNS_POWER, WebEnergyFormat(value_chr, Energy->active_power, Settings->flag2.wattage_resolution));
    if (!Energy->type_dc) {
      if (Energy->current_available && Energy->voltage_available) {
        WSContentSend_PD(HTTP_ENERGY_SNS1, WebEnergyFormat(value_chr, apparent_power, Settings->flag2.wattage_resolution),
                                           WebEnergyFormat(value2_chr, reactive_power, Settings->flag2.wattage_resolution),
                                           WebEnergyFormat(value3_chr, power_factor, 2));
      }
    }
    WSContentSend_PD(HTTP_ENERGY_SNS2, WebEnergyFormat(value_chr, Energy->daily_kWh, Settings->flag2.energy_resolution, 2),
                                       WebEnergyFormat(value2_chr, energy_yesterday_kWh, Settings->flag2.energy_resolution, 2),
                                       WebEnergyFormat(value3_chr, Energy->total, Settings->flag2.energy_resolution, 2));
    if (!isnan(Energy->export_active[0])) {
      uint32_t single = (!isnan(Energy->export_active[1]) && !isnan(Energy->export_active[2])) ? 2 : 1;
      WSContentSend_PD(HTTP_ENERGY_SNS3, WebEnergyFormat(value_chr, Energy->export_active, Settings->flag2.energy_resolution, single));
    }
#ifdef USE_ENERGY_COLUMN_GUI
    XnrgCall(FUNC_WEB_COL_SENSOR);
    WSContentSend_P(PSTR("</table><hr/>{t}"));    // {t} = <table style='width:100%'> - Define for next FUNC_WEB_SENSOR
#endif  // USE_ENERGY_COLUMN_GUI
    XnrgCall(FUNC_WEB_SENSOR);
#endif  // USE_WEBSERVER
  }
}

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

bool Xdrv03(uint32_t function)
{
  bool result = false;

  if (FUNC_PRE_INIT == function) {
    EnergyDrvInit();
  }
  else if (TasmotaGlobal.energy_driver) {
    switch (function) {
      case FUNC_LOOP:
      case FUNC_SLEEP_LOOP:
        XnrgCall(FUNC_LOOP);
        break;
      case FUNC_EVERY_250_MSECOND:
        if (TasmotaGlobal.uptime > 4) {
          XnrgCall(FUNC_EVERY_250_MSECOND);
        }
        break;
      case FUNC_EVERY_SECOND:
        XnrgCall(FUNC_EVERY_SECOND);
        break;
      case FUNC_SERIAL:
        result = XnrgCall(FUNC_SERIAL);
        break;
      case FUNC_SAVE_SETTINGS:
        EnergySettingsSave();
        EnergyRtcSettingsSave();
        break;
      case FUNC_SET_POWER:
        Energy->power_steady_counter = 2;
        break;
      case FUNC_COMMAND:
        result = DecodeCommand(kEnergyCommands, EnergyCommand);
        break;
      case FUNC_NETWORK_UP:
        XnrgCall(FUNC_NETWORK_UP);
        break;
      case FUNC_NETWORK_DOWN:
        XnrgCall(FUNC_NETWORK_DOWN);
        break;
    }
  }
  return result;
}

bool Xsns03(uint32_t function)
{
  bool result = false;

  if (TasmotaGlobal.energy_driver) {
    switch (function) {
      case FUNC_EVERY_SECOND:
        EnergyEverySecond();
        break;
      case FUNC_JSON_APPEND:
        EnergyShow(true);
        break;
#ifdef USE_WEBSERVER
      case FUNC_WEB_SENSOR:
        EnergyShow(false);
        break;
#endif  // USE_WEBSERVER
      case FUNC_SAVE_BEFORE_RESTART:
        EnergySaveState();
        break;
      case FUNC_INIT:
        EnergySnsInit();
        break;
    }
  }
  return result;
}

#endif  // USE_ENERGY_SENSOR_ESP32
