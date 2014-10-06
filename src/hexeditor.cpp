
/***********************************************
 Hex Editor for "Gens-rr r57shell mod"
 Copyright (c) by feos, 2013-2014
 
 Features:
 - View any memory region (easy to add new ones)
 - Edit chars and numbers (including copy/paste)
 - Observe symbolic names
 - Smart sizing

 TODO:
 - Freeze values
 - Color values
 - User defined symbolic names
 - Context menu
 - More...
************************************************/

#include "resource.h"
#include "gens.h"
#include "mem_m68k.h"
#include "mem_s68k.h"
#include "mem_sh2.h"
#include "mem_z80.h"
#include "G_main.h"
#include "G_ddraw.h"
#include "G_dsound.h"
#include "vdp_io.h"
#include "Star_68k.h"
#include "z80.h"
#include "ram_search.h"
#include "hexeditor.h"
#include "save.h"
#include <windowsx.h>
#include <vector>

HWND HexEditorHWnd;
HMENU HexEditorMenu;
HMENU HexRegionsMenu;
HDC HexDC;
SCROLLINFO HexSI;
HFONT HexFont = 0;
HBRUSH BrushBlack = CreateSolidBrush(RGB(  0,  0,  0));
HBRUSH BrushWhite = CreateSolidBrush(RGB(255,255,255));
MousePos MouseArea = NO;
std::vector<SymbolName> HardNames;
std::vector<Patch> HardPatches;
char InputDigit = 0;
bool
	MouseButtonHeld = 0,
	HexStarted = 0,
	SecondDigitPrompted = 0;
UINT
	ClientTopGap = 0,	// How much client area is shifted
	ClientXGap = 0,		// Total diff between client and dialog widths
	RowCount = 16;		// Offset consists of 16 bytes

HexRegion HexRegions[] = {
	{"ROM",        (u8 *)Rom_Data,              0, sizeof(Rom_Data),                true, 1},
	{"RAM M68K",   (u8 *)Ram_68k,        0xFF0000, sizeof(Ram_68k),                 true, 1},
	{"RAM Z80",    (u8 *)Ram_Z80,        0xA00000, sizeof(Ram_Z80),                 true, 1},
	{"VRAM",       (u8 *)VRam,                  0, sizeof(VRam),                    true, 1},
	{"CRAM",       (u8 *)CRam,                  0, sizeof(CRam),                    true, 1},
 	{"Regs M68K",  (u8 *)&main68k_context.dreg, 0, sizeof(int)*16,                  true, 3},
	{"Regs Z80",   (u8 *)&M_Z80,                0, sizeof(int)*14,                  true, 3},
	{"Regs VDP",   (u8 *)&VDP_Reg,              0, sizeof(VDP_Reg),                 true, 0},
	{"RAM CD PRG", (u8 *)Ram_Prg,         0x20000, sizeof(Ram_Prg),     !!SegaCD_Started, 1},
	{"RAM CD 1M",  (u8 *)Ram_Word_1M,    0x200000, sizeof(Ram_Word_1M), !!SegaCD_Started, 1},
	{"RAM CD 2M",  (u8 *)Ram_Word_2M,    0x200000, sizeof(Ram_Word_2M), !!SegaCD_Started, 1},
	{"RAM 32X",    (u8 *)_32X_Ram,      0x6000000, sizeof(_32X_Ram),    !!  _32X_Started, 0}
};

HexParameters Hex = {
	1, 1,							// text area and lines visible
	0, 17, 8, Hex.FontBold?600:400,	// font				// bold, height, width, weight
	Hex.FontWidth,					// vertical gap 
	Hex.FontWidth * 9,				// header gap		// X
	Hex.FontHeight,					// header gap		// Y
	Hex.FontHeight,					// cell				// height
	Hex.FontWidth*3,				// cell				// width
	0, 0,							// dialog pos		// X, Y
	0, 16,							// visible offset	// first, total
	0, 0, 0,						// selected address // first, total, last
	0x00000000, 0x00ffffff,			// colors			// font, BG
	HexRegions[1]					// current region	// m68k ram
};

RECT
	CellArea = {
		Hex.GapHeaderX,
		Hex.GapHeaderY,
		Hex.GapHeaderX + Hex.Gap + Hex.CellWidth * RowCount,
		Hex.GapHeaderY + Hex.CellHeight* Hex.OffsetVisibleTotal
	},
	TextArea = {
		Hex.GapHeaderX + Hex.Gap + Hex.CellWidth * RowCount,
		Hex.GapHeaderY,
		Hex.GapHeaderX + Hex.Gap * 2 + Hex.CellWidth * RowCount + Hex.FontWidth * RowCount,
		Hex.GapHeaderY + Hex.CellHeight* Hex.OffsetVisibleTotal
	};

#define CLIENT_WIDTH	(Hex.TextView ? TextArea.right : CellArea.right)
#define CLIENT_HEIGHT	(Hex.CellHeight * (Hex.OffsetVisibleTotal + 1) + 1)
#define LAST_OFFSET		(Hex.OffsetVisibleFirst + Hex.OffsetVisibleTotal)
#define LAST_ADDRESS	(Hex.OffsetVisibleFirst + Hex.OffsetVisibleTotal * RowCount - 1)
#define SELECTION_START	min(Hex.AddressSelectedFirst, Hex.AddressSelectedLast)
#define SELECTION_END	max(Hex.AddressSelectedFirst, Hex.AddressSelectedLast)
#define REGION_COUNT    sizeof(HexRegions)/sizeof(HexRegions)[0]
#define OFFSET_REMINDER Hex.CurrentRegion.Size % RowCount
#define GAP_CHECK		(row / 8 * Hex.Gap)

