#include "resource.h"
#include "gens.h"
#include "mem_m68k.h"
#include "mem_s68k.h"
#include "mem_sh2.h"
#include "G_main.h"
#include "G_ddraw.h"
#include "ram_search.h"
#include "hexeditor.h"
#include <windowsx.h>

HWND HexEditorHWnd;
HDC HexDC;
SCROLLINFO HexSI;

int
	MouseX,
	MouseY;

unsigned int
	ClientTopGap = 0,
	RowCount = 16; // Offset consists of 16 bytes

void
	HexDestroyDialog(),
	HexUpdateCaption(),
	HexUpdateScrollInfo(),
	HexSelectAddress(int x, int y);

HexParameters Hex =
{
	0, 15, Hex.FontHeight/2, Hex.FontBold? 600:400,	// font				// bold, height, width, weight
	8, 0,											// header gap		// left, top
	Hex.FontWidth*8, Hex.FontHeight + Hex.GapFontY,	// font gap			// X, Y
	Hex.FontHeight  + Hex.GapFontY,					// cell				// height
	Hex.FontWidth*2 + Hex.GapFontX,					// cell				// width
	0, 0,											// dialog pos		// X, Y
	0, 16,											// visible offset	// first, total
	0, 0, 0,										// selected address // first, total, last
	0xff0000,										// memory region	//
	0x00000000, 0x00ffffff,							// colors			// font, BG
};

HFONT HexFont = CreateFont(
	Hex.FontHeight,		// height
	Hex.FontWidth,		// width
	0,					// escapement
	0,					// orientation
	Hex.FontWeight,		// weight
	FALSE,				// italic
	FALSE,				// underline
	FALSE,				// strikeout
	ANSI_CHARSET,		// charset
	OUT_DEVICE_PRECIS,	// precision
	CLIP_MASK,			// clipping
	DEFAULT_QUALITY,	// quality
	DEFAULT_PITCH,		// pitch
	"Courier New"		// name
);

