#ifndef ENCODER_STORAGE_H
#define ENCODER_STORAGE_H

typedef struct {
    float close_limit_encode;
    float open_limit_encode;
} ValveCalib;

esp_err_t valve_calib_save(ValveCalib *calib);

esp_err_t valve_calib_load(ValveCalib *calib);

void load_eeprom_calibration(void);

void save_eeprom_calibration(void);


#endif /* WSENCODER_STORAGE_H */