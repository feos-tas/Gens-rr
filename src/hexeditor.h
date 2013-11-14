#ifndef HEXEDITOR_H
#define HEXEDITOR_H

typedef struct {
	bool FontBold;
	unsigned int
		FontHeight, FontWidth, FontWeight,
		GapFontX, GapFontY, GapHeaderX, GapHeaderY,
		CellHeight, CellWidth,
		DialogPosX, DialogPosY,
		OffsetVisibleFirst, OffsetVisibleTotal,
		AddressSelectedFirst, AddressSelectedTotal, AddressSelectedLast, SelectionStep,
		MemoryRegion;
	COLORREF
		ColorFont, ColorBG, ColorSelection;
} HexParameters;

enum MousePos {
	NOWHERE,
	LEFTHEADER,
	TOPHEADER,
	CORNER,
	CELLS
};

extern HWND HexEditorHWnd;
extern HexParameters Hex;
extern void HexCreateDialog();
extern void HexUpdateDialog();

#endif