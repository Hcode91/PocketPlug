#include <stdbool.h>
#include <stdlib.h>
#include "utils.h"
#include "keyboard.h"

// Enable/disable KBC debugging
#define kbc_debug false

/**
 * List of special key codes
 */
enum {
	KEY_ALL_UP				= 0x40,		///< All keys up
	KEY_LIST_END			= 0x80,		///< End of key code list
	KEY_BEGIN_MOUSE			= 0xCE,		///< Mouse data follows (sys/mouse.h states 0xCE, with 0xCF as mouse lost)
	KEY_BEGIN_KEYBOARD		= 0xDF,		///< Keyboard data follows
};

/**
 * List of keyboard commands
 */
enum {
	KEY_CMD_RESET			= 0x92,		///< Reset keyboard
	KEY_CMD_CAPSLED_OFF		= 0xB0,		///< Caps Lock LED off
	KEY_CMD_CAPSLED_ON		= 0xB1,		///< Caps Lock LED on
	KEY_CMD_NUMLED_OFF		= 0xA0,		///< Num Lock LED off
	KEY_CMD_NUMLED_ON		= 0xA1,		///< Num Lock LED on
	KEY_CMD_MOUSE_ENABLE	= 0xD0,		///< Enable mouse
	KEY_CMD_MOUSE_DISABLE	= 0xD1		///< Disable mouse
};

void keyboard_init(KEYBOARD_STATE *ks)
{
	// Set all key states to "not pressed"
	for (int i=0; i<(sizeof(ks->keystate)/sizeof(ks->keystate[0])); i++) {
		ks->keystate[i] = 0;
	}

	// Reset the R/W pointers and length
	ks->readp = ks->writep = ks->buflen = 0;

	// Clear the update flag
	ks->update_flag = false;

	ks->mouse_enabled = 0;
	ks->lastdata_mouse = 0;
}

// PocketPlug: a key of the 3B1 keyboard (scancode 0-0x7f) went down or up
void keyboard_key(KEYBOARD_STATE *ks, int scancode, bool down)
{
	if (scancode < 0 || scancode >= 0x80) return;
	ks->keystate[scancode] = down ? 1 : 0;
	ks->update_flag = true;
}

bool mouse_event(KEYBOARD_STATE *ks, int dx, int dy, int db)
{
	uint8_t flags = 0, xbyte, ybyte;

	if (!ks->mouse_enabled){
		return (1);
	}
	ks->buffer[ks->writep] = KEY_BEGIN_MOUSE;
	ks->writep = (ks->writep + 1) % KEYBOARD_BUFFER_SIZE;
	if (ks->buflen < KEYBOARD_BUFFER_SIZE) ks->buflen++;

	/* The first byte of mouse data contains the signs of X and Y as well as the
	 * buttons (strangely, the signs of X and Y are opposite)*/
	if (dx > 0){
		flags |= 0x10;
	}
	if (dy < 0){
		flags |= 0x08;
	}

	flags |= db;

	ks->buffer[ks->writep] = flags;
	ks->writep = (ks->writep + 1) % KEYBOARD_BUFFER_SIZE;
	if (ks->buflen < KEYBOARD_BUFFER_SIZE) ks->buflen++;

	/* Second and third bytes are X and Y deltas */
	xbyte = (abs(dx) > 127) ? 127 : abs(dx);
	ks->buffer[ks->writep] = xbyte;
	ks->writep = (ks->writep + 1) % KEYBOARD_BUFFER_SIZE;
	if (ks->buflen < KEYBOARD_BUFFER_SIZE) ks->buflen++;

	ybyte = (abs(dy) > 127) ? 127 : abs(dy);
	ybyte |= 0x80;
	ks->buffer[ks->writep] = ybyte;
	ks->writep = (ks->writep + 1) % KEYBOARD_BUFFER_SIZE;
	if (ks->buflen < KEYBOARD_BUFFER_SIZE) ks->buflen++;

	// need to double send if KEY_BEGIN_KEYBOARD value ever encountered
	if (ybyte == KEY_BEGIN_KEYBOARD) {
		ks->buffer[ks->writep] = ybyte;
		ks->writep = (ks->writep + 1) % KEYBOARD_BUFFER_SIZE;
		if (ks->buflen < KEYBOARD_BUFFER_SIZE) ks->buflen++;
	}

	ks->lastdata_mouse = 1;
	return 1;
}



