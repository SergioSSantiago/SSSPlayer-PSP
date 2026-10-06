/* Kernel display driver: brightness / LCD enable (CFW user imports). */
	.set noreorder

#include "pspimport.s"

	IMPORT_START	"sceDisplay_driver",0x00090000
	IMPORT_FUNC	"sceDisplay_driver",0xA7E61E68,sceDisplayDisable
	IMPORT_FUNC	"sceDisplay_driver",0x3F132D43,sceDisplayEnable
	IMPORT_FUNC	"sceDisplay_driver",0xC66D3C9E,sceDisplaySetBrightness
	IMPORT_FUNC	"sceDisplay_driver",0xA8BAC431,sceDisplayGetBrightness