void HexRepeatPatch(u8 *Array, UINT Start, UINT Size) {
	for (UINT i = 0; i < HardPatches.size(); i++) {
		if (HardPatches[i].Array == Array &&
			HardPatches[i].Start == Start)
			Array[Start] = HardPatches[i].Value;
	}
}

int HexCap(int Val1, int Val2, bool GreaterThan) {
	if      ( GreaterThan && Val1 > Val2) Val1 = Val2;
	else if (!GreaterThan && Val1 < Val2) Val1 = Val2;
	return Val1;
}

void HexSetColors(HDC hDC, bool Selection) {
	if (Selection) {
		SetBkColor  (hDC, Hex.ColorFont);
		SetTextColor(hDC, Hex.ColorBG  );		
	} else {
		SetBkColor  (hDC, Hex.ColorBG  );
		SetTextColor(hDC, Hex.ColorFont);
	}
}

void HexUpdateDialog(bool ClearBG) {
	if (ClearBG)
		InvalidateRect(HexEditorHWnd, NULL, TRUE );
	else
		InvalidateRect(HexEditorHWnd, NULL, FALSE);
}

void HexAddName(u8* Array, UINT Start, UINT Length, char *Name) {
	char *buf = (char *)malloc(strlen(Name)+1);
	sprintf(buf, Name);
	SymbolName Instance = {Array, Start, Length, buf};
	HardNames.push_back(Instance);
}

void HexLoadSymbols() {
	int size;
	char buf[60];
	u8 *Array;
	// M68K Regs names
	Array = (u8 *)&main68k_context.dreg;
	size = sizeof(int);
	for (int i = 0; i < 8; i++) {
		sprintf(buf, "D%d", i);
		HexAddName(Array,      size*i, size, buf);
		sprintf(buf, "A%d", i);
		HexAddName(Array, 0x20+size*i, size, buf);
	}
	// Z80 Regs names
	Array = (u8 *)&M_Z80;
	HexAddName(Array, size* 0, size, "AF");
	HexAddName(Array, size* 1, size, "BC");
	HexAddName(Array, size* 2, size, "DE");
	HexAddName(Array, size* 3, size, "HL");
	HexAddName(Array, size* 4, size, "IX");
	HexAddName(Array, size* 5, size, "IY");
	HexAddName(Array, size* 6, size, "PC");
	HexAddName(Array, size* 7, size, "SP");
	HexAddName(Array, size* 8, size, "AF2");
	HexAddName(Array, size* 9, size, "BC2");
	HexAddName(Array, size*10, size, "DE2");
	HexAddName(Array, size*11, size, "HL2");
	HexAddName(Array, size*12, size, "IFF");
	HexAddName(Array, size*13, size, "R");
	// VDP Regs names
	Array = (u8 *)&VDP_Reg;
	HexAddName(Array, size* 0, size, "Set1");
	HexAddName(Array, size* 1, size, "Set2");
	HexAddName(Array, size* 2, size, "Pat_ScrA_Adr");
	HexAddName(Array, size* 3, size, "Pat_Win_Adr");
	HexAddName(Array, size* 4, size, "Pat_ScrB_Adr");
	HexAddName(Array, size* 5, size, "Spr_Att_Adr");
	HexAddName(Array, size* 6, size, "Reg6");
	HexAddName(Array, size* 7, size, "BG_Color");
	HexAddName(Array, size* 8, size, "Reg8");
	HexAddName(Array, size* 9, size, "Reg9");
	HexAddName(Array, size*10, size, "H_Int");
	HexAddName(Array, size*11, size, "Set3");
	HexAddName(Array, size*12, size, "Set4");
	HexAddName(Array, size*13, size, "H_Scr_Adr");
	HexAddName(Array, size*14, size, "Reg14");
	HexAddName(Array, size*15, size, "Auto_Inc");
	HexAddName(Array, size*16, size, "Scr_Size");
	HexAddName(Array, size*17, size, "Win_H_Pos");
	HexAddName(Array, size*18, size, "Win_V_Pos");
	HexAddName(Array, size*19, size, "DMA_Length_L");
	HexAddName(Array, size*20, size, "DMA_Length_H");
	HexAddName(Array, size*21, size, "DMA_Src_Adr_L");
	HexAddName(Array, size*22, size, "DMA_Src_Adr_M");
	HexAddName(Array, size*23, size, "DMA_Src_Adr_H");
	HexAddName(Array, size*24, size, "DMA_Length");
	HexAddName(Array, size*25, size, "DMA_Address");
}

void HexCastName(char *buf, UINT size, UINT Address) {
	sprintf(buf, "");
	UINT Offset;
	for (UINT i = 0; i < HardNames.size(); i++) {
		if (HardNames[i].Array == Hex.CurrentRegion.Array &&
			HardNames[i].Start <= Address &&
			HardNames[i].Start + HardNames[i].Size > Address) {
			if (HardNames[i].Size > 1) {
				Offset = Address - HardNames[i].Start;
				_snprintf(buf, size, " : %s[%d]", HardNames[i].Name, Offset);
			} else
				_snprintf(buf, size, " : %s", HardNames[i].Name);
		}
	}
}

