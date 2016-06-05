/*
 * ExtIO wrapper
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define LIBRTL_EXPORTS 1

#include <stdint.h>
#include <ActiveSocket.h>

#include <Windows.h>
#include <WindowsX.h>
#include <commctrl.h>
#include <process.h>
#include <tchar.h>

#include <new>
#include <stdio.h>

#include "resource.h"
#include "ExtIO_RTL.h"

#ifdef _MSC_VER
	#pragma warning(disable : 4996)
	#define snprintf  _snprintf
#endif


#define MAX_PPM	1000
#define MIN_PPM	-1000

// 225001 - 300000 Hz, 900001 - 3200000 Hz
#define MAXRATE		3200000
#define MINRATE		900001


//#define EXTIO_HWTYPE_16B	3
typedef enum
{
    exthwNone       = 0
  , exthwSDR14      = 1
  , exthwSDRX       = 2
  , exthwUSBdata16  = 3 // the hardware does its own digitization and the audio data are returned to Winrad
                        // via the callback device. Data must be in 16-bit  (short) format, little endian.
                        //   each sample occupies 2 bytes (=16 bits) with values from  -2^15 to +2^15 -1
  , exthwSCdata     = 4 // The audio data are returned via the (S)ound (C)ard managed by Winrad. The external
                        // hardware just controls the LO, and possibly a preselector, under DLL control.
  , exthwUSBdata24  = 5 // the hardware does its own digitization and the audio data are returned to Winrad
                        // via the callback device. Data are in 24-bit  integer format, little endian.
                        //   each sample just occupies 3 bytes (=24 bits) with values from -2^23 to +2^23 -1
  , exthwUSBdata32  = 6 // the hardware does its own digitization and the audio data are returned to Winrad
                        // via the callback device. Data are in 32-bit  integer format, little endian.
                        //   each sample occupies 4 bytes (=32 bits) but with values from  -2^23 to +2^23 -1
  , exthwUSBfloat32 = 7 // the hardware does its own digitization and the audio data are returned to Winrad
                        // via the callback device. Data are in 32-bit  float format, little endian.
  , exthwHPSDR      = 8 // for HPSDR only!

  // HDSDR > 2.70
  , exthwUSBdataU8  = 9 // the hardware does its own digitization and the audio data are returned to Winrad
                        // via the callback device. Data must be in 8-bit  (unsigned) format, little endian.
                        //   intended for RTL2832U based DVB-T USB sticks
                        //   each sample occupies 1 byte (=8 bit) with values from 0 to 255
  , exthwUSBdataS8  = 10// the hardware does its own digitization and the audio data are returned to Winrad
                        // via the callback device. Data must be in 8-bit  (signed) format, little endian.
                        //   each sample occupies 1 byte (=8 bit) with values from -128 to 127
  , exthwFullPCM32  = 11 // the hardware does its own digitization and the audio data are returned to Winrad
                        // via the callback device. Data are in 32-bit  integer format, little endian.
                        //   each sample occupies 4 bytes (=32 bits) with full range: from  -2^31 to +2^31 -1
} extHWtypeT;
extHWtypeT extHWtype = exthwUSBdata16;  /* default ExtIO type 16-bit samples */

typedef enum
{
    extSDR_NoInfo                 = 0   // sign SDR features would be signed with subsequent calls
  , extSDR_supports_Settings      = 1
  , extSDR_supports_Atten         = 2   // RF Attenuation / Gain may be set via pfnSetAttenuator()
  , extSDR_supports_TX            = 3   // pfnSetModeRxTx() may be called
  , extSDR_controls_BP            = 4   // pfnDeactivateBP() may be called
  , extSDR_supports_AGC           = 5   // pfnExtIoSetAGC() may be called
  , extSDR_supports_MGC           = 6   // IF Attenuation / Gain may be set via pfnExtIoSetMGC()
  , extSDR_supports_PCMU8         = 7   // exthwUSBdataU8 is supported
  , extSDR_supports_PCMS8         = 8   // exthwUSBdataS8 is supported
  , extSDR_supports_PCM32         = 9   // exthwFullPCM32 is supported
} extSDR_InfoT;


const char * TunerName[] = { "None", "E4000", "FC0012", "FC0013", "FC2580", "R820T", "R828D" };
static const int n_tuners = sizeof(TunerName) / sizeof(TunerName[0]);


const int e4k_gains[] =
{ -10, 15, 40, 65, 90, 115, 140, 165, 190, 215, 240, 290, 340, 420 };

const int fc12_gains[] = { -99, -40, 71, 179, 192 };

const int fc13_gains[] =
{ -99, -73, -65, -63, -60, -58, -54, 58, 61, 63, 65, 67, 68, 70, 71, 179, 181, 182, 184, 186, 188, 191, 197 };

const int r820t_gains[] =
{ 0, 9, 14, 27, 37, 77, 87, 125, 144, 157, 166, 197, 207, 229, 254, 280, 297, 328, 338, 364, 372, 386, 402, 421, 434, 439, 445, 480, 496 };


// mix: 1900, 2300, 2700, 3300, 3400,
// rc: 1000, 1200, 1800, 2600, 3400,
// if channel: 2150, 2200, 2240, 2280, 2300, 2400, 2450, 2500, 2550, 2600, 2700, 2750, 2800, 2900, 2950, 3000, 3100, 3200, 3300, 3400
const int e4k_bws[] =
{ 0, 1000, 1200, 1800, 1900, 2150, 2200, 2300, 2400, 2500, 2600, 2700, 2800, 2900, 3000, 3100, 3200, 3300, 3400,   5000, 10000 };

const int r820_bws[] =
{ 0, 350, 450, 550, 700, 900, 1200, 1450, 1550, 1600, 1700, 1800, 1900, 1950, 2050, 2080, 2180, 2280, 2330, 2430, 6000, 7000, 8000 };


struct tuner_gain_t
{
	const int * gain;	// 0.1 dB steps: gain in dB = gain[] / 10
	const int num;
}
tuner_gains[]
{
  { 0, 0 }	// tuner_type: E4000 =1, FC0012 =2, FC0013 =3, FC2580 =4, R820T =5, R828D =6
, { e4k_gains, sizeof(e4k_gains) / sizeof(e4k_gains[0]) }
, { fc12_gains, sizeof(fc12_gains) / sizeof(fc12_gains[0]) }
, { fc13_gains, sizeof(fc13_gains) / sizeof(fc13_gains[0]) }
, { 0, 0 }	// FC2580
, { r820t_gains, sizeof(r820t_gains) / sizeof(r820t_gains[0]) }
, { 0, 0 }	// R828D
};

struct tuner_bw_t
{
	const int * bw;	// bw in kHz: bw in Hz = bw[] * 1000
	const int num;
}
tuner_bws[]
{
  { 0, 0 }	// tuner_type: E4000 =1, FC0012 =2, FC0013 =3, FC2580 =4, R820T =5, R828D =6
, { e4k_bws, sizeof(e4k_bws) / sizeof(e4k_bws[0]) }
, { 0, 0 }	// FC0012
, { 0, 0 }	// FC0013
, { 0, 0 }	// FC2580
, { r820_bws, sizeof(r820_bws) / sizeof(r820_bws[0]) }
, { 0, 0 }	// R828D
};



