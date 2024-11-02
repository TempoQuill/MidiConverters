// GRC -> Midi Converter
// ---------------------
// Written by Valley Bell, 8 December 2013
// Improved on 20 December 2013
// "Socket" support added on 08 April 2016

// TODO:
//		- make Chorus thing optional

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "stdtype.h"
#include "stdbool.h"

#ifdef _MSC_VER
#define stricmp	_stricmp
#else
#define stricmp	strcasecmp
#endif


#include "midi_funcs.h"

typedef struct _track_info
{
	UINT16 startOfs;
	UINT16 loopOfs;
	UINT32 tickCnt;
	UINT32 loopTick;
	UINT16 loopTimes;
	UINT8 flags;
	UINT8 maxVol;
	bool volBoost;
} TRK_INF;

#define BALANCE_TRACK_TIMES
#include "midi_utils.h"


static UINT16 DetectSongCount(UINT32 MusLibLen, const UINT8* MusLibData, UINT32 BasePos);
UINT8 GRC2Mid(UINT32 GrcLen, UINT8* GrcData, UINT16 GrcAddr/*, UINT32* OutLen, UINT8** OutData*/);
static void PreparseGrc(UINT32 GrcLen, const UINT8* GrcData, UINT8* GrcBuf, TRK_INF* TrkInf, UINT8 Mode);
static UINT16 ReadLE16(const UINT8* Buffer);
static float OPN2DB(UINT8 TL, UINT8 PanMode, bool VolBoost);
static float PSG2DB(UINT8 Vol);
static UINT8 DB2Mid(float DB);
static void CopySMPSModData(const UINT8* RawData, UINT8* MidVals);

void SaveInsAsGYB(const char* FileName, const UINT8* InsData);


static const UINT8 VOL_TABLE_FM[0x10] =
{	0x7F, 0x1F, 0x1E, 0x1D, 0x1C, 0x1B, 0x1A, 0x18,
	0x16, 0x14, 0x12, 0x10, 0x0E, 0x0C, 0x0B, 0x00};
static const UINT8 VOL_TABLE_PSG[0x10] =
{	0x7F, 0x70, 0x68, 0x60, 0x58, 0x50, 0x48, 0x40,
	0x38, 0x30, 0x28, 0x20, 0x18, 0x10, 0x08, 0x00};
static const UINT8 NOTE_SCALE[0x10] =
{	0, 2, 4, 5, 7,  9, 11, 0xFF,
	1, 3, 4, 6, 8, 10, 11, 0xFF};
static const UINT8 CHN_MASK[0x0A] =
{	0x00, 0x01, 0x02, 0x10, 0x11, 0x12,
	0x80, 0xA0, 0xC0, 0xE0};

// Modulation -> SMPS Modulation conversion table
static const UINT8 MOD_DATA[0x22][4] =
{
	{0x00, 0x00, 0x00, 0x00},	// 00
	{0x08, 0x01, 0x05, 0x05},	// 01
	{0x08, 0x01, 0x04, 0x07},	// 02
	{0x08, 0x01, 0x03, 0x07},	// 03
	{0x00, 0x01, 0x0A, 0x07},	// 04
	{0x0C, 0x01, 0x10, 0x05},	// 05
	{0x0C, 0x01, 0x20, 0x05},	// 06
	{0x00, 0x01, 0xEC, 0xFF},	// 07
	{0x00, 0x01, 0xE2, 0xFF},	// 08
	{0x00, 0x01, 0xD8, 0xFF},	// 09
	{0x00, 0x01, 0xCE, 0xFF},	// 0A
	{0x00, 0x01, 0x34, 0xFF},	// 0B
	{0x00, 0x01, 0xD8, 0xFF},	// 0C
	{0x06, 0x01, 0x08, 0x03},	// 0D
	{0x00, 0x01, 0xFE, 0xFF},	// 0E
	{0x0C, 0x01, 0x06, 0x06},	// 0F
	{0x00, 0x01, 0x1A, 0xFF},	// 10
	{0x05, 0x02, 0x01, 0x05},	// 11
	{0x05, 0x02, 0xFF, 0x05},	// 12
	{0x06, 0x01, 0x03, 0x03},	// 13
	{0x00, 0x01, 0x3C, 0xFF},	// 14
	{0x00, 0x01, 0x46, 0xFF},	// 15
	{0x00, 0x01, 0x50, 0xFF},	// 16
	{0x00, 0x01, 0x64, 0xFF},	// 17
	{0x00, 0x01, 0x96, 0xFF},	// 18
	{0x0C, 0x01, 0x02, 0xFF},	// 19
	{0x00, 0x01, 0xF8, 0xFF},	// 1A
	{0x00, 0x01, 0x0A, 0x06},	// 1B
	{0x00, 0x01, 0x07, 0x09},	// 1C
	{0x0C, 0x01, 0x06, 0x06},	// 1D
	{0x00, 0x01, 0xFF, 0xFF},	// 1E
	{0x00, 0x01, 0xFE, 0xFF},	// 1F
	{0x00, 0x01, 0x05, 0xFF},	// 20
	{0x00, 0x01, 0x07, 0xFF},	// 21
};