void HexUpdateCaption() {
	char str[100];
	char area[12];
	if (MouseArea == TEXT)
		sprintf(area, "Chars");
	else
		sprintf(area, Hex.CurrentRegion.Name);
	if (Hex.AddressSelectedTotal == 0)
		sprintf(str, "Memory Dump: %s", area);
	else if (Hex.AddressSelectedTotal == 1) {
		char *name = (char *)malloc(60);
		HexCastName(name, 60, SELECTION_START);
		sprintf(str, "%s: $%06X%s", area,
			Hex.AddressSelectedFirst + Hex.CurrentRegion.Offset, name);
		free(name);
	}
	else if (Hex.AddressSelectedTotal > 1)
		sprintf(str, "%s: $%06X - $%06X (%d)", area,
				SELECTION_START + Hex.CurrentRegion.Offset,
				SELECTION_END   + Hex.CurrentRegion.Offset,
				Hex.AddressSelectedTotal);
	SetWindowText(HexEditorHWnd, str);
	return;
}

void HexUpdateScrollInfo() {
	ZeroMemory(&HexSI, sizeof(SCROLLINFO));
	HexSI.cbSize = sizeof(HexSI);
	HexSI.fMask  = SIF_ALL;
	HexSI.nMin   = 0;
	HexSI.nMax   = Hex.CurrentRegion.Size / RowCount + (OFFSET_REMINDER > 0);
	HexSI.nPage  = Hex.OffsetVisibleTotal;
	HexSI.nPos   = Hex.OffsetVisibleFirst / RowCount;
}

int HexGetMouseAddress(LPARAM lParam) {
	int Address;
	POINT Mouse;
	POINTSTOPOINT(Mouse, MAKEPOINTS(lParam));
	Mouse.x = HexCap(Mouse.x, CellArea.left, 0);
	Mouse.x = HexCap(Mouse.x, TextArea.right - 1, 1);
	if (Mouse.x > (int)Hex.CellWidth * 8 + CellArea.left)
		Mouse.x = Mouse.x - Hex.Gap / 2;
	else
		Mouse.x = Mouse.x + Hex.Gap / 2; // todo: get rid of this line
	if (Mouse.x < CellArea.right) {
		MouseArea = CELL;
		Address = (Mouse.y - CellArea.top ) / (int)Hex.CellHeight * RowCount +
				  (Mouse.x - CellArea.left) / (int)Hex.CellWidth + Hex.OffsetVisibleFirst;
	} else if (Mouse.x >= CellArea.right && Hex.TextView) {
		MouseArea = TEXT;
		Address = (Mouse.y - TextArea.top ) / (int)Hex.CellHeight * RowCount +
				  (Mouse.x - TextArea.left) / (int)Hex.FontWidth + Hex.OffsetVisibleFirst;
	} else {
		MouseArea = NO;
		Address = -1;
	}
	return Address;
}

void HexSelectAddress(int Address, bool ButtonDown) {
	if (MouseArea == NO) return;
	else {
		Address = HexCap(Address, 0, 0);
		Address = HexCap(Address, int(Hex.CurrentRegion.Size - 1), 1);
		if (ButtonDown) {
			Hex.AddressSelectedFirst = Address;
			Hex.AddressSelectedLast  = Address;
			Hex.AddressSelectedTotal = 1;
		} else {
			Hex.AddressSelectedLast  = Address;
			Hex.AddressSelectedTotal = SELECTION_END - SELECTION_START + 1;
		}
		if (Hex.AddressSelectedLast < Hex.OffsetVisibleFirst)
			Hex.OffsetVisibleFirst = SELECTION_START / RowCount * RowCount;
		if (Hex.AddressSelectedLast > LAST_ADDRESS)
			Hex.OffsetVisibleFirst = (SELECTION_END / RowCount - Hex.OffsetVisibleTotal + 1) * RowCount;
		SecondDigitPrompted = 0;
		HexUpdateDialog();
	}
}

void HexGoToAddress(int Address) {
	Address = HexCap(Address, 0, 0);
	Address = HexCap(Address, int(Hex.CurrentRegion.Size - 1), 1);
	Hex.AddressSelectedFirst = Address;
	Hex.AddressSelectedLast  = Address;
	Hex.AddressSelectedTotal = 1;
	if (Hex.AddressSelectedLast < Hex.OffsetVisibleFirst)
		Hex.OffsetVisibleFirst = SELECTION_START / RowCount * RowCount;
	if (Hex.AddressSelectedLast > LAST_ADDRESS)
		Hex.OffsetVisibleFirst = (SELECTION_END / RowCount - Hex.OffsetVisibleTotal + 1) * RowCount;
	SecondDigitPrompted = 0;
	HexUpdateDialog();
}

void HexCopy(char type)
{
	if (!OpenClipboard(NULL)) return;
	if (!EmptyClipboard()) return;

	char str[10];
	HGLOBAL hGlobal = GlobalAlloc(GHND,Hex.AddressSelectedTotal*2+1);
	PTSTR pGlobal = (char*)GlobalLock (hGlobal);
	if (type == 0) {
		// numbers
		for (UINT i = 0; i < Hex.AddressSelectedTotal; i++) {
			sprintf(str,"%02X",Hex.CurrentRegion.Array[(i+SELECTION_START)^Hex.CurrentRegion.Swap]);
			strcat(pGlobal,str);
		}
	} else if (type == 1) {
		// chars
		for (UINT i = 0; i < Hex.AddressSelectedTotal; i++) {
			UINT8 check = Hex.CurrentRegion.Array[(i+SELECTION_START)^Hex.CurrentRegion.Swap];
			//if((check >= 32) && (check <= 127))
				pGlobal[i] = (char) check;
			//else
			//	pGlobal[i] = '.';
		}
		pGlobal[Hex.AddressSelectedTotal] = 0;
	} else if (type == -1) {
		// address
		sprintf(str,"%06X",Hex.CurrentRegion.Offset+SELECTION_START);
		strcpy(pGlobal,str);
	} else
		return;
	GlobalUnlock(hGlobal);
	SetClipboardData(CF_TEXT, hGlobal);
	CloseClipboard();
	GlobalFree(hGlobal);
}