typedef struct sr {
	double value;
	TCHAR *name;
	int    valueInt;
} sr_t;


static sr_t samplerates[] = {
#if 0
	{ 225001.0, TEXT("0.23 Msps"), 225001 },			// [0]
	{ 250000.0, TEXT("0.25 Msps"), 250000 },			// [1]
	{ 264600.0, TEXT("0.26 Msps (44.1 kHz)"), 264600 },	// [2]
	{ 288000.0, TEXT("0.29 Msps (48.0 kHz)"), 288000 },	// [3]
	{ 300000.0, TEXT("0.3 Msps"), 300000 },				// [4]
#endif

	{  960000.0, TEXT("0.96 Msps (48.0 kHz)"),  960000 },	// = 5 * 192 kHz		[1]
	{ 1058400.0, TEXT("1.06 Msps (44.1 kHz)"), 1058400 },	// = 6 * 176.4 kHz		[2]
	{ 1152000.0, TEXT("1.15 Msps (48.0 kHz)"), 1152000 },	// = 6 * 192 kHz		[3]

	//{ 1200000.0, TEXT("1.20 Msps"),  1200000 },
	{ 1234800.0, TEXT("1.23 Msps (44.1 kHz)"), 1234800 },	// = 7 * 176.4 kHz		[4]
	{ 1344000.0, TEXT("1.34 Msps (48.0 kHz)"), 1344000 },	// = 7 * 192 kHz		[5]

	{ 1411200.0, TEXT("1.41 Msps (44.1 kHz)"), 1411200 },	// = 8 * 176.4 kHz		[6]
	{ 1536000.0, TEXT("1.54 Msps (48.0 kHz)"), 1536000 },	// = 8 * 192 kHz		[7]

	{ 1764000.0, TEXT("1.76 Msps (44.1 kHz)"), 1764000 },	// = 10 * 176.4 kHz		[8]
	//{ 1800000.0, TEXT("1.8 Msps"), 1800000 },
	{ 1920000.0, TEXT("1.92 Msps (48.0 kHz)"), 1920000 },	// = 10 * 192 kHz		[9]

	{ 2116800.0, TEXT("2.12 Msps (44.1 kHz)"), 2116800 },	// = 12 * 176.4 kHz		[10]
	{ 2304000.0, TEXT("2.30 Msps (48.0 kHz)"), 2304000 },	// = 12 * 192 kHz		[11]
	//{ 2400000.0, TEXT("2.4 Msps"),  2400000 },

	{ 2469600.0, TEXT("2.47 Msps (44.1 kHz)"), 2469600 },	// = 14 * 176.4 kHz		[12]
	{ 2688000.0, TEXT("2.69 Msps (48.0 kHz)"), 2688000 },	// = 14 * 192 kHz		[13]

	{ 2646000.0, TEXT("2.65 Msps (44.1 kHz)"), 2646000 },	// 15 * 176.4 kHz		[14]
	{ 2822400.0, TEXT("2.82 Msps (44.1 kHz)"), 2822400 },	// 16 * 176.4 kHz		[15]

	{ 2880000.0, TEXT("2.88 Msps (48.0 kHz)"), 2880000 },	// 15 * 192 kHz			[16]
#if 0
	{ 3072000.0, TEXT("3.07 Msps (48.0 kHz)"), 3072000 },	// 16 * 192 kHz			[17]
	{ 3090000.0, TEXT("3.09 Msps"), 3090000 },
	{ 3100000.0, TEXT("3.1 Msps"), 3100000 },
#endif

	{ 3200000.0, TEXT("3.2 Msps"), 3200000 }
};

static const int n_srates = sizeof(samplerates) / sizeof(samplerates[0]);


static TCHAR* directS[] = {
	TEXT("disabled: tuner I/Q"),
	TEXT("direct sampling I"),
	TEXT("direct sampling Q")
};


static const int * bandwidths = 0;
static int n_bandwidths = 0;			// tuner_bws[]

static const int * gains = 0;
static int n_gains = 0;			// tuner_gains[]


static union
{
	uint8_t ac[8];			// write 0, 1, 2, [3]
	uint32_t ui[2];			// write 0, [1]
} rtl_tcp_cmd;

static volatile union
{
	int8_t		ac[12];		// 0 .. 3 == "RTL0"
	uint32_t	ui[3];		// [1] = tuner_type; [2] = tuner_gain_count
	// tuner_type: E4000 =1, FC0012 =2, FC0013 =3, FC2580 =4, R820T =5, R828D =6
} rtl_tcp_dongle_info;

static volatile uint32_t	tunerNo = 0;
static volatile uint32_t	numTunerGains = 0;

static volatile bool GotTunerInfo = false;

static int buffer_sizes[] = { //in kBytes
	1, 2, 4, 8, 16, 32, 64, 128, 256
};

#define MAX_BUFFER_LEN	(256*1024)
#define NUM_BUFFERS_BEFORE_CALLBACK		4

static bool rcvBufsAllocated = false;
static short * short_buf = 0;
static uint8_t * rcvBuf[NUM_BUFFERS_BEFORE_CALLBACK + 1] = { 0 };


static volatile int somewhat_changed = 0;	// 1 == freq
											// 2 == srate
											// 4 == gain
											// 8 == tuner agc
											// 16 == rtl agc
											// 32 == direct sampling mode
											// 64 == offset Tuning (E4000)
											// 128 == freq corr ppm
											// 256 == tuner bandwidth

static volatile long last_freq=100000000;
static volatile long new_freq = 100000000;

static volatile int last_srate_idx = 4;
static volatile int new_srate_idx = 4;

static volatile int last_TunerBW = 0;		// 0 == automatic, sonst in Hz
static volatile int new_TunerBW = 0;		// n_bandwidths = bandwidths[]; nearestBwIdx()

static volatile int last_gain = 1;
static volatile int new_gain = 1;

static volatile int last_TunerAGC = 1;	// 0 == off/manual, 1 == on/automatic
static volatile int new_TunerAGC = 1;

static volatile int last_RTLAGC = 0;
static volatile int new_RTLAGC = 0;

static volatile int last_DirectSampling = 0;
static volatile int new_DirectSampling = 0;

static volatile int last_OffsetTuning = 0;
static volatile int new_OffsetTuning = 0;

static volatile int last_FreqCorrPPM = 0;
static volatile int new_FreqCorrPPM = 0;

static volatile int bufferSizeIdx = 6;// 64 kBytes
static volatile int buffer_len = buffer_sizes[bufferSizeIdx];


static char RTL_TCP_IPAddr[32] = "127.0.0.1";
static int RTL_TCP_PortNo = 1234;

static volatile int AutoReConnect = 1;
static volatile int PersistentConnection = 1;