#define MODE_MUS	0x00
#define	MODE_DAC	0x01
#define MODE_INS	0x02



static UINT32 MidLen;
static UINT8* MidData;
static UINT16 TickpQrtr;
static UINT16 DefLoopCount;
static bool OptVolWrites;
static bool NoLoopExt;
static bool EnableSMPSMod;

int main(int argc, char* argv[])
{
	FILE* hFile;
	//UINT8 PLMode;
	UINT32 SongPos;
	char OutFileBase[0x100];
	char OutFile[0x100];
	char* TempPnt;
	int RetVal;
	int argbase;
	UINT8 Mode;
	
	UINT32 InLen;
	UINT8* InData;
	//UINT32 OutLen;
	//UINT8* OutData;
	
	UINT16 FileCount;
	UINT16 CurFile;
	UINT32 CurPos;
	//UINT32 TempLng;
	UINT16 TempSht;
	
	printf("GRC -> Midi Converter\n---------------------\n");
	if (argc < 2)
	{
		printf("Usage: grc2mid.exe [-Mode] [-Options] ROM.bin MusicListAddr(hex) [Song Count]\n");
		printf("Modes:\n");
		printf("    -mus        Music Mode (convert sequences to MID)\n");
		printf("    -ins        Instrument Mode (dump instruments to GYB)\n");
		//printf("    -dac        DAC Mode (dump DAC sounds to RAW)\n");
		printf("Options:\n");
		printf("    -OptVol     Optimize Volume writes (omits redundant ones)\n");
		printf("    -TpQ n      Sets the number of Ticks per Quarter to n. (default: 24)\n");
		printf("                Use values like 18 or 32 on songs with broken tempo.\n");
		printf("    -Loops n    Loop each track at least n times. (default: 2)\n");
		printf("    -NoLpExt    No Loop Extention\n");
		printf("                Do not fill short tracks to the length of longer ones.\n");
		//printf("    -SMPSMod    Enable writing mid2smps Modulation Definitions (Decap Attack)\n");
		return 0;
	}
	
	OptVolWrites = true;
	TickpQrtr = 24;
	DefLoopCount = 2;
	NoLoopExt = false;
	EnableSMPSMod = false;
	
	Mode = MODE_MUS;
	argbase = 1;
	while(argbase < argc && argv[argbase][0] == '-')
	{
		if (! stricmp(argv[argbase] + 1, "Mus"))
			Mode = MODE_MUS;
		else if (! stricmp(argv[argbase] + 1, "DAC"))
			Mode = MODE_DAC;
		else if (! stricmp(argv[argbase] + 1, "Ins"))
			Mode = MODE_INS;
		else if (! stricmp(argv[argbase] + 1, "OptVol"))
			OptVolWrites = true;
		else if (! stricmp(argv[argbase] + 1, "TpQ"))
		{
			argbase ++;
			if (argbase < argc)
			{
				TickpQrtr = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! TickpQrtr)
					TickpQrtr = 24;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "Loops"))
		{
			argbase ++;
			if (argbase < argc)
			{
				DefLoopCount = (UINT16)strtoul(argv[argbase], NULL, 0);
				if (! DefLoopCount)
					DefLoopCount = 2;
			}
		}
		else if (! stricmp(argv[argbase] + 1, "NoLpExt"))
			NoLoopExt = true;
		else if (! stricmp(argv[argbase] + 1, "SMPSMod"))
			EnableSMPSMod = true;
		else
			break;
		argbase ++;
	}
	
	//if (argc <= argbase)
	if (argc <= argbase + 1)
	{
		printf("Not enough arguments.\n");
		return 0;
	}
	
	strcpy(OutFileBase, argv[argbase + 0]);
	TempPnt = strrchr(OutFileBase, '.');
	if (TempPnt == NULL)
		TempPnt = OutFileBase + strlen(OutFileBase);
	*TempPnt = 0x00;
	
	SongPos = strtoul(argv[argbase + 1], NULL, 0x10);
	
	if (argc > argbase + 2)
		FileCount = (UINT16)strtoul(argv[argbase + 2], NULL, 0);
	else
		FileCount = 0x00;

	hFile = fopen(argv[argbase + 0], "rb");
	if (hFile == NULL)
	{
		printf("Error opening file!\n");
		return 1;
	}
	
	fseek(hFile, 0x00, SEEK_END);
	InLen = ftell(hFile);
	if (InLen > 0x800000)	// 8 MB
		InLen = 0x800000;
	
	fseek(hFile, 0x00, SEEK_SET);
	InData = (UINT8*)malloc(InLen);
	fread(InData, 0x01, InLen, hFile);
	
	fclose(hFile);
	
	switch(Mode)
	{
	case MODE_MUS:
		if (! FileCount)
			FileCount = DetectSongCount(InLen, InData, SongPos);
		
		CurPos = SongPos;
		for (CurFile = 0x00; CurFile < FileCount; CurFile ++, CurPos += 0x02)
		{
			printf("File %u / %u ...", CurFile + 1, FileCount);
			TempSht = ReadLE16(&InData[CurPos]);
			RetVal = GRC2Mid(InLen - SongPos, InData + SongPos, TempSht/*, &OutLen, &OutData*/);
			if (RetVal)
			{
				if (RetVal == 0x01)
				{
					printf(" empty - ignored.\n");
					continue;
				}
				
				return RetVal;
			}
			
			sprintf(OutFile, "%s_%02X.mid", OutFileBase, CurFile);
			
			hFile = fopen(OutFile, "wb");
			if (hFile == NULL)
			{
				free(MidData);	MidData = NULL;
				printf("Error opening file!\n");
				continue;
			}
			fwrite(MidData, MidLen, 0x01, hFile);
			
			fclose(hFile);
			free(MidData);	MidData = NULL;
			printf("\n");
		}
		printf("Done.\n");
		break;
	case MODE_DAC:
		//SaveDACData(OutFileBase);
		break;
	case MODE_INS:
		sprintf(OutFile, "%s.gyb", OutFileBase);
		SaveInsAsGYB(OutFile, &InData[SongPos]);
		break;
	}
	
