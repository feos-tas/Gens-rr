#ifndef HEXEDITOR_H
#define HEXEDITOR_H

extern HWND HexEditorHWnd;
extern void DoHexEditor();
extern void UpdateHexEditor();

typedef struct {
	bool FontBold;
	unsigned int
		FontHeight, FontWidth, FontWeight,
		GapFontH, GapFontV, GapHeaderH, GapHeaderV,
		CellHeight, CellWidth,
		OffsetVisibleFirst, OffsetVisibleLast, OffsetVisibleTotal,
		DialogPosX, DialogPosY,
		AddressSelectedFirst, AddressSelectedLast, AddressSelectedTotal,
		MemoryRegion;
	COLORREF
		ColorFont, ColorBG, ColorSelection;
} HexParameters;

extern HexParameters Hex;

#endif