static int ASyncConnection = 1;
static int SleepMillisWaitingForData = 1;

static int HDSDR_AGC=2;


typedef struct {
	char vendor[256], product[256], serial[256], name[256];
} device;

device connected_device;

// Thread handle
volatile bool terminateThread = false;
volatile bool ThreadStreamToSDR = false;
volatile bool commandEverything = true;
static bool GUIDebugConnection = false;
static volatile HANDLE worker_handle=INVALID_HANDLE_VALUE;
void ThreadProc(void * param);


int Start_Thread();
int Stop_Thread();

/* ExtIO Callback */
void (* WinradCallBack)(int, int, float, void *) = NULL;
#define WINRAD_SRCHANGE 100
#define WINRAD_LOCHANGE 101
#define WINRAD_ATTCHANGE 125


static INT_PTR CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
static HWND h_dialog=NULL;


static bool transmitTcpCmd(CActiveSocket &conn, uint8_t cmdId, uint32_t value)
{
	rtl_tcp_cmd.ac[3] = cmdId;
	rtl_tcp_cmd.ui[1] = htonl(value);
	int iSent = conn.Send(&rtl_tcp_cmd.ac[3], 5);
	return (5 == iSent);
}

static int nearestSrateIdx(int srate)
{
	if (srate <= 0)
		return 0;
	else if (srate <= samplerates[1].valueInt)
		return 1;
	else if (srate >= samplerates[n_srates - 1].valueInt)
		return n_srates - 1;

	int nearest_idx = 1;
	int nearest_dist = 10000000;
	for (int idx = 0; idx < n_srates; ++idx)
	{
		int dist = abs(srate - samplerates[idx].valueInt);
		if (dist < nearest_dist)
		{
			nearest_idx = idx;
			nearest_dist = dist;
		}
	}
	return nearest_idx;
}

static int nearestBwIdx(int bw)
{
	if (bw <= 0 || n_bandwidths <= 0)
		return 0;
	else if (bw <= bandwidths[1])
		return 1;
	else if (bw >= bandwidths[n_bandwidths - 1])
		return n_bandwidths - 1;

	int nearest_idx = 1;
	int nearest_dist = 10000000;
	for (int idx = 1; idx < n_bandwidths; ++idx)
	{
		int dist = abs(bw - bandwidths[idx]);
		if (dist < nearest_dist)
		{
			nearest_idx = idx;
			nearest_dist = dist;
		}
	}
	return nearest_idx;
}

static int nearestGainIdx(int gain)
{
	if (n_gains <= 0)
		return 0;
	else if (gain <= gains[0])
		return 0;
	else if (gain >= gains[n_gains - 1])
		return n_gains - 1;

	int nearest_idx = 0;
	int nearest_dist = 10000000;
	for (int idx = 0; idx < n_gains; ++idx)
	{
		int dist = abs(gain - gains[idx]);
		if (dist < nearest_dist)
		{
			nearest_idx = idx;
			nearest_dist = dist;
		}
	}
	return nearest_idx;
}


extern "C"
bool  LIBRTL_API __stdcall InitHW(char *name, char *model, int& type)
{
	strcpy_s( connected_device.vendor, 256, "Realtek" );
	strcpy_s( connected_device.product,15, "RTL2832" );
	strcpy_s( connected_device.serial, 256, "NA" );
	strcpy_s( connected_device.name,   63, "Realtek RTL2832" );

	strcpy_s(name,63, connected_device.name);
	strcpy_s(model,15,connected_device.product);
	name[63]=0;
	model[15]=0;

	type = extHWtype;
	return TRUE;
}

extern "C"
int LIBRTL_API __stdcall GetStatus()
{
	/* dummy function */
    return 0;
}

extern "C"
bool  LIBRTL_API __stdcall OpenHW()
{
	h_dialog=CreateDialog(hInst, MAKEINTRESOURCE(IDD_RTL_SETTINGS), NULL, (DLGPROC)MainDlgProc);
	if (h_dialog)
		ShowWindow(h_dialog,SW_HIDE);

	if (PersistentConnection)
	{
		ThreadStreamToSDR = false;
		if (Start_Thread() < 0)
			return FALSE;
	}

	return TRUE;
}

extern "C"
long LIBRTL_API __stdcall SetHWLO(long freq)
{
	new_freq = freq;
	somewhat_changed |= 1;
	return 0;
}

extern "C"
int LIBRTL_API __stdcall StartHW(long freq)
{
	commandEverything = true;
	ThreadStreamToSDR = true;
	if ( Start_Thread() < 0 )
		return -1;

    SetHWLO(freq);

	if (h_dialog)
	{
		EnableWindow(GetDlgItem(h_dialog, IDC_IP_PORT), FALSE);
		EnableWindow(GetDlgItem(h_dialog, IDC_DIRECT), FALSE);
	}

	return buffer_len/2;
}

extern "C"
long LIBRTL_API __stdcall GetHWLO()
{
	return new_freq;
}


extern "C"
long LIBRTL_API __stdcall GetHWSR()
{
	long sr = long(samplerates[new_srate_idx].valueInt);
	//::MessageBoxA(NULL, samplerates[new_srate_idx].name, "GetHWSR()", 0);
	return sr;
}

extern "C"
int LIBRTL_API __stdcall ExtIoGetSrates( int srate_idx, double * samplerate )
{
	if (srate_idx < n_srates)
	{
		*samplerate=  samplerates[srate_idx].value;
		return 0;
	}
	return 1;	// ERROR
}

extern "C"
int  LIBRTL_API __stdcall ExtIoGetActualSrateIdx(void)
{
	return new_srate_idx;
}

extern "C"
int  LIBRTL_API __stdcall ExtIoSetSrate( int srate_idx )
{
	if (srate_idx >= 0 && srate_idx < n_srates)
	{
		new_srate_idx = srate_idx;
		somewhat_changed |= 2;
		if (h_dialog)
			ComboBox_SetCurSel(GetDlgItem(h_dialog,IDC_SAMPLERATE),srate_idx);
		WinradCallBack(-1,WINRAD_SRCHANGE,0,NULL);// Signal application
		return 0;
	}
	return 1;	// ERROR
}

extern "C"
int  LIBRTL_API __stdcall GetAttenuators( int atten_idx, float * attenuation )
{
	if ( atten_idx < n_gains )
	{
		*attenuation= gains[atten_idx]/10.0F;
		return 0;
	}
	return 1;	// End or Error
}

extern "C"
int  LIBRTL_API __stdcall GetActualAttIdx(void)
{
	for (int i=0;i<n_gains;i++)
		if (new_gain==gains[i])
			return i;
	return -1;
}

