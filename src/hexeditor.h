#ifndef HEXEDITOR_H
#define HEXEDITOR_H

#define u8 UINT8

struct HexRegion {
	char Name[12];
	u8*  Array;
	UINT Offset;
	UINT Size;
	bool Active;
	u8   Swap;
};

struct HexParameters {
	bool TextView, DrawLines, FontBold;
	UINT
		FontHeight, FontWidth, FontWeight,
		Gap, GapHeaderX, GapHeaderY,
		CellHeight, CellWidth,
		DialogPosX, DialogPosY,
		OffsetVisibleFirst, OffsetVisibleTotal,
		AddressSelectedFirst, AddressSelectedTotal, AddressSelectedLast;
	COLORREF
		ColorFont, ColorBG;
	HexRegion CurrentRegion;
};

struct SymbolName {
	u8*  Array;
	UINT Start;
	UINT Size;
	char*Name;
};

struct Patch {
	u8*  Array;
	UINT Start;
	UINT Size;
	UINT Value;
};

enum MousePos {
	NO,
	CELL,
	TEXT
};

void HexCreateDialog();
void HexDestroyDialog();
void HexUpdateDialog(bool ClearBG = 0);
extern HWND HexEditorHWnd;
extern HexParameters Hex;

#endif