#include "resource.h"
#include "gens.h"
#include "mem_m68k.h"
#include "mem_s68k.h"
#include "mem_sh2.h"
#include "G_main.h"
#include "G_ddraw.h"
#include "G_dsound.h"
#include "ram_search.h"
#include "hexeditor.h"
#include <windowsx.h>

HWND HexEditorHWnd;
HDC HexDC;
SCROLLINFO HexSI;
MousePos ClickArea = NO;

bool
	MouseButtonHeld = 0,
	SwapBytes = 0,
	HexStarted = 0,
	TextAreaVisible = 1;

unsigned int
	ClientTopGap = 0,	// How much client area is shifted
	ClientXGap = 0,
	RowCount = 16;		// Offset consists of 16 bytes

HexParameters Hex =
{
	0, 15, Hex.FontHeight/2, Hex.FontBold? 600:400,	// font				// bold, height, width, weight
	8, 0,											// font gap			// left, top
	Hex.FontWidth * 6,								// header gap		// X
	Hex.FontHeight + Hex.GapFontY,					// header gap		// Y
	Hex.FontHeight  + Hex.GapFontY,					// cell				// height
	Hex.FontWidth*2 + Hex.GapFontX,					// cell				// width
	0, 0,											// dialog pos		// X, Y
	0, 0,											// dialog size		// X, Y
	0, 16,											// visible offset	// first, total
	0, 0, 0,										// selected address // first, total, last
	0xff0000,										// memory region	// m68k ram
	0x00000000, 0x00ffffff,							// colors			// font, BG
};

RECT
	CellArea = {
		Hex.GapHeaderX + (Hex.FontWidth * 2),
		Hex.CellHeight,
		(Hex.CellWidth * RowCount) + (Hex.FontWidth * 3),
		Hex.CellHeight * (Hex.OffsetVisibleTotal + 1)
	},
	TextArea = {
		(Hex.CellWidth * RowCount) + Hex.FontWidth,
		Hex.CellHeight,
		(Hex.CellWidth * RowCount) + (Hex.FontWidth * 3) + (Hex.FontWidth * RowCount),
		Hex.CellHeight * (Hex.OffsetVisibleTotal + 1)
	};

#define CLIENT_WIDTH	(CellArea.left + CellArea.right + TextAreaVisible * (TextArea.right - TextArea.left))
#define CLIENT_HEIGHT	(Hex.CellHeight*(Hex.OffsetVisibleTotal + 1) + 1)
#define LAST_OFFSET		(Hex.OffsetVisibleFirst + Hex.OffsetVisibleTotal)
#define LAST_ADDRESS	(Hex.OffsetVisibleFirst + Hex.OffsetVisibleTotal*RowCount - 1)
#define SELECTION_START	min(Hex.AddressSelectedFirst, Hex.AddressSelectedLast)
#define SELECTION_END	max(Hex.AddressSelectedFirst, Hex.AddressSelectedLast)

HFONT HexFont = CreateFont(
	Hex.FontHeight,		// height
	Hex.FontWidth,		// width
	0, 0,				// escapement, orientation
	Hex.FontWeight,		// weight
	FALSE, FALSE, FALSE,// italic, underline, strikeout
	ANSI_CHARSET,		// charset
	OUT_DEVICE_PRECIS,	// precision
	CLIP_MASK,			// clipping
	DEFAULT_QUALITY,	// quality
	DEFAULT_PITCH,		// pitch
	"Courier New"		// name
);

void HexSetColors(HDC hDC, bool Selection)
{
	if (Selection)
	{
		SetBkColor(hDC, Hex.ColorFont);
		SetTextColor(hDC, Hex.ColorBG);		
	}
	else
	{
		SetBkColor(hDC, Hex.ColorBG);
		SetTextColor(hDC, Hex.ColorFont);
	}
}

void HexUpdateDialog()
{
	InvalidateRect(HexEditorHWnd, NULL, FALSE);
}