extern "C"
int  LIBRTL_API __stdcall SetAttenuator( int atten_idx )
{
	if ( atten_idx<0 || atten_idx > n_gains )
		return -1;

	int pos=gains[atten_idx];

	if (h_dialog)
	{
		SendMessage(GetDlgItem(h_dialog, IDC_GAIN), TBM_SETPOS, (WPARAM)TRUE, (LPARAM)-pos);

		if (Button_GetCheck(GetDlgItem(h_dialog, IDC_TUNERAGC)) == BST_UNCHECKED)
		{
			TCHAR str[255];
			_stprintf_s(str, 255, TEXT("%2.1f  dB"), (float)pos / 10);
			Static_SetText(GetDlgItem(h_dialog, IDC_GAINVALUE), str);
			new_gain = pos;
			somewhat_changed |= 4;
		}
	}
	new_gain=pos;
	return 0;
}

extern "C"
int   LIBRTL_API __stdcall ExtIoGetAGCs( int agc_idx, char * text )
{
	switch ( agc_idx )
	{
		case 0:	snprintf( text, 16, "%s", "None" );			return 0;
		case 1:	snprintf( text, 16, "%s", "Tuner AGC" );	return 0;
		case 2:	snprintf( text, 16, "%s", "RTL AGC" );		return 0;
		case 3:	snprintf( text, 16, "%s", "RTL+Tuner AGC" );	return 0;
		default:	return -1;	// ERROR
	}
	return -1;	// ERROR
}

extern "C"
int   LIBRTL_API __stdcall ExtIoGetActualAGCidx (void)
{
	return HDSDR_AGC;
}

extern "C"
int   LIBRTL_API __stdcall ExtIoSetAGC (int agc_idx)
{
	HDSDR_AGC = agc_idx;
	return 0;
}


extern "C"
int   LIBRTL_API __stdcall ExtIoGetSetting( int idx, char * description, char * value )
{
	switch (idx)
	{
	case 0:
		snprintf(description, 1024, "%s", "RTL_TCP IP-Address");
		snprintf(value, 1024, "%s", RTL_TCP_IPAddr);
		return 0;
	case 1:
		snprintf(description, 1024, "%s", "RTL_TCP Portnumber");
		snprintf(value, 1024, "%d", RTL_TCP_PortNo);
		return 0;
	case 2:
		snprintf(description, 1024, "%s", "Automatic_ReConnect");
		snprintf(value, 1024, "%d", AutoReConnect);
		return 0;
	case 3:
		snprintf(description, 1024, "%s", "Persistent_Connection");
		snprintf(value, 1024, "%d", PersistentConnection);
		return 0;
	case 4:
		snprintf( description, 1024, "%s", "SampleRateIdx" );
		snprintf(value, 1024, "%d", new_srate_idx);
		return 0;
	case 5:
		snprintf(description, 1024, "%s", "TunerBandwidth in kHz (only few tuner models) - 0 for automatic");
		snprintf(value, 1024, "%d", new_TunerBW);
		return 0;
	case 6:
		snprintf( description, 1024, "%s", "Tuner_AGC" );
		snprintf(value, 1024, "%d", new_TunerAGC);
		return 0;
	case 7:
		snprintf( description, 1024, "%s", "RTL_AGC" );
		snprintf(value, 1024, "%d", new_RTLAGC);
		return 0;
	case 8:
		snprintf( description, 1024, "%s", "Frequency_Correction" );
		snprintf(value, 1024, "%d", new_FreqCorrPPM);
		return 0;
	case 9:
		snprintf( description, 1024, "%s", "Tuner_Gain" );
		snprintf(value, 1024, "%d", new_gain);
		return 0;
	case 10:
		snprintf( description, 1024, "%s", "Buffer_Size" );
		snprintf(value, 1024, "%d", bufferSizeIdx);
		return 0;
	case 11:
		snprintf( description, 1024, "%s", "Offset_Tuning" );
		snprintf(value, 1024, "%d", new_OffsetTuning);
		return 0;
	case 12:
		snprintf( description, 1024, "%s", "Direct_Sampling" );
		snprintf(value, 1024, "%d", new_DirectSampling);
		return 0;
	case 13:
		snprintf(description, 1024, "%s", "Use Asynchronous I/O on Socket connection");
		snprintf(value, 1024, "%d", ASyncConnection);
		return 0;
	case 14:
		snprintf(description, 1024, "%s", "number of Milliseconds to Sleep before trying to receive new data");
		snprintf(value, 1024, "%d", SleepMillisWaitingForData);
		return 0;
	default:
		return -1;	// ERROR
	}
	return -1;	// ERROR
}

extern "C"
void  LIBRTL_API __stdcall ExtIoSetSetting( int idx, const char * value )
{
	int tempInt;

	switch ( idx )
	{
	case 0:
		snprintf(RTL_TCP_IPAddr, 31, "%s", value);
		return;
	case 1:
		tempInt = atoi(value);
		if (tempInt >= 0 && tempInt < 65536)
			RTL_TCP_PortNo = tempInt;
		return;
	case 2:
		AutoReConnect = atoi(value) ? 1 : 0;
		return;
	case 3:
		PersistentConnection = atoi(value) ? 1 : 0;
		return;
	case 4:
		tempInt = atoi( value );
		if (tempInt >= 0 && tempInt < n_srates)
			new_srate_idx = tempInt;
		return;
	case 5:
		new_TunerBW = atoi(value);
		return;
	case 6:
		new_TunerAGC = atoi(value) ? 1 : 0;
		return;
	case 7:
		new_RTLAGC = atoi(value) ? 1 : 0;
		return;
	case 8:
		tempInt = atoi( value );
		if (  tempInt>MIN_PPM && tempInt < MAX_PPM )
			new_FreqCorrPPM = tempInt;
		return;
	case 9:
		new_gain = atoi( value );
		return;
	case 10:
		tempInt = atoi( value );
		if (  tempInt>=0 && tempInt < (sizeof(buffer_sizes)/sizeof(buffer_sizes[0])) )
			bufferSizeIdx = tempInt;
		return;
	case 11:
		new_OffsetTuning = atoi(value) ? 1 : 0;
		return;
	case 12:
		tempInt = atoi( value );
		if (tempInt < 0)	tempInt = 0;	else if (tempInt >2)	tempInt = 2;
		new_DirectSampling  = tempInt;
		break;
	case 13:
		ASyncConnection = atoi(value) ? 1 : 0;
		break;
	case 14:
		SleepMillisWaitingForData = atoi(value);
		if (SleepMillisWaitingForData > 100)
			SleepMillisWaitingForData = 100;
		break;
	}
}


extern "C"
void LIBRTL_API __stdcall StopHW()
{
	ThreadStreamToSDR = false;
	if (!PersistentConnection)
		Stop_Thread();

	if (h_dialog)
	{
		EnableWindow(GetDlgItem(h_dialog, IDC_IP_PORT), TRUE);
		EnableWindow(GetDlgItem(h_dialog, IDC_DIRECT), TRUE);
	}
}

extern "C"
void LIBRTL_API __stdcall CloseHW()
{
	ThreadStreamToSDR = false;
	Stop_Thread();

	if (h_dialog)
		DestroyWindow(h_dialog);
}

extern "C"
void LIBRTL_API __stdcall ShowGUI()
{
	if (h_dialog)
	{
		ShowWindow(h_dialog, SW_SHOW);
		SetForegroundWindow(h_dialog);
	}
}

