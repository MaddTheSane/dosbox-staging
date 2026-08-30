/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  Copyright (C) 2020-2021  The DOSBox Staging Team
 *  Copyright (C) 2002-2021  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "midi.h"

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <string>
#include <algorithm>

#include "SDL.h"

#include "cross.h"
#include "hardware.h"
#include "mapper.h"
#include "midi_handler.h"
#include "pic.h"
#include "programs.h"
#include "setup.h"
#include "support.h"
#include "timer.h"

//--Added 2011-09-25 by Alun Bestor to let Boxer hook into MIDI messaging
#include "BXCoalfaceAudio.h"
//--End of modifications

#define RAWBUF	1024

uint8_t MIDI_evt_len[256] = {
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x00
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x10
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x20
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x30
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x40
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x50
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x60
  0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,  // 0x70

  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0x80
  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0x90
  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0xa0
  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0xb0

  2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,2,  // 0xc0
  2,2,2,2, 2,2,2,2, 2,2,2,2, 2,2,2,2,  // 0xd0

  3,3,3,3, 3,3,3,3, 3,3,3,3, 3,3,3,3,  // 0xe0

  0,2,3,2, 0,0,1,0, 1,0,1,1, 1,0,1,0   // 0xf0
};

MidiHandler * handler_list = 0;

MidiHandler::MidiHandler() : next(handler_list)
{
	handler_list = this;
}

MidiHandler Midi_none;

//--Disabled 2011-09-25 by Alun Bestor: all MIDI handling is now done by Boxer
/* Include different midi drivers, lowest ones get checked first for default.
   Each header provides an independent midi interface. */
//
//#include "midi_fluidsynth.h"
//#include "midi_mt32.h"
//
//#if defined(MACOSX)
//
//#include "midi_coremidi.h"
//#include "midi_coreaudio.h"
//
//#elif defined(WIN32)
//
//#include "midi_win32.h"
//
//#else
//
//#include "midi_oss.h"
//
//MidiHandler_oss Midi_oss;
//
//#endif
//
//#include "midi_alsa.h"
//--End of modifications

#if C_ALSA
MidiHandler_alsa Midi_alsa;
#endif

struct DB_Midi {
	uint8_t status;
	size_t cmd_len;
	size_t cmd_pos;
	uint8_t cmd_buf[8];
	uint8_t rt_buf[8];
	struct {
		uint8_t buf[MIDI_SYSEX_SIZE];
		size_t used;
		int delay; // ms
		int64_t start;  // ms
	} sysex;
	bool available;
	MidiHandler * handler;
};

DB_Midi midi;

/* When using a physical Roland MT-32 rev. 0 as MIDI output device,
 * some games may require a delay in order to prevent buffer overflow
 * issues.
 *
 * Explanation for this formula can be found in discussion under patch
 * that introduced it: https://sourceforge.net/p/dosbox/patches/241/
 */
int delay_in_ms(size_t sysex_bytes_num)
{
	constexpr double midi_baud_rate = 3.125; // bytes per ms
	const auto delay = (sysex_bytes_num * 1.25) / midi_baud_rate;
	return static_cast<int>(delay) + 2;
}

void MIDI_RawOutByte(uint8_t data)
{
	if (midi.sysex.start) {
		const auto passed_ticks = GetTicksSince(midi.sysex.start);
		if (passed_ticks < midi.sysex.delay)
			Delay(midi.sysex.delay - passed_ticks);
	}

	/* Test for a realtime MIDI message */
	if (data>=0xf8) {
		midi.rt_buf[0]=data;
		//--Replaced 2011-09-25 by Alun Bestor to pass messages on to our own MIDI handling
		//midi.handler->PlayMsg(midi.rt_buf);
		boxer_sendMIDIMessage(midi.rt_buf);
		//--End of modifications
		return;
	}
	/* Test for a active sysex tranfer */
	if (midi.status==0xf0) {
		if (!(data&0x80)) {
			if (midi.sysex.used < (MIDI_SYSEX_SIZE - 1))
				midi.sysex.buf[midi.sysex.used++] = data;
			return;
		} else {
			midi.sysex.buf[midi.sysex.used++] = 0xf7;

			if ((midi.sysex.start) && (midi.sysex.used >= 4) && (midi.sysex.used <= 9) && (midi.sysex.buf[1] == 0x41) && (midi.sysex.buf[3] == 0x16)) {
				LOG(LOG_ALL,LOG_ERROR)("MIDI:Skipping invalid MT-32 SysEx midi message (too short to contain a checksum)");
			} else {
//				LOG(LOG_ALL,LOG_NORMAL)("Play sysex; address:%02X %02X %02X, length:%4d, delay:%3d", midi.sysex.buf[5], midi.sysex.buf[6], midi.sysex.buf[7], midi.sysex.used, midi.sysex.delay);
				//--Replaced 2011-09-25 by Alun Bestor to pass messages on to our own MIDI handling
				//midi.handler->PlaySysex(midi.sysex.buf, midi.sysex.used);
				boxer_sendMIDISysex(midi.sysex.buf, midi.sysex.used);
				//--End of modifications
				if (midi.sysex.start) {
					if (midi.sysex.buf[5] == 0x7F) {
						midi.sysex.delay = 290; // All Parameters reset
					} else if (midi.sysex.buf[5] == 0x10 && midi.sysex.buf[6] == 0x00 && midi.sysex.buf[7] == 0x04) {
						midi.sysex.delay = 145; // Viking Child
					} else if (midi.sysex.buf[5] == 0x10 && midi.sysex.buf[6] == 0x00 && midi.sysex.buf[7] == 0x01) {
						midi.sysex.delay = 30; // Dark Sun 1
					} else {
						midi.sysex.delay = delay_in_ms(midi.sysex.used);
						//--Added 2011-04-20 by Alun Bestor as a quick fix for Colonel's Bequest,
						//which is very time-sensitive and sends way too many sysex messages to fix one-by-one
						if (midi.sysex.delay < 40) midi.sysex.delay = 40;
						//--End of modifications
					}
					midi.sysex.start = GetTicks();
				}
			}

			LOG(LOG_ALL,LOG_NORMAL)("Sysex message size %d", static_cast<int>(midi.sysex.used));
			if (CaptureState & CAPTURE_MIDI) {
				CAPTURE_AddMidi( true, midi.sysex.used-1, &midi.sysex.buf[1]);
			}
		}
	}
	if (data&0x80) {
		midi.status=data;
		midi.cmd_pos=0;
		midi.cmd_len=MIDI_evt_len[data];
		if (midi.status==0xf0) {
			midi.sysex.buf[0]=0xf0;
			midi.sysex.used=1;
		}
	}
	if (midi.cmd_len) {
		midi.cmd_buf[midi.cmd_pos++]=data;
		if (midi.cmd_pos >= midi.cmd_len) {
			if (CaptureState & CAPTURE_MIDI) {
				CAPTURE_AddMidi(false, midi.cmd_len, midi.cmd_buf);
			}
			//--Replaced 2011-09-25 by Alun Bestor to pass messages on to our own MIDI handling
			//midi.handler->PlayMsg(midi.cmd_buf);
			boxer_sendMIDIMessage(midi.cmd_buf);
			//--End of modifications
			midi.cmd_pos=1;		//Use Running status
		}
	}
}