void HexPaste(UINT8 type) {
	char result;
	SecondDigitPrompted = 0;
	OpenClipboard(HexEditorHWnd);
	HGLOBAL hGlobal = GetClipboardData(CF_TEXT);
	if(hGlobal == NULL) {
		CloseClipboard();
		return;
	}
	PTSTR pGlobal = (char *)GlobalLock(hGlobal);
	for (UINT i = 0; i < GlobalSize(pGlobal); i++) {
		if (type == 0) {
			result = -1;
			if (pGlobal[i] == 0) {
				//if (SecondDigitPrompted)
				//	result = 0; // auto-append the missing last digit
				//else
					break;
			}
			if ((pGlobal[i] >= 'a') && (pGlobal[i] <= 'f')) result = pGlobal[i]-('a'-0xA);
			if ((pGlobal[i] >= 'A') && (pGlobal[i] <= 'F')) result = pGlobal[i]-('A'-0xA);
			if ((pGlobal[i] >= '0') && (pGlobal[i] <= '9')) result = pGlobal[i]- '0';
			if (result == -1)
				continue;
			else
				SecondDigitPrompted ^= 1;
			if (SecondDigitPrompted)
				InputDigit = result;
			else {
				InputDigit = (InputDigit << 4) + result;
				Hex.CurrentRegion.Array[Hex.AddressSelectedFirst ^ Hex.CurrentRegion.Swap] = InputDigit;
				HexSelectAddress(Hex.AddressSelectedFirst + 1, 1);
			}
		} else if (type == 1) {
			Hex.CurrentRegion.Array[Hex.AddressSelectedFirst ^ Hex.CurrentRegion.Swap] = pGlobal[i];
			if ((Hex.AddressSelectedFirst < Hex.CurrentRegion.Size - 1) && (pGlobal[i] != 0))
				HexSelectAddress(Hex.AddressSelectedFirst + 1, 1);
			else
				break;
		} else
			return;
	}
	GlobalUnlock(hGlobal);
	CloseClipboard();
	HexUpdateDialog();
}

void HexDestroySelection() {
	MouseArea = NO;
	Hex.AddressSelectedFirst = 0;
	Hex.AddressSelectedTotal = 0;
	Hex.AddressSelectedLast = 0;
	SecondDigitPrompted = 0;
}

void HexDestroyDialog() {
	RECT r;
	for (UINT i = 0; i < HardNames.size(); i++) {
		free(HardNames[i].Name);
	}
	HardNames.~vector();
	GetWindowRect(HexEditorHWnd, &r);
	Hex.DialogPosX = r.left;
	Hex.DialogPosY = r.top;
	DialogsOpen--;
	HexDestroySelection();
	ReleaseDC(HexEditorHWnd, HexDC);
	DestroyWindow(HexEditorHWnd);
	UnregisterClass("HEXEDITOR", ghInstance);
	DeleteObject(HexFont);
	HexFont = 0;
	HexEditorHWnd = 0;
	HexStarted = 0;
	return;
}

void HexSwitchRegion() {
	RECT r;
	GetClientRect(HexEditorHWnd, &r);
	HexSI.nPage = r.bottom / Hex.CellHeight - 1;
	for (int i=0; i<REGION_COUNT, HexRegions[i].Active; i++)
		CheckMenuItem(HexRegionsMenu, IDC_C_HEX_REGION+i,
			(Hex.CurrentRegion.Array == HexRegions[i].Array) ? MF_CHECKED : MF_UNCHECKED);
	Hex.OffsetVisibleFirst = 0;
	Hex.OffsetVisibleTotal = HexCap(HexSI.nPage,
		Hex.CurrentRegion.Size/RowCount + (OFFSET_REMINDER > 0), 1);
	HexDestroySelection();
	HexUpdateScrollInfo();
	HexUpdateDialog(1);
	HexUpdateCaption();
}

LRESULT CALLBACK HexGoToProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	RECT hr;
	switch(uMsg) {
		case WM_INITDIALOG:
			Clear_Sound_Buffer();
			if (Full_Screen)
			{
				while (ShowCursor(false) >= 0);
				while (ShowCursor(true) < 0);
			}
			GetWindowRect(HexEditorHWnd, &hr);
			SetWindowPos(hDlg, NULL, hr.left, hr.top, NULL, NULL, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
			strcpy(Str_Tmp, "Type address to go to.");
			SendDlgItemMessage(hDlg, IDC_PROMPT_TEXT, WM_SETTEXT, 0, (LPARAM)Str_Tmp);
			strcpy(Str_Tmp, "Format: FF****");
			SendDlgItemMessage(hDlg, IDC_PROMPT_TEXT2, WM_SETTEXT, 0, (LPARAM)Str_Tmp);
			return true;
			break;

		case WM_COMMAND:
			switch(LOWORD(wParam)) {
			case IDOK: {
				if (Full_Screen) {
					while (ShowCursor(true) < 0);
					while (ShowCursor(false) >= 0);
				}
				GetDlgItemText(hDlg, IDC_PROMPT_EDIT, Str_Tmp, 10);
				int Address;
				if ((strnicmp(Str_Tmp, "ff", 2) == 0) && (sscanf(Str_Tmp+2, "%x", &Address)))
					HexGoToAddress(Address);
				DialogsOpen--;
				EndDialog(hDlg, true);
				return true;
				}
				break;
			case ID_CANCEL:
			case IDCANCEL:
				if (Full_Screen) {
					while (ShowCursor(true) < 0);
					while (ShowCursor(false) >= 0);
				}
				DialogsOpen--;
				EndDialog(hDlg, false);
				return false;
				break;
			}
			break;

		case WM_CLOSE:
			if (Full_Screen) {
				while (ShowCursor(true) < 0);
				while (ShowCursor(false) >= 0);
			}
			DialogsOpen--;
			EndDialog(hDlg, false);
			return false;
			break;
	}
	return false;
}