extern "C"
void LIBRTL_API  __stdcall HideGUI()
{
	if (h_dialog)
		ShowWindow(h_dialog,SW_HIDE);
}

extern "C"
void LIBRTL_API  __stdcall SwitchGUI()
{
	if (h_dialog)
	{
		if (IsWindowVisible(h_dialog))
			ShowWindow(h_dialog, SW_HIDE);
		else
			ShowWindow(h_dialog, SW_SHOW);
	}
}


extern "C"
void LIBRTL_API __stdcall SetCallback(void (* myCallBack)(int, int, float, void *))
{
	WinradCallBack = myCallBack;
}

extern "C"
void LIBRTL_API  __stdcall ExtIoSDRInfo( int extSDRInfo, int additionalValue, void * additionalPtr )
{
	if( extSDRInfo == extSDR_supports_PCMU8 )
    {
		//This versions supports 8bit samples
		extHWtype = exthwUSBdataU8;
    }
	return;
}

int Start_Thread()
{
	//If already running, exit
	if (worker_handle != INVALID_HANDLE_VALUE)
		return 0;			// all fine

	terminateThread = false;
	GotTunerInfo = false;

	if (!rcvBufsAllocated)
	{
		short_buf = new (std::nothrow) short[MAX_BUFFER_LEN + 1024];
		if (short_buf == 0)
		{
			MessageBox(NULL, TEXT("Couldn't Allocate Sample Buffer!"), TEXT("Error!"), MB_OK | MB_ICONERROR);
			return -1;
		}
		for (int k = 0; k <= NUM_BUFFERS_BEFORE_CALLBACK; ++k)
		{
			rcvBuf[k] = new (std::nothrow) uint8_t[MAX_BUFFER_LEN + 1024];
			if (rcvBuf[k] == 0)
			{
				MessageBox(NULL, TEXT("Couldn't Allocate Sample Buffers!"), TEXT("Error!"), MB_OK | MB_ICONERROR);
				return -1;
			}
		}
		rcvBufsAllocated = true;
	}

	worker_handle = (HANDLE) _beginthread( ThreadProc, 0, NULL );
	if(worker_handle == INVALID_HANDLE_VALUE)
		return -1;	// ERROR

	//SetThreadPriority(worker_handle, THREAD_PRIORITY_TIME_CRITICAL);
	return 0;
}

int Stop_Thread()
{
	if(worker_handle == INVALID_HANDLE_VALUE)
		return 0;

	terminateThread = true;
	WaitForSingleObject(worker_handle,INFINITE);
	GotTunerInfo = false;
	return 0;
}