void keyboard_scan(KEYBOARD_STATE *ks)
{
	int nkeys = 0;

	// Skip doing the scan if the keyboard hasn't changed state
	if (!ks->update_flag) return;

	if (ks->lastdata_mouse){
		//Keyboard Data Begins Here (BEGKBD)
		//This is only supposed to be sent if the last data sent was from the
		//mouse (sending it otherwise breaks the keyboard)*/
		ks->buffer[ks->writep] = KEY_BEGIN_KEYBOARD;
		ks->writep = (ks->writep + 1) % KEYBOARD_BUFFER_SIZE;
		if (ks->buflen < KEYBOARD_BUFFER_SIZE) ks->buflen++;
		ks->lastdata_mouse = 0;
	}

	for (int i=0; i<(sizeof(ks->keystate)/sizeof(ks->keystate[0])); i++) {
		if (ks->keystate[i]) {
			LOG_IF(kbc_debug, "KBC KEY DOWN: %d\n", i);
			ks->buffer[ks->writep] = i;
			ks->writep = (ks->writep + 1) % KEYBOARD_BUFFER_SIZE;
			if (ks->buflen < KEYBOARD_BUFFER_SIZE) ks->buflen++;
			nkeys++;
		}
	}
	if (nkeys) {
		if (ks->writep == 0) {
			ks->buffer[KEYBOARD_BUFFER_SIZE-1] |= KEY_LIST_END;
		} else {
			ks->buffer[ks->writep - 1] |= KEY_LIST_END;
		}
	}else{
		// If no keys down, then send All Keys Up byte
		LOG_IFS(kbc_debug, "KBC ALL KEYS UP\n");
		ks->buffer[ks->writep] = KEY_ALL_UP;
		ks->writep = (ks->writep + 1) % KEYBOARD_BUFFER_SIZE;
		if (ks->buflen < KEYBOARD_BUFFER_SIZE) ks->buflen++;
	}

	// Clear the update flag
	ks->update_flag = false;
}

bool keyboard_get_irq(KEYBOARD_STATE *ks)
{
	bool irq_status = false;

	// Conditions which may cause an IRQ :-
	//   Read Data Reg has data and RxIRQ enabled
	if (ks->rxie)
		if (ks->buflen > 0) irq_status = true;

	//   Transmit Data Reg empty and TxIRQ enabled
//	if (ks->txie)

	//   DCD set and RxIRQ enabled
//

	// returns interrupt status -- i.e. is there data in the buffer?
	return irq_status;
}

uint8_t keyboard_read(KEYBOARD_STATE *ks, uint8_t addr)
{
	if ((addr & 1) == 0) {
		// Status register -- RS=0, read
		uint8_t sr = 0;
		if (ks->buflen > 0) sr |= 1;			// SR0: a new character has been received
		sr |= 2;								// SR1: Transmitter Data Register Empty
//			0 +								// SR2: Data Carrier Detect
//			0 +								// SR3: Clear To Send
//			0 +								// SR4: Framing Error
//			0 +								// SR5: Receiver Overrun
//			0 +								// SR6: Parity Error
		if (keyboard_get_irq(ks)) sr |= 0x80;	// SR7: IRQ status
		//LOG_IF(kbc_debug, "KBC DBG: sr=%02X\n", sr);
		return sr;
	} else {
		// return data, pop off the fifo
		uint8_t x = ks->buffer[ks->readp];
		ks->readp = (ks->readp + 1) % KEYBOARD_BUFFER_SIZE;
		if (ks->buflen > 0) ks->buflen--;
		//LOG_IF(kbc_debug, "\tKBC DBG: rxd=%02X\n", x);
		return x;
	}
}

void keyboard_write(KEYBOARD_STATE *ks, uint8_t addr, uint8_t val)
{
	if ((addr & 1) == 0) {
		// write to control register
		// transmit intr enabled when CR6,5 = 01
		// receive intr enabled when CR7 = 1

		// CR0,1 = divider registers. When =11, do a software reset
		if ((val & 3) == 3) {
			ks->readp = ks->writep = ks->buflen = 0;
		}

		// Ignore CR2,3,4 (word length)...

		// CR5,6 = Transmit Mode
		ks->txie = (val & 0x60)==0x20;

		// CR7 = Receive Interrupt Enable
		ks->rxie = (val & 0x80)==0x80;
	} else {
		// Write command to KBC
		if (val == KEY_CMD_RESET) {
			LOG_IFS(kbc_debug, "KBC: KEYBOARD RESET!\n");
			ks->readp = ks->writep = ks->buflen = 0;
		} else if (val == KEY_CMD_MOUSE_ENABLE){
			ks->mouse_enabled = 1;
		} else if (val == KEY_CMD_MOUSE_DISABLE){
			ks->mouse_enabled = 0;
		} else {
			LOG("KBC TODO: write keyboard data 0x%02X\n", val);
		}
	}
}