void HexUpdateCaption()
{
	char str[100];

	if (Hex.AddressSelectedTotal == 0)
		sprintf(
			str,
			"Hex Editor: RAM M68K"
		);
	else if (Hex.AddressSelectedTotal == 1)
		sprintf(
			str,
			"RAM M68K: $%06X",
			Hex.AddressSelectedFirst + Hex.MemoryRegion
		);
	else if (Hex.AddressSelectedTotal > 1)
		sprintf(
			str,
			"RAM M68K: $%06X - $%06X (%d)",
			Hex.AddressSelectedFirst + Hex.MemoryRegion,
			Hex.AddressSelectedLast + Hex.MemoryRegion,
			Hex.AddressSelectedTotal
		);

	SetWindowText(HexEditorHWnd, str);
	return;
}

void HexUpdateScrollInfo()
{
	ZeroMemory(&HexSI, sizeof(SCROLLINFO));
	HexSI.cbSize = sizeof(HexSI);
	HexSI.fMask  = SIF_ALL;
	HexSI.nMin   = 0;
	HexSI.nMax   = _68K_RAM_SIZE / RowCount;
	HexSI.nPage  = Hex.OffsetVisibleTotal;
	HexSI.nPos   = Hex.OffsetVisibleFirst / RowCount;
}

int HexGetClickAddress(int x, int y)
{
	x = x - Hex.GapHeaderX;
	y = y - Hex.GapHeaderY;

	int Address = y / Hex.CellHeight * RowCount + x / Hex.CellWidth + Hex.OffsetVisibleFirst;
	
	return Address;
}

void HexSelectAddress(int Address, bool ButtonDown)
{
/*	switch (ClickArea)
	{
		case CELL:
		{
			if (ButtonDown)
			{
				Hex.AddressSelectedFirst = Address;
				Hex.AddressSelectedLast  = Address;
				Hex.AddressSelectedTotal = 1;
			}
			else
			{
				Hex.AddressSelectedLast  = Address;
				Hex.AddressSelectedTotal = SELECTION_END - SELECTION_START + 1;
			}
		}
		break;
		
		case LEFTHEADER:
		{
			if (ButtonDown)
			{
				Hex.AddressSelectedFirst = Address;
				Hex.AddressSelectedLast  = Address + RowCount - 1;
				Hex.AddressSelectedTotal = RowCount;
			}
			else
			{
				Hex.AddressSelectedLast  = Address + RowCount - 1;
				Hex.AddressSelectedTotal = SELECTION_END - SELECTION_START + 1;
			}
		}
		break;

		case CORNER:
		{
			Hex.AddressSelectedFirst = Hex.OffsetVisibleFirst;
			Hex.AddressSelectedLast  = LAST_ADDRESS;
			Hex.AddressSelectedTotal = Hex.OffsetVisibleTotal * RowCount;
		}
		break;

		case NO:
		break;
	}
	if (Hex.AddressSelectedLast > _68K_RAM_SIZE - 1)
	{
		Hex.AddressSelectedLast = _68K_RAM_SIZE - 1;
		Hex.AddressSelectedTotal = SELECTION_END - SELECTION_START + 1;
	}
//	if (SELECTION_START < Hex.OffsetVisibleFirst)
//		Hex.OffsetVisibleFirst = SELECTION_START;
	if (SELECTION_END > LAST_ADDRESS)
		Hex.OffsetVisibleFirst = (SELECTION_END / RowCount - Hex.OffsetVisibleTotal + 1) * RowCount;
	HexUpdateScrollInfo();
	HexUpdateDialog();*/
}

void HexDestroySelection()
{
	ClickArea = NO;
	Hex.AddressSelectedFirst = 0;
	Hex.AddressSelectedTotal = 0;
	Hex.AddressSelectedLast = 0;
}

void HexDestroyDialog()
{
	DialogsOpen--;
	HexDestroySelection();
	ReleaseDC(HexEditorHWnd, HexDC);
	DestroyWindow(HexEditorHWnd);
	UnregisterClass("HEXEDITOR", ghInstance);
	HexEditorHWnd = 0;
	return;
}