void ThreadProc(void *p)
{
	while (!terminateThread)
	{
		// E4000 = 1, FC0012 = 2, FC0013 = 3, FC2580 = 4, R820T = 5, R828D = 6
		rtl_tcp_dongle_info.ui[1] = tunerNo = 0;
		rtl_tcp_dongle_info.ui[2] = numTunerGains = 0;

		GotTunerInfo = false;
		if (h_dialog)
			PostMessage(h_dialog, WM_PRINT, (WPARAM)0, (LPARAM)PRF_CLIENT);

		CActiveSocket conn;
		const bool initOK = conn.Initialize();
		const bool connOK = conn.Open(RTL_TCP_IPAddr, (uint16_t)RTL_TCP_PortNo);

		if (connOK)
		{
			if (GUIDebugConnection)
				::MessageBoxA(0, "TCP connect was successful", "Status", 0);
		}
		else
		{
			// ::MessageBoxA(0, "TCP connect failed!\nRetry ..", "Status", 0);
			goto label_reConnect;
		}

		if (ASyncConnection)
			conn.SetNonblocking();
		else
			conn.SetBlocking();

		// on connection server will transmit dongle_info once
		int readHdr = 0;
		bool hdrOK = true;
		while (!terminateThread)
		{
			int32 toRead = 12 - readHdr;
			int32 nRead = conn.Receive(toRead);
			if (nRead > 0)
			{
				uint8_t *nBuf = conn.GetData();
				memcpy((void*)(&rtl_tcp_dongle_info.ac[readHdr]), nBuf, nRead);
				if (readHdr < 4 && readHdr + nRead >= 4)
				{
					if (   rtl_tcp_dongle_info.ac[0] != 'R'
						&& rtl_tcp_dongle_info.ac[1] != 'T'
						&& rtl_tcp_dongle_info.ac[2] != 'L'
						&& rtl_tcp_dongle_info.ac[3] != '0' )
					{
						// It has to start with "RTL0"!
						::MessageBoxA(0, "Error: Stream is not from rtl_tcp", "Error", 0);
						hdrOK = false;
						break;
					}
				}
				readHdr += nRead;
				if (readHdr >= 12)
					break;
			}
			else
			{
				CSimpleSocket::CSocketError err = conn.GetSocketError();
				if (CSimpleSocket::SocketSuccess != err && CSimpleSocket::SocketEwouldblock != err)
				{
					char acMsg[256];
					snprintf(acMsg, 255, "Socket Error %d after %d bytes in header!", (int)err, nRead);
					::MessageBoxA(0, acMsg, "Socket Error", 0);
					hdrOK = false;
					break;
				}
				else if (CSimpleSocket::SocketEwouldblock == err && SleepMillisWaitingForData >= 0)
					::Sleep(SleepMillisWaitingForData);
			}
		}
		if (!hdrOK)
			goto label_reConnect;

		rtl_tcp_dongle_info.ui[1] = tunerNo = ntohl(rtl_tcp_dongle_info.ui[1]);
		rtl_tcp_dongle_info.ui[2] = numTunerGains = ntohl(rtl_tcp_dongle_info.ui[2]);

		GotTunerInfo = true;
		if (h_dialog)
			PostMessage(h_dialog, WM_PRINT, (WPARAM)0, (LPARAM)PRF_CLIENT);

		if (GUIDebugConnection)
			::MessageBoxA(0, "received 12 bytes header", "Status", 0);

		// update bandwidths
		bandwidths = tuner_bws[tunerNo].bw;
		n_bandwidths = tuner_bws[tunerNo].num;
		if (n_bandwidths)
		{
			int bwIdx = nearestBwIdx(new_TunerBW);
			new_TunerBW = bandwidths[bwIdx];
			last_TunerBW = new_TunerBW + 1;
		}

		// update gains
		gains = tuner_gains[tunerNo].gain;
		n_gains = tuner_gains[tunerNo].num;
		if (n_gains)
		{
			int gainIdx = nearestGainIdx(new_gain);
			new_gain = gains[gainIdx];
			last_gain = new_gain + 10;
		}

		int callbackBufferBegin = 0;
		int numReceivedBuffers = 0;
		int receivedLen = 0;
		unsigned receivedBlocks = 0;
		int initialSrate = 1;
		int receivedSamples = 0;
		commandEverything = true;

		while (!terminateThread)
		{
			if (ThreadStreamToSDR && (somewhat_changed || commandEverything))
			{
				if (last_DirectSampling != new_DirectSampling || commandEverything)
				{
					if (!transmitTcpCmd(conn, 0x09, new_DirectSampling))
						break;
					last_DirectSampling = new_DirectSampling;
					somewhat_changed &= ~(32);
				}
				if (last_OffsetTuning != new_OffsetTuning || commandEverything)
				{
					if (!transmitTcpCmd(conn, 0x0A, new_OffsetTuning))
						break;
					last_OffsetTuning = new_OffsetTuning;
					somewhat_changed &= ~(64);
				}
				if (last_FreqCorrPPM != new_FreqCorrPPM || commandEverything)
				{
					if (!transmitTcpCmd(conn, 0x05, new_FreqCorrPPM))
						break;
					last_FreqCorrPPM = new_FreqCorrPPM;
					somewhat_changed &= ~(128);
				}
				if (last_freq != new_freq || commandEverything)
				{
					if (!transmitTcpCmd(conn, 0x01, new_freq))
						break;
					last_freq = new_freq;
					somewhat_changed &= ~(1);
				}
				if (last_srate_idx != new_srate_idx || commandEverything)
				{
					// re-parametrize TunerAGC
					{
						transmitTcpCmd(conn, 0x03, 1 - new_TunerAGC);
						last_TunerAGC = new_TunerAGC;
						somewhat_changed &= ~(8);
					}

					// re-parametrize Gain
					if (new_TunerAGC == 0)
					{
						transmitTcpCmd(conn, 0x04, new_gain);
						last_gain = new_gain;
						somewhat_changed &= ~(4);
					}

					// re-parametrize Tuner Bandwidth
					{
						if (n_bandwidths)
							transmitTcpCmd(conn, 0x0E, new_TunerBW * 1000);
						last_TunerBW = new_TunerBW;
						somewhat_changed &= ~(256);
					}

					// re-parametrize samplerate
					{
						if (!transmitTcpCmd(conn, 0x02, samplerates[new_srate_idx].valueInt))
							break;
						last_srate_idx = new_srate_idx;
						somewhat_changed &= ~(2);
					}
				}
				if (last_TunerBW != new_TunerBW )
				{
					if (!transmitTcpCmd(conn, 0x0E, new_TunerBW*1000))
						break;
					last_TunerBW = new_TunerBW;
					somewhat_changed &= ~(256);
				}
				if (last_TunerAGC != new_TunerAGC)
				{
					if (!transmitTcpCmd(conn, 0x03, 1-new_TunerAGC))
						break;
					last_TunerAGC = new_TunerAGC;
					if (new_TunerAGC == 0)
						last_gain = new_gain + 1;
					somewhat_changed &= ~(8);
				}
				if (last_RTLAGC != new_RTLAGC || commandEverything)
				{
					if (!transmitTcpCmd(conn, 0x08, new_RTLAGC))
						break;
					last_RTLAGC = new_RTLAGC;
					somewhat_changed &= ~(16);
				}
				if (last_gain != new_gain)
				{
					if (new_TunerAGC == 0)
					{
						// transmit manual gain only when TunerAGC is off
						if (!transmitTcpCmd(conn, 0x04, new_gain))
							break;
					}
					last_gain = new_gain;
					somewhat_changed &= ~(4);
				}
				commandEverything = false;
			}

			int32 toRead = buffer_len - receivedLen;
			int32 nRead = conn.Receive(toRead);
			if (nRead > 0)
			{
				uint8_t *nBuf = conn.GetData();
				memcpy(&rcvBuf[numReceivedBuffers][receivedLen], nBuf, nRead);
				receivedLen += nRead;
				if (receivedLen >= buffer_len)
				{
					if (ThreadStreamToSDR)
					{
						++numReceivedBuffers;
						if (numReceivedBuffers >= NUM_BUFFERS_BEFORE_CALLBACK)
						{
							for (int callbackBufferNo = callbackBufferBegin; callbackBufferNo < NUM_BUFFERS_BEFORE_CALLBACK; ++callbackBufferNo)
							{
								if (extHWtype == exthwUSBdata16)
								{
									short *short_ptr = &short_buf[0];
									const unsigned char* char_ptr = &rcvBuf[callbackBufferNo][0];
									for (int i = 0; i < buffer_len; i++)
										*short_ptr++ = ((short)(*char_ptr++)) - 128;
									WinradCallBack(buffer_len >> 1, 0, 0, short_buf);
								}
								else
									WinradCallBack(buffer_len >> 1, 0, 0, &rcvBuf[callbackBufferNo][0]);
							}
							numReceivedBuffers = callbackBufferBegin = NUM_BUFFERS_BEFORE_CALLBACK - 1;
						}
					}
					else
					{
						commandEverything = true;
						numReceivedBuffers = callbackBufferBegin = 0;
					}

					receivedLen -= buffer_len;
					++receivedBlocks;
					if (receivedLen > 0)
						memcpy(&rcvBuf[0][0], &rcvBuf[0][buffer_len], receivedLen);
				}
			}
			else
			{
				CSimpleSocket::CSocketError err = conn.GetSocketError();
				if (CSimpleSocket::SocketSuccess != err && CSimpleSocket::SocketEwouldblock != err)
				{
					char acMsg[256];
					if (GUIDebugConnection)
						snprintf(acMsg, 255, "Socket Error %d after %d bytes in data after %u blocks!", (int)err, receivedLen, receivedBlocks);
					else
						snprintf(acMsg, 255, "Socket Error %d !", (int)err);
					::MessageBoxA(0, acMsg, "Socket Error", 0);
					goto label_reConnect;
				}
				else if (CSimpleSocket::SocketEwouldblock == err && SleepMillisWaitingForData >= 0)
					::Sleep(SleepMillisWaitingForData);
			}
		}

label_reConnect:
		conn.Close();
		if (!AutoReConnect)
			break;
	}

	worker_handle = INVALID_HANDLE_VALUE;
	_endthread();
}


static void updateTunerBWs(HWND hwndDlg)
{
	TCHAR str[256];
	HWND hDlgItmTunerBW = GetDlgItem(hwndDlg, IDC_TUNERBANDWIDTH);

	bandwidths = tuner_bws[tunerNo].bw;
	n_bandwidths = tuner_bws[tunerNo].num;

	ComboBox_ResetContent(hDlgItmTunerBW);
	if (n_bandwidths)
		ComboBox_AddString(hDlgItmTunerBW, TEXT("Automatic") );
	for (int i = 1; i < n_bandwidths; i++)
	{
		_stprintf_s(str, 255, TEXT("~ %d kHz%s"), bandwidths[i], ( (bandwidths[i] * 1000 > MAXRATE) ? " !" : "" ) );
		ComboBox_AddString(hDlgItmTunerBW, str);
	}
	ComboBox_SetCurSel(hDlgItmTunerBW, nearestBwIdx(new_TunerBW));
}

