// HINT! See also `print_pin_config_html.c` and `doc/pin_config.html`
#ifndef CONFIG_H
                                     // GP0-GP28 (GPIO number, *not* pin number)
			             // |
			             // |
#define PINS \
	PIN(DATA,      READ_DATA,       0    ) \
	PIN(DATA,      READ_CLOCK,      1    ) \
	PIN(STATUS,    INDEX,           2    ) \
	PIN(STATUS,    SECTOR,          3    ) \
	PIN(STATUS,    FAULT,           4    ) \
	PIN(STATUS,    SEEK_ERROR,      5    ) \
	PIN(STATUS,    ON_CYLINDER,     6    ) \
	PIN(STATUS,    UNIT_READY,      7    ) \
	PIN(STATUS,    ADDRESS_MARK,    8    ) \
	PIN(STATUS,    UNIT_SELECTED,   9    ) \
	PIN(STATUS,    SEEK_END,        10   ) \
	PIN(CTRL,      UNIT_SELECT_TAG, 14   ) \
	PIN(CTRL,      TAG1,            11   ) \
	PIN(CTRL,      TAG2,            12   ) \
	PIN(CTRL,      TAG3,            13   ) \
	PIN(CTRL,      BIT0,            16   ) \
	PIN(CTRL,      BIT1,            17   ) \
	PIN(CTRL,      BIT2,            18   ) \
	PIN(CTRL,      BIT3,            19   ) \
	PIN(CTRL,      BIT4,            20   ) \
	PIN(CTRL,      BIT5,            21   ) \
	PIN(CTRL,      BIT6,            22   ) \
	PIN(CTRL,      BIT7,            26   ) \
	PIN(CTRL,      BIT8,            27   ) \
	PIN(CTRL,      BIT9,            28   ) \

// Naming matches figure 3-11 in this document:
// https://bitsavers.org/pdf/cdc/discs/smd/83322200M_CDC_BK4XX_BK5XX_Hardware_Reference_Manual_Jun1980.pdf

enum gpio_type { // <- first PIN() column
	DATA     = 1, // input: 9.67MHz data rate
	STATUS,       // input: low bitrate
	CTRL          // output: low bitrate
};

enum gpio_map {
	#define PIN(TYPE, NAME, GPIO) GPIO_ ## NAME = GPIO,
	PINS
	#undef PIN
};

#define CONFIG_H
#endif