#ifdef _DEBUG
	getchar();
#endif
	
	return 0;
}


static UINT16 DetectSongCount(UINT32 MusLibLen, const UINT8* MusLibData, UINT32 BasePos)
{
	// Song Count autodetection
	UINT16 CurFile;
	UINT32 CurPos;
	UINT32 SongPos;
	UINT32 MaxPos;
	
	MaxPos = BasePos + ReadLE16(&MusLibData[BasePos]);
	if (MaxPos > MusLibLen)
		MaxPos = MusLibLen;
	
	for (CurPos = BasePos, CurFile = 0x00; CurPos < MaxPos; CurPos += 0x02, CurFile ++)
	{
		SongPos = BasePos + ReadLE16(&MusLibData[CurPos]);
		if (SongPos < MaxPos)
			MaxPos = SongPos;
	}
	
	printf("Songs detected: 0x%02X (%u)\n", CurFile, CurFile);
	return CurFile;
}

UINT8 GRC2Mid(UINT32 GrcLen, UINT8* GrcData, UINT16 GrcAddr/*, UINT32* OutLen, UINT8** OutData*/)
{
	UINT8* TempBuf;
	TRK_INF TrkInf[0x0A];
	TRK_INF* TempTInf;
	UINT8 TrkCnt;
	UINT8 CurTrk;
	UINT16 InPos;
	UINT8 ChnMode;
	FILE_INF midFileInf;
	MID_TRK_STATE MTS;
	bool TrkEnd;
	UINT8 CurCmd;
	
	UINT8 StackPos;
	UINT16 StackAddr[0x10];
	UINT8 TempArr[0x04];
	UINT32 TempLng;
	UINT16 TempSht;
	UINT8 TempByt;
	UINT8 ChnVol;
	UINT8 ChnIns;
	UINT8 MidChnVol;
	UINT8 PanReg;
	UINT8 DefNoteLen;
	UINT8 CurOctave;
	UINT8 LastNote;
	UINT8 CurNote;
	UINT8 PanMode;
	UINT8 HoldNote;
	UINT8 NoteStop;
	UINT16 LoopCnt;
	UINT8 LocLoopCount;
	UINT8 LastModType;
	UINT8 ModDataMem[5];
	
	TrkCnt = 0x0A;
	midFileInf.alloc = 0x20000;	// 128 KB should be enough
	midFileInf.data = (UINT8*)malloc(midFileInf.alloc);
	midFileInf.pos = 0x00;
	
	WriteMidiHeader(&midFileInf, 0x0001, 1 + TrkCnt, TickpQrtr);	// number of tracks: MasterTrk + TrkCnt
	
	// write Master Track
	WriteMidiTrackStart(&midFileInf, &MTS);
	MTS.midChn = 0x00;
	
	// Note: Timing is 1 tick = 1 frame (60 Hz)
	// BPM = 3600 Ticks/min / 24 Ticks/Quarter
	// 3600 / 24 = 150 BPM
	// 150 BPM == MIDI Tempo 400 000
	//TempLng = 400000;
	TempLng = 50000 * TickpQrtr / 3;	// 1 000 000 * Tick/Qrtr / 60
	WriteBE32(TempArr, TempLng);
	WriteMetaEvent(&midFileInf, &MTS, 0x51, 0x03, &TempArr[0x01]);
	
	WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
	WriteMidiTrackEnd(&midFileInf, &MTS);
	
	// Read Header
	TempBuf = (UINT8*)malloc(GrcLen);
	InPos = GrcAddr;
	for (CurTrk = 0; CurTrk < TrkCnt; CurTrk ++, InPos += 0x03)
	{
		TempTInf = &TrkInf[CurTrk];
		TempTInf->flags = GrcData[InPos + 0x00];
		TempTInf->startOfs = ReadLE16(&GrcData[InPos + 0x01]);
		TempTInf->loopOfs = 0x0000;
		TempTInf->tickCnt = 0;
		TempTInf->loopTick = 0;
		TempTInf->maxVol = 0x7F;
		TempTInf->volBoost = false;
		ChnMode = CHN_MASK[CurTrk] & 0x80;
		
		PreparseGrc(GrcLen, GrcData, TempBuf, TempTInf, ChnMode | 0x00);
		if (TempTInf->maxVol < 0x08)
			TempTInf->volBoost = true;
		
		// If there is a loop, parse a second time to get the Loop Tick.
		if (TempTInf->loopOfs)
			PreparseGrc(GrcLen, GrcData, TempBuf, TempTInf, ChnMode | 0x01);
		TempTInf->loopTimes = TempTInf->loopOfs ? DefLoopCount : 0;
	}
	free(TempBuf);	TempBuf = NULL;
	
	if (! NoLoopExt)
		BalanceTrackTimes(TrkCnt, TrkInf, 24 / 4, 0xFF);
	
	// --- Main Conversion ---
	for (CurTrk = 0; CurTrk < TrkCnt; CurTrk ++)
	{
		TempTInf = &TrkInf[CurTrk];
		
		WriteMidiTrackStart(&midFileInf, &MTS);
		
		if (TempTInf->flags & 0x80)
			TrkEnd = false;
		else
			TrkEnd = true;
		InPos = TempTInf->startOfs;
		
		ChnMode = CHN_MASK[CurTrk];
		if (ChnMode & 0x80)
			MTS.midChn = 0x0A + (CurTrk - 0x06);
		else
			MTS.midChn = CurTrk;
		//MTS.midChn = (CurTrk == 0x05) ? 0x09 : CurTrk;
		ChnVol = 0x00;
		ChnIns = 0x00;
		PanReg = 0x00;
		DefNoteLen = 0x00;
		CurOctave = 0;
		StackPos = 0x00;
		HoldNote = 0x00;
		NoteStop = 0;
		
		CurNote = 0x40;
		LastNote = 0xFF;
		PanMode = 0x00;
		MidChnVol = 0xFF;
		LoopCnt = 0xFFFF;
		LocLoopCount = 0x80;
		LastModType = 0xFF;
		CopySMPSModData(MOD_DATA[0], ModDataMem);
		
		//TempArr[0x00] = 0x04;
		//WriteMetaEvent(&midFileInf, &MTS, 0x21, 0x01, &TempArr[0x00]);
		if (TempTInf->volBoost)
			WriteEvent(&midFileInf, &MTS, 0xB0, 93, 0x08);
		if (ChnMode == 0xE0)
			CurOctave = 5;
		
		while(! TrkEnd && InPos < GrcLen)
		{
			if (LoopCnt == 0xFFFF && InPos == TempTInf->loopOfs)
			{
				LoopCnt ++;
				WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)LoopCnt);
				MidChnVol |= 0x80;		// set Bit 7 for to force writing it the Volume again
				LastModType = 0xFF;
			}
			
			CurCmd = GrcData[InPos];
			if (! (CurCmd & 0x80))
			{
				//	Bits 0-3 (0F): Note Value (07/0F = rest)
				//	Bit   4  (10): use custom delay
				//	Bits 5-6 (60): Stereo Mask
				//
				//	Decap Attack: Note 0 is a B. (YM2612 FNum 0x26A)
				//	Socket: Note 0 is a C. (YM2612 FNum 0x28E)
				TempByt = NOTE_SCALE[CurCmd & 0x0F];
				if (MTS.midChn == 0x09)
					CurNote = 36 + ChnIns;
				else
				{
					if (TempByt == 0xFF)
						CurNote = 0xFF;
					else
					{
						CurNote = CurOctave * 12 + TempByt - 0;
						if (CurNote > 0x7F)
							CurNote = 0x7F;
					}
				}
				
				if (HoldNote && LastNote != CurNote)
				{
					if (CurNote == 0xFF)
					{
						printf("Warning: Ignoring command 0xFE!\n");
						HoldNote = 0x00;
					}
					else
					{
						//printf("Warning: Note Portamento!\n");
						HoldNote = 0x02;
					}
				}
				if (LastNote != 0xFF && ! HoldNote)
				{
					WriteEvent(&midFileInf, &MTS, 0x90, LastNote, 0x00);
				}
				
				if (CurNote != 0xFF)
				{
					// Pan has only an effect when a Note is played.
					TempByt = (CurCmd & 0x60) << 1;
					if (TempByt != PanReg)
					{
						// write Pan
						PanReg = TempByt;
						switch(PanReg & 0xC0)
						{
						case 0x40:	// Left Channel
							TempByt = 0x00;
							break;
						case 0x80:	// Right Channel
							TempByt = 0x7F;
							break;
						case 0x00:	// No Channel
						case 0xC0:	// Both Channels
							TempByt = 0x40;
							break;
						}
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x0A, TempByt);
						
						TempByt = (TempByt == 0x40) ? 0x00 : 0x01;
						TempByt |= (PanMode & ~0x01);
						if (TempByt != PanMode)
						{
							PanMode = TempByt;
							TempByt = DB2Mid(OPN2DB(ChnVol, PanMode, TempTInf->volBoost));
							if (! OptVolWrites || TempByt != MidChnVol)
							{
								MidChnVol = TempByt;
								WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, MidChnVol);
							}
						}
					}
				}
				
				if (CurNote != 0xFF)
				{
					if (HoldNote == 0x00)
					{
						WriteEvent(&midFileInf, &MTS, 0x90, CurNote, 0x7F);
					}
					else if (HoldNote == 0x02)
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x7F);	// Portamento On
						WriteEvent(&midFileInf, &MTS, 0x90, LastNote, 0x00);
						WriteEvent(&midFileInf, &MTS, 0x90, CurNote, 0x7F);
					}
				}
				
				if (! (CurCmd & 0x10))
				{
					MTS.curDly += DefNoteLen;
				}
				else
				{
					InPos ++;
					MTS.curDly += GrcData[InPos];
				}
				InPos ++;
				// TODO: add code to handle Note Stop here
				
				if (HoldNote == 0x02)
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x41, 0x00);	// Portamento Off
				LastNote = CurNote;
				HoldNote = false;
			}
			else if ((CurCmd & 0xE0) == 0x80)
			{
				if ((CurCmd & 0xF0) == 0x80)
				{
					// set Volume from Lookup Table
					if (! (ChnMode & 0x80))
					{
						ChnVol = VOL_TABLE_FM[CurCmd & 0x0F];
						TempByt = DB2Mid(OPN2DB(ChnVol, PanMode, TempTInf->volBoost));
					}
					else
					{
						ChnVol = VOL_TABLE_PSG[CurCmd & 0x0F];
						TempByt = DB2Mid(PSG2DB(ChnVol));
					}
					if (! OptVolWrites || TempByt != MidChnVol)
					{
						MidChnVol = TempByt;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, MidChnVol);
					}
				}
				else //if ((CurCmd & 0xF0) == 0x90)
				{
					CurOctave = CurCmd & 0x0F;
					if (ChnMode & 0x80)
						CurOctave ++;
					if (ChnMode == 0xC0)
						CurOctave -= 4;
				}
				InPos ++;
			}
			else
			{
				InPos ++;
				switch(CurCmd)
				{
				case 0xEC:	// Note Stop (x frames before it expires)
					NoteStop = GrcData[InPos];
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x29, NoteStop);
					InPos ++;
					
					printf("NoteStop = %u on track %X\n", NoteStop, CurTrk);
					//if (NO_NOTESTOP)
					//	NoteStop = 0;
					break;
				case 0xED:	// Set PSG Noise Mode
					TempByt = GrcData[InPos];
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x03, TempByt & 0x0F);
					InPos ++;
					break;
				case 0xEE:	// Loop
					if (LocLoopCount)
					{
						if (LocLoopCount & 0x80)
							LocLoopCount = GrcData[InPos];
						LocLoopCount --;
						if (LocLoopCount)
						{
							TempSht = ReadLE16(&GrcData[InPos + 0x01]);
							InPos --;
							InPos += TempSht;
							if (InPos >= GrcLen)
								return 0x00;
							break;
						}
					}
					
					LocLoopCount = 0x80;
					InPos += 0x03;
					break;
				case 0xEF:	// set Detune
					TempSht = 0x2000 + (INT8)GrcData[InPos] * 128;
					InPos ++;
					
					WriteEvent(&midFileInf, &MTS, 0xE0, TempSht & 0x7F, (TempSht >> 7) & 0x7F);
					break;
				case 0xF0:	// reset SFX ID
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, 0x70);
					break;
				case 0xF1:	// set Volume
					ChnVol = GrcData[InPos] & 0x7F;
					if (ChnMode & 0x80)
						TempByt = DB2Mid(PSG2DB(ChnVol));
					else
						TempByt = DB2Mid(OPN2DB(ChnVol, PanMode, TempTInf->volBoost));
					InPos ++;
					
					if (! OptVolWrites || TempByt != MidChnVol)
					{
						MidChnVol = TempByt;
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x07, MidChnVol);
					}
					break;
				case 0xF2:	// Enable/Disable DAC
					if (LastNote != 0xFF)
					{
						// turn note off before we change the channel
						WriteEvent(&midFileInf, &MTS, 0x90, LastNote, 0x00);
						LastNote = 0xFF;
					}
					
					TempByt = GrcData[InPos];
					InPos ++;
					if (TempByt)
					{
						// enable DAC
						PanMode |= 0x80;
						if (MTS.midChn != 0x09)
							MTS.midChn = 0x09;
					}
					else
					{
						// disable DAC
						PanMode &= ~0x80;
						if (MTS.midChn == 0x09)
							MTS.midChn = CurTrk;
					}
					break;
				case 0xF3:	// set Fade Speed
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, 0x73);
					InPos ++;
					break;
				case 0xF4:	// synchronize all tracks
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, 0x74);
					break;
				case 0xF5:	// set YM2612 Timer B
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, 0x75);
					InPos ++;
					break;
				case 0xF6:	// set AMS/FMS
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, 0x76);
					InPos ++;
					break;
				case 0xF7:	// set LFO rate
					WriteEvent(&midFileInf, &MTS, 0xB0, 0x70, 0x77);
					InPos ++;
					break;
				case 0xF8:	// Return from GoSub
					if (! StackPos)
					{
						printf("Error: Return without GoSub! (Pos 0x%04X)\n", InPos - 0x01);
						TrkEnd = true;
						break;
					}
					
					StackPos --;
					InPos = StackAddr[StackPos];
					break;
				case 0xF9:	// GoSub
					TempSht = ReadLE16(&GrcData[InPos]);
					InPos --;
					
					StackAddr[StackPos] = InPos + 0x03;
					StackPos ++;
					
					InPos += TempSht;
					break;
				case 0xFA:	// GoTo
					TempSht = ReadLE16(&GrcData[InPos]);
					InPos --;
					
					InPos += TempSht;
					if (InPos >= GrcLen)
						*((char*)NULL) = 'x';
					
					if (InPos == TempTInf->loopOfs)
					{
						if (LoopCnt == 0xFFFF)
							LoopCnt = 0;
						LoopCnt ++;
						if (LoopCnt < 0x80)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x6F, (UINT8)LoopCnt);
						
						if (LoopCnt >= TempTInf->loopTimes)
							TrkEnd = true;
						
						MidChnVol |= 0x80;		// set Bit 7 for to force writing it the Volume again
						LastModType = 0xFF;
					}
					break;
				case 0xFB:	// Set Modulation
					TempByt = GrcData[InPos];
					InPos ++;
					
					if (TempByt & 0x80)
						printf("Warning: Modulation Type %02X used!\n", TempByt);
					if (TempByt)
					{
						if (LastModType != TempByt)
						{
							LastModType = TempByt;
							
							if (EnableSMPSMod)
							{
								CopySMPSModData(MOD_DATA[LastModType], ModDataMem);
								
								for (TempByt = 0x00; TempByt < 0x04; TempByt ++)
									WriteEvent(&midFileInf, &MTS, 0xB0, 0x10 | TempByt, ModDataMem[TempByt]);
								WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, ModDataMem[0x04]);
							}
						}
						
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x21, LastModType);
						if (! EnableSMPSMod)
							WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, 0x40);
					}
					else
					{
						WriteEvent(&midFileInf, &MTS, 0xB0, 0x01, TempByt);
					}
					break;
				case 0xFC:	// set Instrument
					ChnIns = GrcData[InPos];
					InPos ++;
					
					if (MTS.midChn != 0x09)	// not on DAC channel
						WriteEvent(&midFileInf, &MTS, 0xC0, ChnIns, 0x00);
					break;
				case 0xFD:	// set Default Note Length
					DefNoteLen = GrcData[InPos];
					InPos ++;
					break;
				case 0xFE:	// Hold Note ("no attack" mode)
					HoldNote = true;
					break;
				case 0xFF:	// Track End
					TrkEnd = true;
					break;
				default:
					printf("Unknown event %02X on track %X\n", CurCmd, CurTrk);
					TrkEnd = true;
					break;
				}
			}
		}
		if (LastNote != 0xFF)
			WriteEvent(&midFileInf, &MTS, 0x90, LastNote, 0x00);
		
		WriteEvent(&midFileInf, &MTS, 0xFF, 0x2F, 0x00);
		
		WriteMidiTrackEnd(&midFileInf, &MTS);
	}
	MidData = midFileInf.data;
	MidLen = midFileInf.pos;
	
	return 0x00;
}