LRESULT CALLBACK HexEditorProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	RECT r, wr, cr;
	PAINTSTRUCT ps;

	switch (uMsg)
	{
		case WM_CREATE:
		{
			HexDC = GetDC(hDlg);
			SelectObject(HexDC, HexFont);
			SetTextAlign(HexDC, TA_UPDATECP | TA_TOP | TA_LEFT);
			if (Full_Screen)
			{
				while (ShowCursor(false) >= 0);
				while (ShowCursor(true) < 0);
			}			
			SetRect(
				&r, 0, 0,
				CLIENT_WIDTH,
				CLIENT_HEIGHT
			);
			// Automatic adjust to account for menu, scrollbar and OS style
			AdjustWindowRectEx(
				&r,
				GetWindowLong(hDlg, GWL_STYLE),
				(GetMenu(hDlg) > 0),
				GetWindowLong(hDlg, GWL_EXSTYLE)
			);
			SetWindowPos(
				hDlg, NULL,
				Hex.DialogPosX,
				Hex.DialogPosY,
				r.right - r.left,
				r.bottom - r.top,
				SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_SHOWWINDOW
			);
			GetClientRect(hDlg, &cr);
			ClientTopGap = r.bottom - r.top - cr.bottom + 1;
			ClientXGap = r.right - r.left - CLIENT_WIDTH;
			Hex.AddressSelectedTotal = 0;
			HexUpdateScrollInfo();
			SetScrollInfo(hDlg, SB_VERT, &HexSI, TRUE);
			HexStarted = 1;
			return 0;
		}
		break;

		case WM_PAINT:
		{
			static char buf[10];
			unsigned int row = 0, line = 0;
			GetWindowRect(hDlg, &wr);
			BeginPaint(hDlg, &ps);
			// TOP HEADER, static.
			for (row = 0; row < RowCount; row++)
			{
				MoveToEx(
					HexDC,
					row * Hex.CellWidth + CellArea.left,
					0,
					NULL
				);
				HexSetColors(HexDC, 0);
				sprintf(buf, "%2X", row);
				TextOut(HexDC, 0, 0, buf, strlen(buf));
			}
			// LEFT HEADER, semi-dynamic.
			for (line = 0; line < Hex.OffsetVisibleTotal; line++)
			{
				MoveToEx(
					HexDC,
					0,
					line * Hex.CellHeight + Hex.GapHeaderY,
					NULL
				);
				HexSetColors(HexDC, 0);
				sprintf(buf, "%06X", Hex.OffsetVisibleFirst + line * RowCount + Hex.MemoryRegion);
				TextOut(HexDC, 0, 0, buf, strlen(buf));
			}
			// RAM, dynamic.
			for (line = 0; line < Hex.OffsetVisibleTotal; line++)
			{
				for (row = 0; row < RowCount; row++)
				{
					unsigned int carriage = Hex.OffsetVisibleFirst + line * RowCount + row;
					int swap = 0;
					if (SwapBytes)
					{
						if ((row % 2) > 0)
							swap = -1;
						else
							swap = 1;
					}
					// Print numbers in main area
					MoveToEx(
						HexDC,
						row * Hex.CellWidth + CellArea.left,
						line * Hex.CellHeight + Hex.GapHeaderY,
						NULL
					);
					if ((Hex.AddressSelectedTotal) && (carriage >= SELECTION_START) && (carriage <= SELECTION_END))
						HexSetColors(HexDC, 1);
					else
						HexSetColors(HexDC, 0);
					sprintf(buf, "%02X", Ram_68k[carriage + swap]);
					TextOut(HexDC, 0, 0, buf, strlen(buf));
					// Print chars on the right
					MoveToEx(
						HexDC,
						row * Hex.FontWidth + TextArea.left + CellArea.left,
						line * Hex.CellHeight + Hex.GapHeaderY,
						NULL
					);
					if ((Hex.AddressSelectedTotal) && (carriage >= SELECTION_START) && (carriage <= SELECTION_END))
						HexSetColors(HexDC, 1);
					else
						HexSetColors(HexDC, 0);
					UINT8 check = Ram_68k[carriage + swap];
					if((check >= 32) && (check <= 127))
						buf[0] = (char) check;
					else
						buf[0] = '.';
					TextOut(HexDC, 0, 0, buf, 1);
				}
			}
			EndPaint(hDlg, &ps);
			return 0;
		}
		break;

		case WM_COMMAND:
		{
/*			switch(wParam)
			{
				case IDC_SAVE:
				{
					char fname[2048];
					strcpy(fname,"dump.bin");
					if(Change_File_S(fname,".","Save Full Dump As...","All Files\0*.*\0\0","*.*",hDlg))
					{
						FILE *out=fopen(fname,"wb+");
						int i;
						for (i=0;i<sizeof(Ram_68k);++i)
						{
							fname[i&2047]=Ram_68k[i^1];
							if ((i&2047)==2047)
								fwrite(fname,1,sizeof(fname),out);
						}
						fwrite(fname,1,i&2047,out);
						fclose(out);
					}
				}
				break;

				case IDC_BUTTON1:
				{
					char fname[2048];
					GetDlgItemText(hDlg,IDC_EDIT1,fname,2047);
					int CurPos;
					if ((strnicmp(fname,"ff",2)==0) && sscanf(fname+2,"%x",&CurPos))
					{
						SetScrollPos(GetDlgItem(hDlg,IDC_SCROLLBAR1),SB_CTL,(CurPos>>4),TRUE);
						Update_RAM_Dump();
					}
				}
				break;
			}
*/		
		}
		break;

		case WM_LBUTTONDOWN:
		{
			SetCapture(hDlg); // watch mouse actions outside the client area
			int MouseX = GET_X_LPARAM(lParam);
			int MouseY = GET_Y_LPARAM(lParam);
			HexSelectAddress(HexGetClickAddress(MouseX, MouseY), 1);
			MouseButtonHeld = 1;
			HexUpdateCaption();
			return 0;
		}
		break;

		case WM_MOUSEMOVE:
		{
			int MouseX = GET_X_LPARAM(lParam);
			int MouseY = GET_Y_LPARAM(lParam);
			if (MouseButtonHeld)
				HexSelectAddress(HexGetClickAddress(MouseX, MouseY), 0);
			HexUpdateCaption();
			return 0;
		}
		break;

		case WM_LBUTTONUP:
		{
			int MouseX = GET_X_LPARAM(lParam);
			int MouseY = GET_Y_LPARAM(lParam);
			HexSelectAddress(HexGetClickAddress(MouseX, MouseY), 0);
			MouseButtonHeld = 0;
			HexUpdateCaption();
			ReleaseCapture(); // stop wathcing mouse
			return 0;
		}
		break;

		case WM_VSCROLL:
		{
			Clear_Sound_Buffer();
			HexUpdateScrollInfo();
			GetScrollInfo(hDlg, SB_VERT, &HexSI);
			switch (LOWORD(wParam))
			{
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
			if (HexSI.nPos < HexSI.nMin)
				HexSI.nPos = HexSI.nMin;
			if ((HexSI.nPos + (int) HexSI.nPage) > HexSI.nMax)
				HexSI.nPos = HexSI.nMax - HexSI.nPage;			
			Hex.OffsetVisibleFirst = HexSI.nPos * RowCount;
			SetScrollInfo(hDlg, SB_VERT, &HexSI, TRUE);
			HexUpdateDialog();
			return 0;
		}
		break;

		case WM_MOUSEWHEEL:
		{
			int WheelDelta = (short) HIWORD(wParam);
			HexUpdateScrollInfo();
			GetScrollInfo(hDlg, SB_VERT, &HexSI);
			if (WheelDelta < 0)
				HexSI.nPos += HexSI.nPage;
			if (WheelDelta > 0)
				HexSI.nPos -= HexSI.nPage;
			if (HexSI.nPos < HexSI.nMin)
				HexSI.nPos = HexSI.nMin;
			if ((HexSI.nPos + (int) HexSI.nPage) > HexSI.nMax)
				HexSI.nPos = HexSI.nMax - HexSI.nPage;
			Hex.OffsetVisibleFirst = HexSI.nPos * RowCount;
			SetScrollInfo(hDlg, SB_VERT, &HexSI, TRUE);
			HexUpdateDialog();
			return 0;
		}
		break;

		case WM_SIZING:
		{
			Clear_Sound_Buffer();
			RECT *r = (RECT *) lParam;			
			HexUpdateScrollInfo();
			GetScrollInfo(hDlg, SB_VERT, &HexSI);
			if ((wParam == WMSZ_BOTTOM) || (wParam == WMSZ_BOTTOMRIGHT))
			{
				int height = r->bottom - r->top;
				// Manual adjust to account for cell parameters
				r->bottom = r->top + height - ((height - ClientTopGap) % Hex.CellHeight);
				HexSI.nPage = (height - ClientTopGap) / Hex.CellHeight - 1;
				if ((HexSI.nPos + (int) HexSI.nPage) > HexSI.nMax)
					HexSI.nPos = HexSI.nMax - HexSI.nPage;

				Hex.OffsetVisibleFirst = HexSI.nPos * RowCount;
				Hex.OffsetVisibleTotal = HexSI.nPage;
				SetScrollInfo(hDlg, SB_VERT, &HexSI, TRUE);
			}
			HexUpdateDialog();
			return 0;
		}
		break;

		case WM_NCHITTEST:
		{
			LRESULT lRes = DefWindowProc(hDlg, uMsg, wParam, lParam);
			if (lRes == HTBOTTOMLEFT || lRes == HTTOPLEFT || lRes == HTTOPRIGHT ||
				lRes == HTTOP        || lRes == HTLEFT    || lRes == HTSIZE     )
				lRes = HTBORDER;
			return lRes;
		}
		break;

		case WM_GETMINMAXINFO:
		{
			MINMAXINFO *pInfo = (MINMAXINFO *) lParam;
			// Manual adjust to account for cell parameters
			pInfo->ptMinTrackSize.y = Hex.CellHeight * 2 + ClientTopGap;
			if (HexStarted)
			{
				pInfo->ptMinTrackSize.x = CellArea.left + CellArea.right + ClientXGap;
				pInfo->ptMaxTrackSize.x = CellArea.left + CellArea.right + (TextArea.right - TextArea.left) + ClientXGap;
			}
			return 0;
		}

		case WM_CLOSE:
		{
			if (Full_Screen)
			{
				while (ShowCursor(true) < 0);
				while (ShowCursor(false) >= 0);
			}
			GetWindowRect(hDlg, &wr);
			Hex.DialogPosX = wr.left;
			Hex.DialogPosY = wr.top;
			HexDestroyDialog();
			return 0;
		}
		break;
	}
	return DefWindowProc(hDlg, uMsg, wParam, lParam);
}

void HexCreateDialog()
{
	WNDCLASSEX wndclass;

	if (!HexEditorHWnd)
	{
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
		wndclass.lpszClassName = "HEXEDITOR";

		if(!RegisterClassEx(&wndclass))
		{
			Put_Info("Error Registering HEXEDITOR Window Class.");
			return;
		}

		HexEditorHWnd = CreateWindowEx(
			0,
			"HEXEDITOR",
			"HexEditor",
			WS_SYSMENU | WS_SIZEBOX | WS_MINIMIZEBOX | WS_VSCROLL,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			NULL,
			NULL,
			ghInstance,
			NULL
		);

		ShowWindow(HexEditorHWnd, SW_SHOW);
		HexUpdateCaption();
		DialogsOpen++;
	}
	else
	{
		ShowWindow(HexEditorHWnd, SW_SHOWNORMAL);
		SetForegroundWindow(HexEditorHWnd);
		HexUpdateCaption();
	}
}