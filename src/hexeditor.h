#ifndef HEXEDITOR_H
#define HEXEDITOR_H

typedef struct {
	bool TextView, FontBold;
	unsigned int
		FontHeight, FontWidth, FontWeight,
		GapFontX, GapFontY, GapHeaderX, GapHeaderY,
		CellHeight, CellWidth,
		DialogPosX, DialogPosY,
		OffsetVisibleFirst, OffsetVisibleTotal,
		AddressSelectedFirst, AddressSelectedTotal, AddressSelectedLast,
		MemoryRegion;
	COLORREF
		ColorFont, ColorBG, ColorSelection;
} HexParameters;

enum MousePos {
	NO,
	CELL,
	TEXT };

extern HWND HexEditorHWnd;
extern HexParameters Hex;
extern void HexCreateDialog();
extern void HexUpdateDialog();

#endif