static void updateTunerGains(HWND hwndDlg)
{
	// rtlsdr_get_tuner_gains(dev, NULL);
	HWND hGain = GetDlgItem(hwndDlg, IDC_GAIN);
	HWND hGainLabel = GetDlgItem(hwndDlg, IDC_GAINVALUE);
	HWND hTunerBwLabel = GetDlgItem(hwndDlg, IDC_TUNER_BW_LABEL);

	gains = tuner_gains[tunerNo].gain;
	n_gains = tuner_gains[tunerNo].num;

	if (0 == tunerNo)
		Static_SetText(hTunerBwLabel, TEXT("Tuner Bandwidth:"));
	else if (0 == n_gains)
	{
		TCHAR str[255];
		_stprintf_s(str, 255, TEXT("[ %s-Tuner Bandwidth ]"), TunerName[tunerNo]);
		Static_SetText(hTunerBwLabel, str);
	}
	else
	{
		TCHAR str[255];
		_stprintf_s(str, 255, TEXT("%s-Tuner Bandwidth"), TunerName[tunerNo]);
		Static_SetText(hTunerBwLabel, str);
	}

	if (n_gains > 0)
	{
		SendMessage(hGain, TBM_SETRANGEMIN, (WPARAM)TRUE, (LPARAM)-gains[n_gains - 1]);
		SendMessage(hGain, TBM_SETRANGEMAX, (WPARAM)TRUE, (LPARAM)-gains[0]);
	}
	else
	{
		SendMessage(hGain, TBM_SETRANGEMIN, (WPARAM)TRUE, (LPARAM)-100);
		SendMessage(hGain, TBM_SETRANGEMAX, (WPARAM)TRUE, (LPARAM)0);
	}

	SendMessage(hGain, TBM_CLEARTICS, (WPARAM)FALSE, (LPARAM)0);
	if (n_gains > 0)
	{
		for (int i = 0; i<n_gains; i++)
			SendMessage(hGain, TBM_SETTIC, (WPARAM)0, (LPARAM)-gains[i]);

		int gainIdx = nearestGainIdx(new_gain);
		if (new_gain != gains[gainIdx])
		{
			new_gain = gains[gainIdx];
			somewhat_changed |= 4;
		}
		SendMessage(hGain, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)-new_gain);
	}

	if (new_TunerAGC)
	{
		EnableWindow(hGain, FALSE);
		Static_SetText(hGainLabel, TEXT("AGC"));
	}
	else
	{
		EnableWindow(hGain, TRUE);
		int pos = -SendMessage(hGain, TBM_GETPOS, (WPARAM)0, (LPARAM)0);
		TCHAR str[255];
		_stprintf_s(str, 255, TEXT("%2.1f dB"), (float)pos / 10);
		Static_SetText(hGainLabel, str);
		//rtlsdr_set_tuner_gain(dev,gains[gain_d_index]);
	}
}