static void PreparseGrc(UINT32 GrcLen, const UINT8* GrcData, UINT8* GrcBuf, TRK_INF* TrkInf, UINT8 Mode)
{
	// Note: GrcBuf is a temporary buffer with a size of GrcLen bytes.
	//       It is used to find loops by marking processed bytes.
	//       A loop is found when a GoTo jumps to an already processed byte.
	//
	//       The buffer has to be allocated by the calling function to speed the program
	//       up by saving a few mallocs.
	
	UINT16 InPos;
	UINT8 CurCmd;
	UINT8 StackPos;
	UINT16 StackAddr[0x04];
	UINT16 TempSht;
	UINT8 TempByt;
	UINT8 DefNoteLen;
	UINT8 LocLoopCount;
	UINT8 Mask;
	UINT16 MaskMinPos[0x04];
	UINT16 MaskMaxPos[0x04];
	
	if (! Mode)
	{
		TrkInf->loopOfs = 0x0000;
		TrkInf->maxVol = 0x7F;
	}
	if (! (TrkInf->flags & 0x80))
		return;	// Track inactive - return
	
	if (! (Mode & 0x01))
		memset(GrcBuf, 0x00, GrcLen);
	InPos = TrkInf->startOfs;
	StackPos = 0x00;
	DefNoteLen = 0x00;
	LocLoopCount = 0x80;
	Mask = 0x01;
	
	while(InPos < GrcLen)
	{
		if ((Mode & 0x01) && InPos == TrkInf->loopOfs)
			return;
		
		CurCmd = GrcData[InPos];
		GrcBuf[InPos] |= Mask;
		InPos ++;
		if (! (CurCmd & 0x80))
		{
			if (! (CurCmd & 0x10))
			{
				TempByt = DefNoteLen;
			}
			else
			{
				TempByt = GrcData[InPos];
				GrcBuf[InPos] |= Mask;
				InPos ++;
			}
			if (! (Mode & 0x01))
				TrkInf->tickCnt += TempByt;
			else
				TrkInf->loopTick += TempByt;
		}
		else if ((CurCmd & 0xE0) == 0x80)
		{
			if ((CurCmd & 0xF0) == 0x80)
			{
				if (Mode & 0x80)
					TempByt = VOL_TABLE_PSG[CurCmd & 0x0F];
				else
					TempByt = VOL_TABLE_FM[CurCmd & 0x0F];
				if (TrkInf->maxVol > TempByt)
					TrkInf->maxVol = TempByt;
			}
		}
		else
		{
			switch(CurCmd)
			{
			case 0xEC:	// Note Stop
				GrcBuf[InPos] |= Mask;
				InPos ++;
				break;
			case 0xED:	// Noise Mode
				GrcBuf[InPos] |= Mask;
				InPos ++;
				break;
			case 0xEE:	// Loop
				GrcBuf[InPos + 0x00] |= Mask;
				GrcBuf[InPos + 0x01] |= Mask;
				GrcBuf[InPos + 0x02] |= Mask;
				
				if (LocLoopCount)
				{
					if (LocLoopCount & 0x80)
						LocLoopCount = GrcData[InPos];
					LocLoopCount --;
					if (LocLoopCount)
					{
						TempSht = ReadLE16(&GrcData[InPos + 0x01]);
						InPos --;
						InPos += TempSht;
						if (InPos >= GrcLen)
							return;
						break;
					}
				}
				
				LocLoopCount = 0x80;
				InPos += 0x03;
				break;
			case 0xEF:	// set Detune
				GrcBuf[InPos] |= Mask;
				InPos ++;
				break;
			case 0xF0:	// reset SFX ID
				break;
			case 0xF1:	// set Volume
				TempByt = GrcData[InPos] & 0x7F;
				if (TrkInf->maxVol > TempByt)
					TrkInf->maxVol = TempByt;
				
				GrcBuf[InPos] |= Mask;
				InPos ++;
				break;
			case 0xF2:	// Enable/Disable DAC
				break;
			case 0xF3:	// set Fade Speed
				GrcBuf[InPos] |= Mask;
				InPos ++;
				break;
			case 0xF4:	// synchronize all tracks
				break;
			case 0xF5:	// set YM2612 Timer B
				GrcBuf[InPos] |= Mask;
				InPos ++;
				break;
			case 0xF6:	// set AMS/FMS
				GrcBuf[InPos] |= Mask;
				InPos ++;
				break;
			case 0xF7:	// set LFO rate
				GrcBuf[InPos] |= Mask;
				InPos ++;
				break;
			case 0xF8:	// Return from GoSub
				if (! StackPos)
					return;
				
				if (MaskMaxPos[StackPos] < InPos)
					MaskMaxPos[StackPos] = InPos;
				for (InPos = MaskMinPos[StackPos]; InPos < MaskMaxPos[StackPos]; InPos ++)
					GrcBuf[InPos] &= ~Mask;	// remove usage mask of this subroutine
				Mask >>= 1;
				StackPos --;
				InPos = StackAddr[StackPos];
				break;
			case 0xF9:	// GoSub
				TempSht = ReadLE16(&GrcData[InPos]);
				InPos --;
				
				StackAddr[StackPos] = InPos + 0x03;
				StackPos ++;
				Mask <<= 1;
				InPos += TempSht;
				
				MaskMinPos[StackPos] = InPos;
				MaskMaxPos[StackPos] = InPos;
				break;
			case 0xFA:	// GoTo
				if (MaskMaxPos[StackPos] < InPos)
					MaskMaxPos[StackPos] = InPos;
				TempSht = ReadLE16(&GrcData[InPos]);
				InPos --;
				
				InPos += TempSht;
				if (InPos >= GrcLen)
					return;
				if (MaskMinPos[StackPos] > InPos)
					MaskMinPos[StackPos] = InPos;
				
				if (GrcBuf[InPos] & Mask)
				{
					TrkInf->loopOfs = InPos;
					return;
				}
				break;
			case 0xFB:	// Set Modulation
				GrcBuf[InPos] |= Mask;
				InPos ++;
				break;
			case 0xFC:	// set Instrument
				GrcBuf[InPos] |= Mask;
				InPos ++;
				break;
			case 0xFD:	// set Default Note Length
				DefNoteLen = GrcData[InPos];
				GrcBuf[InPos] |= Mask;
				InPos ++;
				break;
			case 0xFE:	// Hold Note ("no attack" mode)
				break;
			case 0xFF:	// Track End
				return;
			default:
				return;
			}
		}
	}
	
	return;
}