LRESULT CALLBACK HexEditorProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	RECT r, wr, cr;
	PAINTSTRUCT ps;
	switch (uMsg) {
	case WM_CREATE: {
		HexEditorMenu = GetMenu(hDlg);
		HexRegionsMenu = CreatePopupMenu();
		InsertMenu(HexEditorMenu, GetMenuItemCount(HexEditorMenu)+1, MF_BYPOSITION | MF_POPUP | MF_STRING,
			(UINT)HexRegionsMenu, "&Region");
		for (int i=0; i<REGION_COUNT, HexRegions[i].Active; i++)
			InsertMenu(HexRegionsMenu, i,
				(Hex.CurrentRegion.Array == HexRegions[i].Array) ? MF_CHECKED : MF_UNCHECKED,
				IDC_C_HEX_REGION+i, HexRegions[i].Name);
		HexDC = GetDC(hDlg);
		SelectObject(HexDC, HexFont);
		SetTextAlign(HexDC, TA_UPDATECP | TA_TOP | TA_LEFT);
		if (Full_Screen) {
			while (ShowCursor(false) >= 0);
			while (ShowCursor(true) < 0);
		}
		SetRect(&r, 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT);
		// Automatic adjust to account for menu and OS style, manual for scrollbar
		int ScrollbarWidth = GetSystemMetrics(SM_CXVSCROLL);
		AdjustWindowRectEx(&r, GetWindowLong(hDlg, GWL_STYLE),
			(GetMenu(hDlg) > 0), GetWindowLong(hDlg, GWL_EXSTYLE));
		SetWindowPos(hDlg, NULL, Hex.DialogPosX, Hex.DialogPosY,
			r.right - r.left + ScrollbarWidth, r.bottom - r.top,
			SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
		GetClientRect(hDlg, &cr);
		ClientTopGap = r.bottom - r.top - cr.bottom + 1;
		ClientXGap = r.right - r.left - CLIENT_WIDTH + ScrollbarWidth;
		Hex.AddressSelectedTotal = 0;
		HexUpdateScrollInfo();
		SetScrollInfo(hDlg, SB_VERT, &HexSI, TRUE);
		HexStarted = 1;
		return 0;
		break;
	}

	case WM_PAINT: {
		static char buf[10];
		unsigned int row = 0, line = 0;
		GetWindowRect(hDlg, &wr);
		BeginPaint(hDlg, &ps);
		// TOP HEADER, static.
		for (row = 0; row < RowCount; row++) {
			MoveToEx(HexDC, row * Hex.CellWidth + CellArea.left + GAP_CHECK, -1, NULL);
			HexSetColors(HexDC, 0);
			sprintf(buf, "%2X", row);
			TextOut(HexDC, 0, 0, buf, strlen(buf));
		}
		// LEFT HEADER, semi-dynamic.
		for (line = 0; line < Hex.OffsetVisibleTotal; line++) {
			MoveToEx(HexDC, Hex.Gap / 2, line * Hex.CellHeight + CellArea.top, NULL);
			HexSetColors(HexDC, 0);
			sprintf(buf, "%06X:", Hex.OffsetVisibleFirst + line * RowCount + Hex.CurrentRegion.Offset);
			TextOut(HexDC, 0, 0, buf, strlen(buf));
		}
		// RAM, dynamic.
		for (line = 0; line < Hex.OffsetVisibleTotal; line++) {
			for (row = 0; row < RowCount; row++) {
				UINT carriage = Hex.OffsetVisibleFirst + line * RowCount + row;
				if (carriage > int(Hex.CurrentRegion.Size - 1))
					break;
				// Print numbers in main area
				MoveToEx(HexDC, row * Hex.CellWidth + CellArea.left + GAP_CHECK,
					line * Hex.CellHeight + CellArea.top, NULL);
				RECT r0 = {
					row  * Hex.CellWidth  + CellArea.left - Hex.FontWidth / 2 + GAP_CHECK,
					line * Hex.CellHeight + CellArea.top,
					row  * Hex.CellWidth  + CellArea.left + Hex.FontWidth / 2 * 5 + GAP_CHECK,
					line * Hex.CellHeight + CellArea.top  + Hex.CellHeight
				};
				if ((Hex.AddressSelectedTotal)    &&
					(carriage >= SELECTION_START) &&
					(carriage <= SELECTION_END)) {
					HexSetColors(HexDC, 1);
					FillRect(HexDC, &r0, BrushBlack);
				}
				else {
					HexSetColors(HexDC, 0);
					FillRect(HexDC, &r0, BrushWhite);
				}
				if (SecondDigitPrompted && carriage == SELECTION_START)
					sprintf(buf, "%1X_", InputDigit);
				else
					sprintf(buf, "%02X", Hex.CurrentRegion.Array[carriage ^ Hex.CurrentRegion.Swap]);
				TextOut(HexDC, 0, 0, buf, strlen(buf));
				// Print chars on the right
				if (Hex.TextView) {
					MoveToEx(HexDC, row * Hex.FontWidth + TextArea.left + Hex.Gap / 2,
						line * Hex.CellHeight + CellArea.top, NULL);
					if ((Hex.AddressSelectedTotal) && (carriage >= SELECTION_START) && (carriage <= SELECTION_END))
						HexSetColors(HexDC, 1);
					else
						HexSetColors(HexDC, 0);
					UINT8 check = Hex.CurrentRegion.Array[carriage^Hex.CurrentRegion.Swap];
					if((check >= 0x20) && (check <= 0x7e))
						buf[0] = (char) check;
					else
						buf[0] = '.';
					TextOut(HexDC, 0, 0, buf, 1);
				}
			}
		}
		if (Hex.DrawLines) {
			MoveToEx(HexDC, 0, CellArea.top - 1, NULL);
			LineTo  (HexDC, CLIENT_WIDTH, CellArea.top - 1);						// horizontal
			MoveToEx(HexDC, CellArea.left - Hex.FontWidth,     0, NULL);			// vertical left
			LineTo  (HexDC, CellArea.left - Hex.FontWidth,     CLIENT_HEIGHT);
			MoveToEx(HexDC, CellArea.left + Hex.CellWidth * 8, 0, NULL);		// vertical middle
			LineTo  (HexDC, CellArea.left + Hex.CellWidth * 8, CLIENT_HEIGHT);
			if (Hex.TextView) {
				MoveToEx(HexDC, TextArea.left, 0, NULL);						// vertical right
				LineTo  (HexDC, TextArea.left, CLIENT_HEIGHT);
			}
		}
		EndPaint(hDlg, &ps);
		return 0;
		break;
	}

	case WM_INITMENU:
		CheckMenuItem(HexEditorMenu, IDC_C_HEX_LINES, Hex.DrawLines ? MF_CHECKED : MF_UNCHECKED);
		CheckMenuItem(HexEditorMenu, IDC_C_HEX_TEXT,  Hex.TextView  ? MF_CHECKED : MF_UNCHECKED);
		break;

	case WM_MENUSELECT:
 	case WM_ENTERSIZEMOVE:
		Clear_Sound_Buffer();
		break;

	case WM_COMMAND: {
			int command = LOWORD(wParam);
			if (command >= IDC_C_HEX_REGION &&
				command <  IDC_C_HEX_REGION + REGION_COUNT) {
				Hex.CurrentRegion = HexRegions[command - IDC_C_HEX_REGION];
				HexSwitchRegion();
				return 0;
			}
		}
		switch(wParam) {

		case IDC_C_HEX_LINES:
			Hex.DrawLines ^= 1;
			CheckMenuItem(HexEditorMenu, IDC_C_HEX_LINES, Hex.DrawLines ? MF_CHECKED : MF_UNCHECKED);
			HexUpdateDialog(1);
			break;

		case IDC_C_HEX_TEXT:
			Hex.TextView ^= Hex.TextView;
			CheckMenuItem(HexEditorMenu, IDC_C_HEX_TEXT, Hex.TextView ? MF_CHECKED : MF_UNCHECKED);
			GetWindowRect(hDlg, &wr);
			SetWindowPos(hDlg, NULL, wr.left, wr.top, CLIENT_WIDTH + ClientXGap, wr.bottom - wr.top,
				SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
			HexUpdateDialog();
			break;

		case IDC_C_HEX_GOTO:
			DialogsOpen++;
			DialogBox(ghInstance, MAKEINTRESOURCE(IDD_PROMPT), hDlg, (DLGPROC) HexGoToProc);
			break;

		case IDC_C_HEX_DUMP: {
			char fname[2048];
			sprintf(fname,"%s Dump.bin",Hex.CurrentRegion.Name);
			if(Change_File_S(fname,".","Save Full Dump As...","All Files\0*.*\0\0","*.*",hDlg)) {
				FILE *out=fopen(fname, "wb+");
				int i;
				for (i=0; i < sizeof(Hex.CurrentRegion.Array); ++i) {
					fname[i&2047] = Hex.CurrentRegion.Array[i^Hex.CurrentRegion.Swap];
					if ((i&2047) == 2047)
						fwrite(fname, 1, sizeof(fname), out);
				}
				fwrite(fname, 1, i & 2047, out);
				fclose(out);
			}
			break;
		}
		case IDC_C_HEX_COPY_AUTO:
			HexCopy(MouseArea == TEXT);
			break;
		case IDC_C_HEX_COPY_NUMS:
			HexCopy(0);
			break;
		case IDC_C_HEX_COPY_CHARS:
			HexCopy(1);
			break;
		case IDC_C_HEX_COPY_ADDRSESS:
			HexCopy(-1);
			break;
		case IDC_C_HEX_PASTE_AUTO:
			HexPaste(MouseArea == TEXT);
			break;
		case IDC_C_HEX_PASTE_NUMS:
			HexPaste(0);
			break;
		case IDC_C_HEX_PASTE_CHARS:
			HexPaste(1);
			break;
	}

	case WM_CHAR: {
		if (GetKeyState(VK_CONTROL) & 0x8000) return 0;
		char c[2], result = -1;
		c[0] = (char) (wParam & 0xFF);
		c[1] = 0;
		Hex.AddressSelectedFirst = Hex.AddressSelectedLast = SELECTION_START;
		if (MouseArea == TEXT) {
			Hex.CurrentRegion.Array[Hex.AddressSelectedFirst ^ Hex.CurrentRegion.Swap] = c[0];
			Hex.AddressSelectedFirst++;
			Hex.AddressSelectedLast = Hex.AddressSelectedFirst;
		} else {
			if ((c[0] >= 'a') && (c[0] <= 'f')) result = c[0]-('a'-0xA);
			if ((c[0] >= 'A') && (c[0] <= 'F')) result = c[0]-('A'-0xA);
			if ((c[0] >= '0') && (c[0] <= '9')) result = c[0]- '0';
			if (result == -1) return 0;
			SecondDigitPrompted ^= 1;
			MouseButtonHeld = 0;
			if (SecondDigitPrompted)
				InputDigit = result;
			else {
				InputDigit = (InputDigit << 4) + result;
				Hex.CurrentRegion.Array[Hex.AddressSelectedFirst ^ Hex.CurrentRegion.Swap] = InputDigit;
				HexSelectAddress(Hex.AddressSelectedFirst + 1, 1);
				Hex.AddressSelectedLast = Hex.AddressSelectedFirst;
				HexUpdateCaption();
			}
		}
		HexUpdateDialog();
		return 0;
		break;
	}

	case WM_KEYDOWN:
		if (GetKeyState(VK_CONTROL) & 0x8000) {
			switch(wParam) {
			case 0x43: // Ctrl+C
				HexEditorProc(HexEditorHWnd, WM_COMMAND, IDC_C_HEX_COPY_AUTO, 0);
				return 0;
			case 0x56: // Ctrl+V
				HexEditorProc(HexEditorHWnd, WM_COMMAND, IDC_C_HEX_PASTE_AUTO, 0);
				return 0;
			case 0x47: // Ctrl+G
				HexEditorProc(HexEditorHWnd, WM_COMMAND, IDC_C_HEX_GOTO, 0);
				return 0;
			}
		}
		HexUpdateDialog();
		return 0;
		break;
	
	case WM_LBUTTONDOWN:
		SetCapture(hDlg); // Watch mouse actions outside the client area
		HexSelectAddress(HexGetMouseAddress(lParam), 1);
		MouseButtonHeld = 1;
		HexUpdateCaption();
		return 0;
		break;

	case WM_MOUSEMOVE:
		HexGetMouseAddress(lParam); // Update mouse area
		if (MouseButtonHeld)
			HexSelectAddress(HexGetMouseAddress(lParam), 0);
		HexUpdateScrollInfo();
		SetScrollInfo(hDlg, SB_VERT, &HexSI, TRUE);
		HexUpdateCaption();
		return 0;
		break;

	case WM_LBUTTONUP:
		if (SecondDigitPrompted) return 0;
		HexSelectAddress(HexGetMouseAddress(lParam), 0);
		MouseButtonHeld = 0;
		HexUpdateCaption();
		ReleaseCapture(); // Stop wathcing mouse
		return 0;
		break;

	case WM_VSCROLL:
		Clear_Sound_Buffer();
		HexUpdateScrollInfo();
		GetScrollInfo(hDlg, SB_VERT, &HexSI);
		switch (LOWORD(wParam)) {
		case SB_ENDSCROLL:
		case SB_TOP:
		case SB_BOTTOM:
			break;
		case SB_LINEUP:
			HexSI.nPos--;
			break;
		case SB_LINEDOWN:
			HexSI.nPos++;
			break;
		case SB_PAGEUP:
			HexSI.nPos -= HexSI.nPage;
			break;
		case SB_PAGEDOWN:
			HexSI.nPos += HexSI.nPage;
			break;
		case SB_THUMBPOSITION:
		case SB_THUMBTRACK:
			HexSI.nPos = HexSI.nTrackPos;
			break;
		}
		if (HexSI.nPos < HexSI.nMin) HexSI.nPos = HexSI.nMin;
		if ((HexSI.nPos + (int)HexSI.nPage) > HexSI.nMax) HexSI.nPos = HexSI.nMax - HexSI.nPage;			
		Hex.OffsetVisibleFirst = HexSI.nPos * RowCount;
		SetScrollInfo(hDlg, SB_VERT, &HexSI, TRUE);
		HexUpdateDialog(1);
		return 0;
		break;

	case WM_MOUSEWHEEL: {
		int WheelDelta = (short)HIWORD(wParam);
		HexUpdateScrollInfo();
		GetScrollInfo(hDlg, SB_VERT, &HexSI);
		if (WheelDelta < 0) HexSI.nPos += HexSI.nPage;
		if (WheelDelta > 0) HexSI.nPos -= HexSI.nPage;
		if (HexSI.nPos < HexSI.nMin) HexSI.nPos = HexSI.nMin;
		if ((HexSI.nPos + (int)HexSI.nPage) > HexSI.nMax) HexSI.nPos = HexSI.nMax - HexSI.nPage;
		Hex.OffsetVisibleFirst = HexSI.nPos * RowCount;
		SetScrollInfo(hDlg, SB_VERT, &HexSI, TRUE);
		HexUpdateDialog(1);
		return 0;
		break;
	}

	case WM_SIZING: {
		Clear_Sound_Buffer();
		RECT *r = (RECT *) lParam;
		HexUpdateScrollInfo();
		GetScrollInfo(hDlg, SB_VERT, &HexSI);
		if ((wParam == WMSZ_BOTTOM) || (wParam == WMSZ_BOTTOMRIGHT) || (wParam == WMSZ_RIGHT)) {
			// Gradual resizing
			UINT height = r->bottom - r->top;
			UINT width  = r->right  - r->left;
			UINT split  = Hex.FontWidth;
			// Manual adjust to account for cell parameters
			r->bottom = r->top + height - ((height - ClientTopGap) % Hex.CellHeight);
			HexSI.nPage = (height - ClientTopGap) / Hex.CellHeight - 1;
			if ((HexSI.nPos + (int) HexSI.nPage) > HexSI.nMax)
				HexSI.nPos = HexSI.nMax - HexSI.nPage;
			Hex.OffsetVisibleFirst = HexCap(HexSI.nPos * RowCount, 0, 0);
			Hex.OffsetVisibleTotal = HexCap(HexSI.nPage,
				Hex.CurrentRegion.Size/RowCount + (OFFSET_REMINDER > 0), 1);
			SetScrollInfo(hDlg, SB_VERT, &HexSI, TRUE);
			if ((width > TextArea.left + ClientXGap + split) && (!Hex.TextView))
				r->right = r->left + TextArea.right + ClientXGap;
			else if ((width < TextArea.right + ClientXGap - split) && (Hex.TextView))
				r->right = r->left + TextArea.left + ClientXGap;
		}
		HexUpdateDialog();
		return 0;
		break;
	}

	case WM_EXITSIZEMOVE: {
		RECT r;
		GetWindowRect(hDlg, &r);
		if (r.right - r.left == TextArea.left  + ClientXGap)
			Hex.TextView = 0;
		if (r.right - r.left == TextArea.right + ClientXGap)
			Hex.TextView = 1;
		HexUpdateDialog(1);
		break;
	}

	case WM_NCHITTEST: {
		LRESULT lRes = DefWindowProc(hDlg, uMsg, wParam, lParam);
		if (lRes == HTBOTTOMLEFT || lRes == HTTOPLEFT || lRes == HTTOPRIGHT ||
			lRes == HTTOP        || lRes == HTLEFT    || lRes == HTSIZE     )
			lRes = HTBORDER;
		return lRes;
		break;
	}

	case WM_GETMINMAXINFO: {
		MINMAXINFO *pInfo = (MINMAXINFO *) lParam;
		// Manual adjust to account for cell parameters
		pInfo->ptMinTrackSize.y = Hex.CellHeight * 2 + ClientTopGap;
		if (HexStarted) {
			pInfo->ptMinTrackSize.x = TextArea.left + ClientXGap;
			pInfo->ptMaxTrackSize.x = TextArea.right + ClientXGap;
		}
		return 0;
		break;
	}

	case WM_CLOSE:
		if (Full_Screen) {
			while (ShowCursor(true) < 0);
			while (ShowCursor(false) >= 0); }
		HexDestroyDialog();
		return 0;
		break;
	}
	return DefWindowProc(hDlg, uMsg, wParam, lParam);
}

void HexCreateDialog() {
	WNDCLASSEX wndclass;
	if (!HexEditorHWnd) {
		memset(&wndclass, 0, sizeof(wndclass));
		wndclass.cbSize        = sizeof(WNDCLASSEX);
		wndclass.style         = CS_HREDRAW | CS_VREDRAW;
		wndclass.lpfnWndProc   = HexEditorProc;
		wndclass.cbClsExtra    = 0;
		wndclass.cbWndExtra    = 0;
		wndclass.hInstance     = ghInstance;
		wndclass.hIcon         = LoadIcon(ghInstance, MAKEINTRESOURCE(IDI_GENS));
		wndclass.hIconSm       = LoadIcon(ghInstance, MAKEINTRESOURCE(IDI_GENS));
		wndclass.hCursor       = LoadCursor (NULL, IDC_ARROW);
		wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
		wndclass.lpszMenuName  = "HEXEDITOR_MENU";
		wndclass.lpszClassName = "HEXEDITOR";
		if(!RegisterClassEx(&wndclass)) {
			Put_Info("Error Registering HEXEDITOR Window Class.");
			return;
		}
		HexFont = CreateFont(
			Hex.FontHeight, Hex.FontWidth,	// height, width
			0, 0, Hex.FontWeight,			// escapement, orientation, weight
			FALSE, FALSE, FALSE,			// italic, underline, strikeout
			ANSI_CHARSET, OUT_DEVICE_PRECIS,// charset, precision
			CLIP_MASK, DEFAULT_QUALITY,		// clipping, quality
			DEFAULT_PITCH, "Courier New"); 	// pitch, name
		HexEditorHWnd = CreateWindowEx(0, "HEXEDITOR", "Hex Editor",
			WS_SYSMENU | WS_SIZEBOX | WS_MINIMIZEBOX | WS_VSCROLL,
			0, 0, 100, 100, NULL, NULL, ghInstance, NULL);
		ShowWindow(HexEditorHWnd, SW_SHOW);
		HexLoadSymbols();
		HexUpdateCaption();
		DialogsOpen++;
	} else {
		ShowWindow(HexEditorHWnd, SW_SHOWNORMAL);
		SetForegroundWindow(HexEditorHWnd);
		HexUpdateCaption();
	}
}