static INT_PTR CALLBACK MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static HBRUSH BRUSH_RED=CreateSolidBrush(RGB(255,0,0));
	static HBRUSH BRUSH_GREEN=CreateSolidBrush(RGB(0,255,0));

   	switch (uMsg)
    {
        case WM_INITDIALOG:
		{
			//HWND hGainLabel = GetDlgItem(hwndDlg, IDC_GAINVALUE);
			HWND hDlgItmOffset = GetDlgItem(hwndDlg, IDC_OFFSET);

			for (int i=0; i<(sizeof(directS)/sizeof(directS[0]));i++)
				ComboBox_AddString(GetDlgItem(hwndDlg,IDC_DIRECT),directS[i]);
			ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_DIRECT), new_DirectSampling);

			Button_SetCheck(GetDlgItem(hwndDlg, IDC_AUTORECONNECT), AutoReConnect ? BST_CHECKED : BST_UNCHECKED);

			Button_SetCheck(GetDlgItem(hwndDlg, IDC_PERSISTCONNECTION), PersistentConnection ? BST_CHECKED : BST_UNCHECKED);

			Button_SetCheck(GetDlgItem(hwndDlg, IDC_TUNERAGC), new_TunerAGC ? BST_CHECKED : BST_UNCHECKED);

			Button_SetCheck(GetDlgItem(hwndDlg, IDC_RTLAGC), new_RTLAGC ? BST_CHECKED : BST_UNCHECKED);

			Button_SetCheck(GetDlgItem(hwndDlg, IDC_OFFSET), new_OffsetTuning ? BST_CHECKED : BST_UNCHECKED);

			SendMessage(GetDlgItem(hwndDlg,IDC_PPM_S), UDM_SETRANGE  , (WPARAM)TRUE, (LPARAM)MAX_PPM | (MIN_PPM << 16));
			
			TCHAR tempStr[255];
			_stprintf_s(tempStr, 255, TEXT("%d"), new_FreqCorrPPM);
			Edit_SetText(GetDlgItem(hwndDlg,IDC_PPM), tempStr );
			//rtlsdr_set_freq_correction(dev, new_FreqCorrPPM);

			{
				TCHAR tempStr[255];
				_stprintf_s(tempStr, 255, TEXT("%s:%d"), RTL_TCP_IPAddr, RTL_TCP_PortNo);
				Edit_SetText(GetDlgItem(hwndDlg, IDC_IP_PORT), tempStr);
			}

			for (int i = 0; i<n_srates; i++)
				ComboBox_AddString(GetDlgItem(hwndDlg,IDC_SAMPLERATE),samplerates[i].name);
			ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_SAMPLERATE), new_srate_idx);

			{
				for (int i = 0; i < (sizeof(buffer_sizes) / sizeof(buffer_sizes[0])); i++)
				{
					TCHAR str[255];
					_stprintf_s(str, 255, TEXT("%d kB"), buffer_sizes[i]);
					ComboBox_AddString(GetDlgItem(hwndDlg, IDC_BUFFER), str);
				}
				ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_BUFFER), bufferSizeIdx);
				buffer_len = buffer_sizes[bufferSizeIdx] * 1024;
			}

			updateTunerBWs(hwndDlg);

			updateTunerGains(hwndDlg);

			return TRUE;
		}

		case WM_PRINT:
			if (lParam == (LPARAM)PRF_CLIENT)
			{
				HWND hDlgItmOffset = GetDlgItem(hwndDlg, IDC_OFFSET);
				HWND hDlgItmTunerBW = GetDlgItem(hwndDlg, IDC_TUNERBANDWIDTH);

				updateTunerBWs(hwndDlg);
				updateTunerGains(hwndDlg);

				BOOL enableOffset = (1 == tunerNo) ? TRUE : FALSE;
				BOOL enableTunerBW = (bandwidths && 0 == new_DirectSampling) ? TRUE : FALSE;
				EnableWindow(hDlgItmOffset, enableOffset);
				EnableWindow(hDlgItmTunerBW, enableTunerBW);

				const char * tunerText = (tunerNo >= 0 && tunerNo < n_tuners)
					? TunerName[tunerNo] : "unknown";
				TCHAR str[255];
				_stprintf_s(str,255, TEXT("%s-Tuner AGC"), tunerText ); 
				Static_SetText(GetDlgItem(hwndDlg,IDC_TUNERAGC),str);
			}
			return TRUE;

        case WM_COMMAND:
            switch (GET_WM_COMMAND_ID(wParam, lParam))
            {
				case IDC_PPM:
					if(GET_WM_COMMAND_CMD(wParam, lParam) == EN_CHANGE)
                    { 
                        TCHAR ppm[255];
						Edit_GetText((HWND) lParam, ppm, 255 );
						new_FreqCorrPPM = _ttoi(ppm);
						somewhat_changed |= 128;
						WinradCallBack(-1,WINRAD_LOCHANGE,0,NULL);
                    }
                    return TRUE;
                case IDC_RTLAGC:
				{
					new_RTLAGC = (Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED) ? 1 : 0;
					somewhat_changed |= 16;
					return TRUE;
				}
                case IDC_OFFSET:
				{
					HWND hDlgItmOffset = GetDlgItem(hwndDlg, IDC_OFFSET);

					new_OffsetTuning = (Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED) ? 1 : 0;
					somewhat_changed |= 64;

					// E4000 = 1, FC0012 = 2, FC0013 = 3, FC2580 = 4, R820T = 5, R828D = 6
					if (1 == rtl_tcp_dongle_info.ui[1])
						EnableWindow(hDlgItmOffset, TRUE);
					else
						EnableWindow(hDlgItmOffset, FALSE);
					return TRUE;
				}
				case IDC_TUNERAGC:
				{
					HWND hGain = GetDlgItem(hwndDlg, IDC_GAIN);
					HWND hGainLabel = GetDlgItem(hwndDlg, IDC_GAINVALUE);

					if(Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED) //it is checked
					{
						new_TunerAGC = 1;	// automatic
						somewhat_changed |= 8;

						EnableWindow(hGain,FALSE);
						Static_SetText(hGainLabel, TEXT("AGC"));
					}
					else //it has been unchecked
					{
						//rtlsdr_set_tuner_gain_mode(dev,1);
						new_TunerAGC = 0;	// manual
						somewhat_changed |= 8;

						EnableWindow(hGain,TRUE);

						int pos=-SendMessage(hGain, TBM_GETPOS, (WPARAM)0, (LPARAM)0);
						TCHAR str[255];
						_stprintf_s(str,255, TEXT("%2.1f dB"),(float) pos/10); 
						Static_SetText(hGainLabel, str);
					}
					return TRUE;
				}

				case IDC_AUTORECONNECT:
				{
					AutoReConnect = (Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED) ? 1 : 0;
					return TRUE;
				}

				case IDC_PERSISTCONNECTION:
				{
					PersistentConnection = (Button_GetCheck(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED) ? 1 : 0;
					if (!PersistentConnection && !ThreadStreamToSDR)
						Stop_Thread();
					return TRUE;
				}

				case IDC_SAMPLERATE:
					if(GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
                    { 
						new_srate_idx = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));
						somewhat_changed |= 2;
						WinradCallBack(-1,WINRAD_SRCHANGE,0,NULL);// Signal application
                    }
					if(GET_WM_COMMAND_CMD(wParam, lParam) ==  CBN_EDITUPDATE)
                    { 
                        TCHAR  ListItem[256];
						ComboBox_GetText((HWND) lParam,ListItem,256);
						TCHAR *endptr;
						double coeff = _tcstod(ListItem, &endptr);
						
						while (_istspace(*endptr)) ++endptr;

						int exp = 1;	
						switch (_totupper(*endptr)) {
							case 'K': exp = 1024; break;
							case 'M': exp = 1024*1024; break;
						}
						
						uint32_t newrate = uint32_t( coeff * exp );
						if (newrate>=MINRATE && newrate<=MAXRATE) {
							//rtlsdr_set_sample_rate(dev, newrate);
// @TODO!
							WinradCallBack(-1,WINRAD_SRCHANGE,0,NULL);// Signal application
						}

                    }
                    return TRUE;

				case IDC_BUFFER:
					if(GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
                    {
						bufferSizeIdx = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));
						buffer_len = buffer_sizes[bufferSizeIdx] * 1024;
						WinradCallBack(-1,WINRAD_SRCHANGE,0,NULL);// Signal application
                    }
                    return TRUE;

				case IDC_TUNERBANDWIDTH:
					if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
					{
						int bwIdx = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));
						new_TunerBW = bandwidths[bwIdx];
						somewhat_changed |= 256;
					}
					return TRUE;

				case IDC_DIRECT:
					if(GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
                    { 
						new_DirectSampling = ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam));
						somewhat_changed |= 32;

						WinradCallBack(-1,WINRAD_LOCHANGE,0,NULL);// Signal application
                    }
                    return TRUE;

				case IDC_IP_PORT:
					if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_CHANGE)
					{
						TCHAR tempStr[255];
						Edit_GetText((HWND)lParam, tempStr, 255);
						//rtlsdr_set_freq_correction(dev, _ttoi(ppm))
						char * IP = strtok(tempStr, ":");
						if (IP)
							snprintf(RTL_TCP_IPAddr, 31, "%s", IP);
						char * PortStr = strtok(NULL, ":");
						if (PortStr)
						{
							int PortNo = atoi(PortStr);
							if (PortNo > 0 && PortNo < 65536)
								RTL_TCP_PortNo = PortNo;
						}
					}
					return TRUE;
			}
            break;

		case WM_VSCROLL:
			{
				HWND hGain = GetDlgItem(hwndDlg, IDC_GAIN);
				if ((HWND)lParam == hGain)
				{
					int pos = -SendMessage(hGain, TBM_GETPOS, (WPARAM)0, (LPARAM)0);
					for (int i = 0; i < n_gains - 1; ++i)
						if (gains[i] < pos && pos < gains[i + 1])
							if ((pos - gains[i]) < (gains[i + 1] - pos) && (LOWORD(wParam) != TB_LINEUP) || (LOWORD(wParam) == TB_LINEDOWN))
								pos = gains[i];
							else
								pos = gains[i + 1];

					SendMessage(hGain, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)-pos);
					TCHAR str[255];
					_stprintf_s(str, 255, TEXT("%2.1f  dB"), (float)pos / 10);
					Static_SetText(GetDlgItem(hwndDlg, IDC_GAINVALUE), str);

					if (pos != last_gain)
					{
						new_gain = pos;
						somewhat_changed |= 4;
						WinradCallBack(-1, WINRAD_ATTCHANGE, 0, NULL);
					}

					return TRUE;
				}
				if ((HWND)lParam == GetDlgItem(hwndDlg, IDC_PPM_S))
				{
					return TRUE;
				}
			}
			break;

        case WM_CLOSE:
			ShowWindow(hwndDlg, SW_HIDE);
            return TRUE;
			break;

		case WM_DESTROY:
			h_dialog=NULL;
			return TRUE;
			break;

		/*
		* TODO: Add more messages, when needed.
		*/
	}

	return FALSE;
}