static UINT16 ReadLE16(const UINT8* Buffer)
{
	return	(Buffer[0x01] << 8) |
			(Buffer[0x00] << 0);
}

static float OPN2DB(UINT8 TL, UINT8 PanMode, bool VolBoost)
{
	if (TL >= 0x7F)
		return -999.9f;
	if (PanMode & 0x01)
		TL += 0x04;
	if (! VolBoost && ! (PanMode & 0x80))
	{
		if (TL >= 8)
			TL -= 8;
		else
			TL = 0;
	}
	return -(TL * 3 / 4.0f);	// 8 steps per 6 db
}

static float PSG2DB(UINT8 Vol)
{
	if (Vol >= 0x78)	// 0x78-0x7F -> PSG volume 0x0F == silence
		return -999.9f;
	//return -(Vol / 8 * 2.0f);
	return -(Vol / 4.0f);	// 3 PSG steps per 6 db, makes 24 internal steps per 6 db
}

static UINT8 DB2Mid(float DB)
{
	return (UINT8)(pow(10.0, DB / 40.0) * 0x7F + 0.5);
}

static void CopySMPSModData(const UINT8* RawData, UINT8* MidVals)
{
	INT16 ModDelta;
	
	MidVals[0x00] = RawData[0x00];
	MidVals[0x01] = RawData[0x01];
	MidVals[0x02] = RawData[0x02];
	MidVals[0x03] = RawData[0x03];
	
	if (MidVals[0x02] >= 0x40 && MidVals[0x02] < 0xC0)
		MidVals[0x01] |= 0x20;
	MidVals[0x02] &= 0x7F;
	
	if (MidVals[0x03] >= 0x40 && MidVals[0x03] < 0xC0)
		MidVals[0x01] |= 0x40;
	MidVals[0x03] &= 0x7F;
	
	ModDelta = (INT8)RawData[0x02] * RawData[0x03];
	if (ModDelta < 0)
		ModDelta = -ModDelta;
	
	ModDelta *= 2;
	if (ModDelta < 0x08)
		MidVals[0x04] = 0x08;
	else if (ModDelta > 0x7F)
		MidVals[0x04] = 0x7F;
	else
		MidVals[0x04] = (UINT8)ModDelta;
	
	return;
}


