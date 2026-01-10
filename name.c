

#include "usb_names.h"

#define MIDI_NAME {'P', 'C', '-', 'M', 'I', 'D', 'I', ' ', 'S', 't', 'u', 'd', 'i', 'o'}
#define MIDI_NAME_LEN 14

struct usb_string_descriptor_struct usb_string_product_name = {2 + MIDI_NAME_LEN * 2, 3, MIDI_NAME};