//--Disabled 2011-09-25 by Alun Bestor to let Boxer field such questions itself
//bool MIDI_Available(void)  {
//    return midi.available;
//}
//--End of modifications

class MIDI final : public Module_base {
public:
	MIDI(Section *configuration) : Module_base(configuration)
	{
		using namespace std::string_literals;

		Section_prop * section=static_cast<Section_prop *>(configuration);
		std::string dev = section->Get_string("mididevice");
		lowcase(dev);

		std::string fullconf=section->Get_string("midiconfig");
		//--Added 2011-09-25 by Alun Bestor to let Boxer pick up on the suggested MIDI device
		boxer_suggestMIDIHandler(dev, fullconf.c_str());
		//--End of modifications
		/* If device = "default" go for first handler that works */
		MidiHandler * handler;
        //Disabled 2011-09-30 by Alun Bestor: Boxer now handles sysex delays itself
		/*
		midi.sysex.delay = 0;
		midi.sysex.start = 0;
		if (fullconf.find("delaysysex") != std::string::npos) {
			midi.sysex.start = GetTicks();
			fullconf.erase(fullconf.find("delaysysex"));
			LOG_MSG("MIDI: Using delayed SysEx processing");
		}
		 */
		trim(fullconf);
		const char * conf = fullconf.c_str();
		midi.status=0x00;
		midi.cmd_pos=0;
		midi.cmd_len=0;
		// Value "default" exists for backwards-compatibility.
		// TODO: Rewrite this logic without using goto
		//--Modified 2011-09-25 by Alun Bestor: DOSBox's MIDI handlers are all disabled,
		//so skip straight to the 'none' handler.
		goto getdefault;
		//--End of modifications
		if (dev == "auto" || dev == "default")
			goto getdefault;
		handler=handler_list;
		while (handler) {
			if (dev == handler->GetName()) {
				if (!handler->Open(conf)) {
					LOG_MSG("MIDI: Can't open device: %s with config: '%s'",
					        dev.c_str(), conf);
					goto getdefault;
				}
				midi.handler=handler;
				midi.available=true;
				LOG_MSG("MIDI: Opened device: %s",
				        handler->GetName());
				return;
			}
			handler=handler->next;
		}
		LOG_MSG("MIDI: Can't find device: %s, using default handler.",
		        dev.c_str());
getdefault:
		for (handler = handler_list; handler; handler = handler->next) {
			const std::string name = handler->GetName();
			if (name == "fluidsynth") {
				// Never select fluidsynth automatically.
				// Users needs to opt-in, otherwise
				// fluidsynth will slow down emulator
				// startup for all games.
				continue;
			}
			if (name == "mt32") {
				// Never select mt32 automatically.
				// Users needs to opt-in.
				continue;
			}
			if (handler->Open(conf)) {
				midi.available=true;
				midi.handler=handler;
				LOG_MSG("MIDI: Opened device: %s", name.c_str());
				return;
			}
		}
		assert((handler != nullptr) && (handler->GetName() == "none"s));
	}

	~MIDI(){
		if(midi.available) midi.handler->Close();
		midi.available = false;
		midi.handler = 0;
	}
};

void MIDI_ListAll(Program *caller)
{
	for (auto *handler = handler_list; handler; handler = handler->next) {
		const std::string name = handler->GetName();
		if (name == "none")
			continue;

		caller->WriteOut("%s:\n", name.c_str());

		const auto err = handler->ListAll(caller);
		if (err == MIDI_RC::ERR_DEVICE_NOT_CONFIGURED)
			caller->WriteOut("  device not configured\n");
		if (err == MIDI_RC::ERR_DEVICE_LIST_NOT_SUPPORTED)
			caller->WriteOut("  listing not supported\n");

		caller->WriteOut("\n"); // additional newline to separate devices
	}
}

static MIDI* test;
void MIDI_Destroy(Section* /*sec*/){
	delete test;
}
void MIDI_Init(Section * sec) {
	test = new MIDI(sec);
	sec->AddDestroyFunction(&MIDI_Destroy,true);
}
