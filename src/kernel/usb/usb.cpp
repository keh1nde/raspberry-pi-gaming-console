//
// Created by Kehinde Adeoso on 8/30/26.
//

#include "kernel/usb.h"

#include "lib/circle/interrupt.h"
#include "lib/circle/timer.h"
#include "lib/circle/types.h"
#include "lib/circle/usb/xhcidevice.h"

void usb_init() {
	const auto interrupt = new CInterruptSystem();
	const auto timer = new CTimer();

	const auto dev = new CXHCIDevice(
		interrupt,
		timer,
		FALSE,
		0,
		0
	);

	const bool ok = dev->Initialize(TRUE);
	
	printf("usb_init: xHCI Initialize() %s\r\n", ok ? "OK" : "FAILED");
	if (ok) {
		dev->DumpStatus();
	}
}

