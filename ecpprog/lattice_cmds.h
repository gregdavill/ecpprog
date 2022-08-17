
#include <stdint.h>

/* Not sure if all of these are applicable to the JTAG interface */
enum lattice_ecp5_cmd
{
	ISC_NOOP = 0xFF, /* 0 bits - Non-operation */
	READ_ID = 0xE0, /* 24 bits - Read out the 32-bit IDCODE of the device */
	USERCODE = 0xC0, /* 24 bits - Read 32-bit usercode */
	LSC_READ_STATUS = 0x3C, /* 24 bits - Read out internal status */
	LSC_CHECK_BUSY = 0xF0, /* 24 bits - Read 1 bit busy flag to check the command execution status */
	LSC_REFRESH = 0x79, /* 24 bits - Equivalent to toggle PROGRAMN pin */
	ISC_ENABLE = 0xC6, /* 24 bits - Enable the Offline configuration mode */
	ISC_ENABLE_X = 0x74, /* 24 bits - Enable the Transparent configuration mode */
	ISC_DISABLE = 0x26, /* 24 bits - Disable the configuration operation */
	ISC_PROGRAM_USERCODE = 0xC2, /* 24 bits - Write the 32-bit new USERCODE data to USERCODE register */
	ISC_ERASE = 0x0E, /* 24 bits - Bulk erase the memory array base on the access mode and array selection */
	ISC_PROGRAM_DONE = 0x5E, /* 24 bits - Program the DONE bit if the device is in Configuration state. */
	ISC_PROGRAM_SECURITY = 0xCE, /* 24 bits - Program the Security bit if the device is in Configuration state */
	LSC_INIT_ADDRESS = 0x46, /* 24 bits - Initialize the Address Shift Register */
	LSC_WRITE_ADDRESS = 0xB4, /* 24 bits - Write the 16 bit Address Register to move the address quickly */
	LSC_BITSTREAM_BURST = 0x7A, /* 24 bits - Program the device the whole bitstream sent in as the command operand */
	LSC_PROG_INCR_RTI = 0x82, /* 24 bits - Write configuration data to the configuration memory frame at current address and post increment the address, Byte 2~0 of the opcode indicate number of the frames included in the operand field */
	LSC_PROG_INCR_ENC = 0xB6, /* 24 bits - Encrypt the configuration data then write */
	LSC_PROG_INCR_CMP = 0xB8, /* 24 bits - Decompress the configuration data, then write */
	LSC_PROG_INCR_CNE = 0xBA, /* 24 bits - Decompress and Encrypt the configuration data, then write */
	LSC_VERIFY_INCR_RTI = 0x6A, /* 24 bits - Read back the configuration memory frame selected by the address register and post increment the address */
	LSC_PROG_CTRL0 = 0x22, /* 24 bits - Modify the Control Register 0 */
	LSC_READ_CTRL0 = 0x20, /* 24 bits - Read the Control Register 0 */
	LSC_RESET_CRC = 0x3B, /* 24 bits - Reset 16-bit frame CRC register to 0x0000 */
	LSC_READ_CRC = 0x60, /* 24 bits - Read 16-bit frame CRC register content */
	LSC_PROG_SED_CRC = 0xA2, /* 24 bits - Program the calculated 32-bit CRC based on configuration bit values only into overall CRC register */
	LSC_READ_SED_CRC = 0xA4, /* 24 bits - Read the 32-bit SED CRC */
	LSC_PROG_PASSWORD = 0xF1, /* 24 bits - Program 64-bit password into the non-volatile memory (Efuse) */
	LSC_READ_PASSWORD = 0xF2, /* 24 bits - Read out the 64-bit password before activated for verification */
	LSC_SHIFT_PASSWORD = 0xBC, /* 24 bits - Shift in the password to unlock for re-configuration (necessary when password protection feature is active). */
	LSC_PROG_CIPHER_KEY = 0xF3, /* 24 bits - Program the 128-bit cipher key into Efuse */
	LSC_READ_CIPHER_KEY = 0xF4, /* 24 bits - Read out the 128-bit cipher key before activated for verification */
	LSC_PROG_FEATURE = 0xE4, /* 24 bits - Program User Feature, such as Customer ID, I2C Slave Address, Unique ID Header */
	LSC_READ_FEATURE = 0xE7, /* 24 bits - Read User Feature, such as Customer ID, I2C Slave Address, Unique ID Header */
	LSC_PROG_FEABITS = 0xF8, /* 24 bits - Program User Feature Bits, such as CFG port and pin persistence, PWD_EN, PWD_ALL, DEC_ONLY, Feature Row Lock etc. */
	LSC_READ_FEABITS = 0xFB, /* 24 bits - Read User Feature Bits, such as CFH port and pin persistence, PWD_EN, PWD_ALL, DEC_ONLY, Feature Row Lock etc. */
	LSC_PROG_OTP = 0xF9, /* 24 bits - Program OTP bits, to set Memory Sectors One Time Programmable */
	LSC_READ_OTP = 0xFA, /* 24 bits - Read OTP bits setting */
};


struct device_id_pair {
	const char* device_name;
	uint32_t    device_id;
};

const struct device_id_pair ecp_devices[] =
{
	{"LFE5U-12"   , 0x21111043 },
	{"LFE5U-25"   , 0x41111043 },
	{"LFE5U-45"   , 0x41112043 },
	{"LFE5U-85"   , 0x41113043 },
	{"LFE5UM-25"  , 0x01111043 },
	{"LFE5UM-45"  , 0x01112043 },
	{"LFE5UM-85"  , 0x01113043 },
	{"LFE5UM5G-25", 0x81111043 },
	{"LFE5UM5G-45", 0x81112043 },
	{"LFE5UM5G-85", 0x81113043 }
};

const struct device_id_pair nx_devices[] =
{
	/* Crosslink NX */
	{"LIFCL-17",    0x010F0043 },
	{"LIFCL-40-ES", 0x010F1043 },
	{"LIFCL-40",    0x110F1043 },
	/* Certus NX */
	{"LFD2NX-17",   0x310F0043 },
	{"LFD2NX-40",   0x310F1043 },
	/* Certus Pro NX */
	{"LFCPNX-100",  0x010F4043 },
};