LRESULT CALLBACK HexEditorProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	RECT r, wr, cr;
	PAINTSTRUCT ps;

	switch(uMsg)
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
				(Hex.CellWidth + 1) * RowCount + Hex.GapHeaderX,
				Hex.CellHeight * (Hex.OffsetVisibleTotal + 1) + 1
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
			Hex.AddressSelectedTotal = 0;

			HexUpdateScrollInfo();
			SetScrollInfo(hDlg, SB_VERT, &HexSI, TRUE);
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
			MouseX = GET_X_LPARAM(lParam);
			MouseY = GET_Y_LPARAM(lParam);
			HexSelectAddress(MouseX, MouseY);
			HexUpdateCaption();
			return 0;
		}
		break;

		case WM_VSCROLL:
		{
			HexUpdateScrollInfo();
			GetScrollInfo(hDlg, SB_VERT, &HexSI);

			switch(LOWORD(wParam))
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
			HexUpdateScrollInfo();
			GetScrollInfo(hDlg, SB_VERT, &HexSI);

			RECT *r = (RECT *) lParam;

			if (wParam == WMSZ_BOTTOM) // just in case...
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
			if (
				lRes == HTBOTTOMLEFT || lRes == HTBOTTOMRIGHT ||
				lRes == HTTOPLEFT || lRes == HTTOPRIGHT || lRes == HTTOP ||
				lRes == HTLEFT || lRes == HTRIGHT || lRes == HTSIZE
			)
				lRes = HTBORDER; // block resizing for all but HTBOTTOM
			return lRes;
		}
		break;

		case WM_GETMINMAXINFO:
		{
			MINMAXINFO *pInfo = (MINMAXINFO *)lParam;
			// Manual adjust to account for cell parameters
			pInfo->ptMinTrackSize.y = Hex.CellHeight * 2 + ClientTopGap;
			return 0;
		}

		case WM_PAINT:
		{
			GetWindowRect(hDlg, &wr);
			BeginPaint(hDlg, &ps);
			static char buf[10];
			unsigned int row = 0, line = 0;

			// TOP HEADER, static.
			for (row = 0; row < RowCount; row++)
			{
				MoveToEx(HexDC, row * Hex.CellWidth + Hex.GapHeaderX, 0, NULL);
				SetBkColor(HexDC, Hex.ColorBG);
				SetTextColor(HexDC, Hex.ColorFont);
				sprintf(buf, "%2X", row);
				TextOut(HexDC, 0, 0, buf, strlen(buf));
			}

			// LEFT HEADER, semi-dynamic.
			for (line = 0; line < Hex.OffsetVisibleTotal; line++)
			{
				MoveToEx(HexDC, 0, line * Hex.CellHeight + Hex.GapHeaderY, NULL);
				SetBkColor(HexDC, Hex.ColorBG);
				SetTextColor(HexDC, Hex.ColorFont);
				sprintf(buf, "%06X:", Hex.OffsetVisibleFirst + line * RowCount + Hex.MemoryRegion);
				TextOut(HexDC, 0, 0, buf, strlen(buf));
			}

			// RAM, dynamic.
			for (line = 0; line < Hex.OffsetVisibleTotal; line++)
			{
				for (row = 0; row < RowCount; row++)
				{
					MoveToEx(HexDC, row * Hex.CellWidth + Hex.GapHeaderX, line * Hex.CellHeight + Hex.GapHeaderY, NULL);
					if (
						(Hex.AddressSelectedTotal > 0) &&
						((Hex.OffsetVisibleFirst + line * RowCount + row) >= Hex.AddressSelectedFirst) &&
						((Hex.OffsetVisibleFirst + line * RowCount + row) <= Hex.AddressSelectedLast)
					)
					{	
						SetBkColor(HexDC, Hex.ColorFont);
						SetTextColor(HexDC, Hex.ColorBG);
					}
					else
					{
						SetBkColor(HexDC, Hex.ColorBG);
						SetTextColor(HexDC, Hex.ColorFont);
					}					
					sprintf(buf, "%02X", (int) Ram_68k[Hex.OffsetVisibleFirst + line * RowCount + row]);
					TextOut(HexDC, 0, 0, buf, strlen(buf));
				}
			}
			EndPaint(hDlg, &ps);
			return 0;
		}
		break;

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

void HexSelectAddress(int x, int y)
{
	x = x - Hex.GapHeaderX;
	y = y - Hex.GapHeaderY;
	
	if (x < 0 && y >= 0)
	{
		Hex.AddressSelectedFirst = Hex.OffsetVisibleFirst + y / Hex.CellHeight * RowCount;
		Hex.AddressSelectedLast  = Hex.AddressSelectedFirst + RowCount - 1;
		Hex.AddressSelectedTotal = RowCount;
	}
	else if (y < 0 && x >=0)
	{
		Hex.AddressSelectedFirst = x / Hex.CellWidth + Hex.OffsetVisibleFirst;
		Hex.AddressSelectedLast  = (Hex.OffsetVisibleTotal - 1) * RowCount + Hex.AddressSelectedFirst;
		Hex.AddressSelectedTotal = Hex.OffsetVisibleTotal;
	}
	else if (y < 0 && x < 0)
	{
		Hex.AddressSelectedFirst = Hex.OffsetVisibleFirst;
		Hex.AddressSelectedLast  = Hex.OffsetVisibleFirst + Hex.OffsetVisibleTotal * RowCount - 1;
		Hex.AddressSelectedTotal = Hex.OffsetVisibleTotal * RowCount;
	}
	else
	{
		Hex.AddressSelectedFirst = y/Hex.CellHeight*RowCount + x/Hex.CellWidth + Hex.OffsetVisibleFirst;
		Hex.AddressSelectedLast  = Hex.AddressSelectedFirst;
		Hex.AddressSelectedTotal = 1;
	}
	HexUpdateDialog();
}

void HexDestroyDialog()
{
	DialogsOpen--;	
	ReleaseDC(HexEditorHWnd, HexDC);
	DestroyWindow(HexEditorHWnd);
	UnregisterClass("HEXEDITOR", ghInstance);
	HexEditorHWnd = 0;
	return;
}