void SaveInsAsGYB(const char* FileName, const UINT8* InsData)
{
	const UINT8 INS_REG_MAP[0x20] =
	{	0x01, 0x03, 0x02, 0x04,		// 30-3C
		0x05, 0x07, 0x06, 0x08,		// 40-4C
		0x09, 0x0B, 0x0A, 0x0C,		// 50-5C
		0x0D, 0x0F, 0x0E, 0x10,		// 60-6C
		0x11, 0x13, 0x12, 0x14,		// 70-7C
		0x15, 0x17, 0x16, 0x18,		// 80-8C
		0xFF, 0xFF, 0xFF, 0xFF,		// 90-9C
		0x00, 0xFF, 0xFF, 0xFF};	// B0, B4, Extra, Padding
	FILE* hFile;
	UINT8 InsCount;
	UINT8 CurIns;
	UINT8 CurReg;
	const UINT8* InsPtr;
	char TempStr[0x80];
	UINT8 GybIns[0x20];	// GYB instrument data buffer
	
	InsPtr = InsData;
	for (InsCount = 0x00; InsCount < 0xFF; InsCount ++, InsPtr += 0x19)
	{
		if (InsPtr[0x00] & 0xC0)	// check unused bits in B0 register
			break;	// if set - exit
	}
	printf("Instruments counted: 0x%02X\n", InsCount);
	
	hFile = fopen(FileName, "wb");
	if (hFile == NULL)
	{
		printf("Error opening %s!\n", FileName);
		return;
	}
	
	// Write Header
	fputc(26, hFile);	// Signature Byte 1
	fputc(12, hFile);	// Signature Byte 2
	fputc(0x02, hFile);	// Version
	fputc(InsCount, hFile);	// Melody Instruments
	fputc(0x00, hFile);		// Drum Instruments
	
	// Write Mappings
	for (CurIns = 0x00; CurIns < InsCount && CurIns < 0x80; CurIns ++)
	{
		fputc(CurIns, hFile);	// GM Mapping: Melody
		fputc(0xFF, hFile);		// GM Mapping: Drum
	}
	for (; CurIns < 0x80; CurIns ++)
	{
		fputc(0xFF, hFile);
		fputc(0xFF, hFile);
	}
	
	fputc(0x00, hFile);	// LFO Value
	
	// Write Instrument Data
	InsPtr = InsData;
	for (CurIns = 0x00; CurIns < InsCount; CurIns ++, InsPtr += 0x19)
	{
		for (CurReg = 0x00; CurReg < 0x20; CurReg ++)
		{
			if (INS_REG_MAP[CurReg] == 0xFF)
				GybIns[CurReg] = 0x00;
			else
				GybIns[CurReg] = InsPtr[INS_REG_MAP[CurReg]];
		}
		fwrite(GybIns, 0x01, 0x20, hFile);
	}
	
	// Write Instrument Names
	for (CurIns = 0x00; CurIns < InsCount; CurIns ++)
	{
		sprintf(TempStr, "Instrument %02X", CurIns);
		
		CurReg = (UINT8)strlen(TempStr);
		fputc(CurReg, hFile);
		fwrite(TempStr, 0x01, CurReg, hFile);
	}
	
	fputc(0x00, hFile);	// Fake Checksum
	fputc(0x00, hFile);
	fputc(0x00, hFile);
	fputc(0x00, hFile);
	
	fclose(hFile);
	printf("Done.\n");
	
	return;
}
