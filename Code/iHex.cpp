/*hdr
**	FILE:			iHex.cpp
**	AUTHOR:			Matthew Allen
**	DATE:			7/5/2002
**	DESCRIPTION:	Hex viewer/editor
**
**	Copyright (C) 2002, Matthew Allen
**		fret@memecode.com
*/

#define _WIN32_WINNT 0x0400
#include "iHex.h"
#include "GToken.h"
#include "GAbout.h"
#include "resdefs.h"
#include "GCombo.h"
#include "GScrollBar.h"
#include "GProgressDlg.h"
#include "GXmlTreeUi.h"
#include "GTableLayout.h"
#ifdef WIN32
#include "wincrypt.h"
#endif

///////////////////////////////////////////////////////////////////////////////////////////////
// Application identification
char *AppName = "i.Hex";
bool CancelSearch = false;

#define ColourSelectionFore			Rgb24(255, 255, 0)
#define ColourSelectionBack			Rgb24(0, 0, 255)
#define	CursorColourBack			Rgb24(192, 192, 192)

#define M_INFO						(M_USER + 0x400)

#define HEX_COLUMN					13
#define TEXT_COLUMN					63

#define FILE_BUFFER_SIZE			1024
#define	UI_UPDATE_SPEED				500 // ms

///////////////////////////////////////////////////////////////////////////////////////////////
class RandomData : public GStream
{
	#ifdef WIN32
	typedef BOOLEAN (APIENTRY *RtlGenRandom)(void*, ULONG);
	HCRYPTPROV	phProv;
	HMODULE		hADVAPI32;
	RtlGenRandom GenRandom;
	#endif

public:
	RandomData()
	{
		#ifdef WIN32
		phProv = 0;
		hADVAPI32 = 0;
		GenRandom = 0;

		if (!CryptAcquireContext(&phProv, 0, 0, PROV_RSA_FULL, 0))
		{
			// f***ing windows... try a different strategy.
			hADVAPI32 = LoadLibrary("ADVAPI32.DLL");
			if (hADVAPI32)
			{
				GenRandom = (RtlGenRandom) GetProcAddress(hADVAPI32, "SystemFunction036");
			}
		}			
		#endif
	}

	~RandomData()
	{
		#ifdef WIN32
		if (phProv)
			CryptReleaseContext(phProv, 0);
		else if (hADVAPI32)
			FreeLibrary(hADVAPI32);
		#endif
	}

	int Read(void *Ptr, int Len, int Flags = 0)
	{
		#ifdef WIN32
		if (phProv)
		{
			if (CryptGenRandom(phProv, Len, (uchar*)Ptr))
				return Len;
		}
		else if (GenRandom)
		{
			if (GenRandom(Ptr, Len))
				return Len;
		}
		#endif
		return 0;
	}
};

#define OPT_FindText		"FindText"
#define OPT_FindHex			"FindHex"
#define OPT_FindMatchCase	"MatchCase"
#define OPT_FindMatchWord	"MatchWord"
#define OPT_FindFolder		"FindFolder"
#define OPT_FindRecurse		"FindRecurse"
#define OPT_FindFileTypes	"FileTypes"

class FindInFiles : public GDialog, public GXmlToUi
{
	GDom *Opts;

public:
	FindInFiles(GViewI *p, GDom *opts)
	{
		Opts = opts;
		SetParent(p);
		if (LoadFromResource(IDD_FIND_IN_FILES))
		{
			MoveToCenter();

			Map(OPT_FindText, IDC_TXT);
			Map(OPT_FindHex, IDC_HEX);
			Map(OPT_FindMatchCase, IDC_MATCH_CASE);
			Map(OPT_FindMatchWord, IDC_MATCH_WORD);
			Map(OPT_FindFolder, IDC_FOLDER);
			Map(OPT_FindRecurse, IDC_RECURSE);
			Map(OPT_FindFileTypes, IDC_TYPES);

			Convert(Opts, this, true);
		}
	}

	void OnCreate()
	{
		GViewI *v;
		if (GetViewById(IDC_TXT, v))
			v->Focus(true);
	}

	int OnNotify(GViewI *c, int f)
	{
		switch (c->GetId())
		{
			case IDC_BROWSE_FOLDER:
			{
				GFileSelect f;
				f.Parent(this);
				if (f.OpenFolder())
				{
					SetCtrlName(IDC_FOLDER, f.Name());
				}
				break;
			}
			case IDOK:
			case IDCANCEL:
			{
				Convert(Opts, this, false);
				EndModal(c->GetId() == IDOK);
			}
		}
		return 0;
	}
};

class ResultItem : public GListItem
{
public:
	GAutoString Path;
	uint64 Pos;

	ResultItem(char *p, uint64 pos)
	{
		Path.Reset(NewStr(p));
		Pos = pos;
	}

	char *GetText(int Col)
	{
		switch (Col)
		{
			case 0:
				return Path;
			case 1:
			{
				static char s[32];
				sprintf(s, LGI_PrintfInt64, Pos);
				return s;
			}
		}

		return 0;
	}
};

bool FileCallback(void *UserData, char *Path, GDirectory *Dir);

class FindUtils
{
protected:
	GList *Results;
	bool Loop;
	uint64 Prev;
	GAutoString Info;

public:
	FindUtils(GList *r)
	{
		Results = r;
		Loop = true;
		Prev = 0;
	}

	bool Search(char *Path, GStream &s, GArray<uint8> &Needle, bool FindMatchCase)
	{
		bool Status = false;
		GArray<uint8> b;
		GArray<uint8> not;
		b.Length(2 << 20);
		not.Length(256);
		memset(&not[0], 1, not.Length());
		for (int n=0; n<Needle.Length(); n++)
		{
			not[Needle[n]] = false;
		}

		uint64 Now = LgiCurrentTime();
		if (Results &&
			Results->GetWindow() &&
			Now - Prev > 1000)
		{
			Prev = Now;
			if (!Info)
			{
				Info.Reset(NewStr(Path));
				Results->GetWindow()->PostEvent(M_INFO, (GMessage::Param)&Info);
			}
		}

		int used = 0;
		for (uint64 i=0; Loop && i<s.GetSize(); )
		{
			int r = s.Read(&b[used], b.Length() - used);
			if (r <= 0)
				break;

			used += r;
			int slen = used - Needle.Length();
			if (slen <= 0)
				break;

			if (FindMatchCase)
			{
				for (int n=0; n<slen; n++)
				{
				}
			}
			else
			{
				uint8 *data_p = &b[0];
				uint8 *end_p = &b[slen];
				uint8 *not_p = &not[0];
				uint8 *needle_p = &Needle[0];
				int n_len = Needle.Length();
				while (data_p < end_p)
				{
					if (not_p[ data_p[n_len-1] ])
					{
						data_p += n_len;
						continue;
					}
					
					if (*data_p != *needle_p)
					{
						data_p++;
						continue;
					}

					if (!memcmp(data_p, needle_p, n_len))
					{
						Results->Insert(new ResultItem(Path, data_p - &b[0] + i));
						Status = true;
						data_p += n_len;
					}
					else
						data_p++;
				}
			}

			i += slen;
			used = r - slen;
			memmove(&b[0], &b[slen], used);
		}

		return Status;
	}

	void OnFinished()
	{
		if (Results &&
			Results->GetWindow())
		{
			Info.Reset(NewStr("Finished"));
			Results->GetWindow()->PostEvent(M_INFO, (GMessage::Param)&Info);
		}
	}
};

class FindInFilesThread : public GThread, public FindUtils
{
	GSemaphore Lock;
	GOptionsFile *Opts;
	GVariant FindText, FindHex, FindMatchCase, FindMatchWord, FindFolder, FindRecurse, FindFileTypes;
	GToken Types;
	GArray<uint8> Needle;

public:
	FindInFilesThread(GOptionsFile *opts, GList *results) : FindUtils(results)
	{
		Opts = opts;
		Opts->GetValue(OPT_FindText, FindText);
		Opts->GetValue(OPT_FindHex, FindHex);
		Opts->GetValue(OPT_FindMatchCase, FindMatchCase);
		Opts->GetValue(OPT_FindMatchWord, FindMatchWord);
		Opts->GetValue(OPT_FindFolder, FindFolder);
		Opts->GetValue(OPT_FindRecurse, FindRecurse);
		Opts->GetValue(OPT_FindFileTypes, FindFileTypes);
		Types.Parse(FindFileTypes.Str(), ",;: ");

		if (FindHex.Str())
		{
			GArray<char> h;
			for (char *s = FindHex.Str(); *s; s++)
			{
				if (
					(*s >= '0' && *s <= '9') ||
					(*s >= 'a' && *s <= 'f') ||
					(*s >= 'A' && *s <= 'F')
					)
					h.Add(*s);

				if (h.Length() == 2)
				{
					h.Add(0);
					Needle.Add(htoi(&h[0]));
					h.Length(0);
				}
			}
		}
		else
		{
			Needle.Add((uint8*)FindText.Str(), strlen(FindText.Str()));
		}

		Run();
	}

	~FindInFilesThread()
	{
		Loop = false;
		while (!IsExited())
		{
			LgiSleep(1);
		}
	}

	bool OnFile(char *Path, GDirectory *Dir)
	{
		if (!Loop)
			return false;

		char *l = strrchr(Path, DIR_CHAR);
		if (!l++)
			return false;

		if (Dir->IsDir())
			return true;

		bool Match = Types.Length() == 0;
		if (!Match)
		{
			for (int i=0; i<Types.Length(); i++)
			{
				if (MatchStr(Types[i], l))
				{
					Match = true;
					break;
				}
			}
		}
		if (Match)
		{
			GFile f;
			if (f.Open(Path, O_READ))
			{
				Search(Path, f, Needle, FindMatchCase.CastBool());
			}
		}

		return true;
	}

	int Main()
	{
		if (FindRecurse.CastBool())
		{
			LgiRecursiveFileSearch(FindFolder.Str(), 0, 0, 0, 0, (RecursiveFileSearch_Callback)FileCallback, this);
		}
		else
		{
			GAutoPtr<GDirectory> d(FileDev->GetDir());
			for (bool b=d->First(FindFolder.Str()); b; b=d->Next())
			{
				char p[MAX_PATH];
				if (!d->IsDir() &&
					d->Path(p, sizeof(p)) &&
					!OnFile(p, d))
					break;
			}
		}

		if (Loop)
			OnFinished();
		return 0;
	}
};

bool FileCallback(void *UserData, char *Path, GDirectory *Dir)
{
	return ((FindInFilesThread*)UserData)->OnFile(Path, Dir);
}

class FindInFilesResults : public GWindow, public GLgiRes
{
	AppWnd *App;
	GOptionsFile *Opts;
	GAutoPtr<FindInFilesThread> Thread;
	GList *Lst;

public:
	FindInFilesResults(AppWnd *app, GOptionsFile *opts)
	{
		App = app;
		Opts = opts;
		if (LoadFromResource(IDD_FIND_RESULTS, this) &&
			GetViewById(IDC_RESULTS, Lst))
		{
			GRect r(0, 0, 1000, 800);
			SetPos(r);
			MoveToCenter();
			
			GLayout *v;
			if (GetViewById(IDC_TABLE, v))
			{
				v->SetPourLargest(true);
			}

			if (Attach(0))
			{
				AttachChildren();
				Visible(true);
				Thread.Reset(new FindInFilesThread(Opts, Lst));
			}
		}
	}

	int OnNotify(GViewI *v, int f)
	{
		switch (v->GetId())
		{
			case IDC_RESULTS:
			{
				if (f == GLIST_NOTIFY_DBL_CLICK)
				{
					List<ResultItem> r;
					if (Lst->GetSelection(r))
					{
						ResultItem *i = r.First();
						if (i)
						{
							App->OnResult(i->Path, i->Pos);
						}
					}
				}
				break;
			}
			case IDOK:
			{
				Quit();
				break;
			}
		}

		return 0;
	}

	int OnEvent(GMessage *m)
	{
		if (MsgCode(m) == M_INFO)
		{
			GAutoString *a = (GAutoString*) MsgA(m);
			if (a)
			{
				SetCtrlName(IDC_INFO, *a);
				a->Reset();

				GTableLayout *t;
				if (GetViewById(IDC_TABLE, t))
				{
					t->InvalidateLayout();
				}
			}
		}

		return GWindow::OnEvent(m);
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////
class ChangeSizeDlg : public GDialog
{
	int OldUnits;

public:
	int64 Size;

	ChangeSizeDlg(AppWnd *app, int64 size)
	{
		SetParent(app);
		if (LoadFromResource(IDD_CHANGE_FILE_SIZE))
		{
			MoveToCenter();

			GCombo *Units = dynamic_cast<GCombo*>(FindControl(IDC_UNITS));
			if (Units)
			{
				Units->Insert("bytes");
				Units->Insert("KB");
				Units->Insert("MB");
				Units->Insert("GB");

				if (size < 10 << 10)
				{
					SetCtrlValue(IDC_UNITS, 0);
				}
				else if (size < 1 << 20)
				{
					SetCtrlValue(IDC_UNITS, 1);
				}
				else if (size < 1 << 30)
				{
					SetCtrlValue(IDC_UNITS, 2);
				}
				else
				{
					SetCtrlValue(IDC_UNITS, 3);
				}

				SetBytes(size);
				OnNotify(FindControl(IDC_NUMBER), 0);
			}
		}
	}

	void SetBytes(int64 size)
	{
		switch (GetCtrlValue(IDC_UNITS))
		{
			case 0:
			{
				char s[64];
				#ifndef WIN32
				sprintf(s, "%ld", size);
				#else
				sprintf(s, "%I64u", size);
				#endif
				SetCtrlName(IDC_NUMBER, s);
				break;
			}
			case 1:
			{
				char s[64];
				double d = (double)size / 1024.0;
				sprintf(s, "%f", d);
				SetCtrlName(IDC_NUMBER, s);
				break;
			}
			case 2:
			{
				char s[64];
				double d = (double)size / 1024.0 / 1024.0;
				sprintf(s, "%f", d);
				SetCtrlName(IDC_NUMBER, s);
				break;
			}
			case 3:
			{
				char s[64];
				double d = (double)size / 1024.0 / 1024.0 / 1024.0;
				sprintf(s, "%f", d);
				SetCtrlName(IDC_NUMBER, s);
				break;
			}
		}

		OldUnits = GetCtrlValue(IDC_UNITS);
	}

	int64 GetBytes(int Units = -1)
	{
		int64 n = 0;
		char *s = GetCtrlName(IDC_NUMBER);
		if (s)
		{
			switch (Units >= 0 ? Units : GetCtrlValue(IDC_UNITS))
			{
				case 0: // bytes
				{
					n = _atoi64(s);
					break;
				}
				case 1: // KB
				{
					n = (int64) (atof(s) * 1024.0);
					break;
				}
				case 2: // MB
				{
					n = (int64) (atof(s) * 1024.0 * 1024.0);
					break;
				}
				case 3: // GB
				{
					n = (int64) (atof(s) * 1024.0 * 1024.0 * 1024.0);
					break;
				}
			}
		}

		return n;
	}

	int OnNotify(GViewI *c, int f)
	{
		if (!c) return 0;
		switch (c->GetId())
		{
			case IDC_UNITS:
			{
				SetBytes(Size);
				break;
			}
			case IDC_NUMBER:
			{
				Size = GetBytes();

				char s[64];
				#ifdef WIN32
				sprintf(s, "(%I64i bytes)", GetBytes());
				#else
				sprintf(s, "(%ld bytes)", GetBytes());
				#endif
				SetCtrlName(IDC_BYTE_SIZE, s);
				break;
			}
			case IDOK:
			case IDCANCEL:
			{
				EndModal(c->GetId() == IDOK);
				break;
			}
		}

		return 0;
	}
};

///////////////////////////////////////////////////////////////////////////////////////////////
enum PaneType
{
	HexPane,
	AsciiPane
};

class IHexBar;
class GHexView : public GLayout
{
	AppWnd *App;
	IHexBar *Bar;
	GFont *Font;
	int CharX, LineY;
	bool IsHex;
	bool IsReadOnly;

	// File
	GFile *File;
	int64 Size;

	// Comparision file
	GFile *Compare;
	int64 CompareSize;
	uchar *CompareMap;

	// Buffer
	uchar *Buf;			// Buffer for data from the file
	int BufLen;			// Length of the data buffer
	int BufUsed;		// Length of the buffer used
	int64 BufPos;		// Where the start of the buffer came from in the file
	
	// Cursor
	int64 Cursor;		// Offset into the file that the cursor is over
	int Nibble;			// 0 or 1, defining the nibble pointed to
						// 0: 0xc0, 1: 0x0c etc
	PaneType Pane;		// 0 = hex, 1 = ascii
	bool Flash;
	GRect CursorText, CursorHex;

	// Selection
	int64 Select;
	int SelectNibble;

	bool GetData(int64 Start, int Len);
	void SwapBytes(void *p, int Len);
	GRect GetPositionAt(int64 Offset);

public:
	GHexView(AppWnd *app, IHexBar *bar);
	~GHexView();

	bool OpenFile(char *FileName, bool ReadOnly);
	bool SaveFile(char *FileName);
	bool HasFile() { return File != 0; }
	bool HasSelection() { return Select >= 0; }

	void Copy(bool AsHex = false);
	void Paste();
	void Paste(void *Ptr, int Len);
	void SelectAll();

	void SaveSelection(char *File);
	void SelectionFillRandom(GStream *Rnd);
	void CompareFile(char *File);

	void SetScroll();
	void SetCursor(int64 cursor, int nibble = 0, bool select = false);
	void SetIsHex(bool i);
	int64 GetFileSize() { return Size; }
	bool SetFileSize(int64 Size);
	void DoInfo();
	int64 Search(SearchDlg *For, uchar *Bytes, int Len);
	void DoSearch(SearchDlg *For);
	bool GetCursorFromLoc(int x, int y, int64 &Cursor, int &Nibble);
	bool GetDataAtCursor(char *&Data, int &Len);
	void SetBit(uint8 Bit, bool On);
	void SetByte(uint8 Byte);
	void SetShort(uint16 Byte);
	void SetInt(uint32 Byte);

	bool Pour(GRegion &r);

	int OnNotify(GViewI *c, int f);
	void OnPosChange();
	void OnPaint(GSurface *pDC);
	void OnMouseClick(GMouse &m);
	void OnMouseMove(GMouse &m);
	void OnFocus(bool f);
	bool OnKey(GKey &k);
	void OnMouseWheel(double Lines);
	void OnPulse();
	void OnCreate() { SetPulse(500); }
};

class IHexBar : public GLayout, public GLgiRes
{
	friend class AppWnd;

	AppWnd *App;
	GHexView *View;
	int Y;

public:
	bool NotifyOff;

	IHexBar(AppWnd *a, int y);
	~IHexBar();

	int64 GetOffset(int IsHex = -1, bool *Select = 0);
	void SetOffset(int64 Offset);
	bool IsSigned();
	bool IsLittleEndian();

	bool Pour(GRegion &r);
	void OnPaint(GSurface *pDC);
	int OnNotify(GViewI *c, int f);
};

/////////////////////////////////////////////////////////////////////////////////////
IHexBar::IHexBar(AppWnd *a, int y)
{
	App = a;
	View = 0;
	Y = y;
	_IsToolBar = true;
	NotifyOff = false;
	Attach(App);

	LoadFromResource(IDD_HEX, this);
	for (GViewI *c=Children.First(); c; c=Children.Next())
	{
		GRect r = c->GetPos();
		r.Offset(1, 4);
		c->SetPos(r);
	}
	AttachChildren();

	GVariant v;
	if (!a->GetOptions() || !a->GetOptions()->GetValue("IsHex", v))
		v = true;
	SetCtrlValue(IDC_IS_HEX, v.CastInt32());

	if (!a->GetOptions() || !a->GetOptions()->GetValue("LittleEndian", v))
		v = true;
	SetCtrlValue(IDC_LITTLE, v.CastInt32());

	SetCtrlValue(IDC_OFFSET, 0);
}

IHexBar::~IHexBar()
{
	if (App && App->GetOptions())
	{
		GVariant v;
		App->GetOptions()->SetValue("IsHex", v = GetCtrlValue(IDC_IS_HEX));
		App->GetOptions()->SetValue("LittleEndian", v = GetCtrlValue(IDC_LITTLE));
	}
}

bool IHexBar::Pour(GRegion &r)
{
	GRect *Best = FindLargestEdge(r, GV_EDGE_TOP);
	if (Best)
	{
		GRect r = *Best;
		if (r.Y() != Y)
		{
			r.y2 = r.y1 + Y - 1;
		}

		SetPos(r, true);

		return true;
	}
	return false;
}

void IHexBar::OnPaint(GSurface *pDC)
{
	GRect r = GetClient();
	LgiThinBorder(pDC, r, RAISED);
	pDC->Colour(LC_MED, 24);
	pDC->Rectangle(&r);

	#define Divider(x) \
		pDC->Colour(LC_LOW, 24); \
		pDC->Line(x, 1, x, pDC->Y()); \
		pDC->Colour(LC_WHITE, 24); \
		pDC->Line(x+1, 1, x+1, pDC->Y());

	Divider(134);
	Divider(268);
}

bool IHexBar::IsSigned()
{
	return GetCtrlValue(IDC_SIGNED);
}

bool IHexBar::IsLittleEndian()
{
	return GetCtrlValue(IDC_LITTLE);
}

void IHexBar::SetOffset(int64 Offset)
{
	if (GetCtrlValue(IDC_IS_HEX))
	{
		char s[64];
		char *c = s;
		for (int i=0; i<16; i++)
		{
			int n = (Offset >> ((15 - i) * 4)) & 0xf;
			if (n || c > s)
			{
				c += sprintf(c, "%01.1X", n);
			}
		}
		*c++ = 0;

		SetCtrlName(IDC_OFFSET, s);
	}
	else
	{
		SetCtrlValue(IDC_OFFSET, Offset);
	}
}

int64 IHexBar::GetOffset(int IsHex, bool *Select)
{
	int64 c = 0;
	char *o = GetCtrlName(IDC_OFFSET);
	if (o)
	{
		if (IsHex < 0)
			IsHex = GetCtrlValue(IDC_IS_HEX);
			
		char LastOp = 0;
		for (char *s = o; s && *s; )
		{
			char *Tok = LgiTokStr(s);
			if (Tok)
			{
				printf("Tok='%s'\n", Tok);
				
				if (*Tok == '-' ||
					*Tok == '+' ||
					*Tok == '&')
				{
					LastOp = *Tok;
				}
				else
				{
					int64 i;
					if (IsHex)
					{
						i = htoi64(Tok);
					}
					else
					{
						i = atoi64(Tok);
					}

					if (LastOp == '+' ||
						LastOp == '&')
					{
						c += i;

						if (LastOp == '&' && Select) *Select = true;
					}
					else if (LastOp == '-')
					{
						c -= i;
					}
					else
					{
						c = i;
					}
					printf("i=%i c=%i\n", (int)i, (int)c);
				}
				DeleteArray(Tok);
			}
		}
	}

	return c;
}

int IHexBar::OnNotify(GViewI *c, int f)
{
	if (NotifyOff)
		return 0;

	switch (c->GetId())
	{
		case IDC_SIGNED:
		case IDC_LITTLE:
		{
			if (View)
			{
				View->DoInfo();
				View->Focus(true);
			}
			break;
		}
		case IDC_OFFSET:
		{
			if (View &&
				f == VK_RETURN)
			{
				// Set the cursor
				bool Select = false;
				int64 Off = GetOffset(-1, &Select);

				View->SetCursor(Select ? Off - 1 : Off, Select, Select);

				// Return focus to the view
				View->Focus(true);
			}
			break;
		}
		case IDC_IS_HEX:
		{
			if (View)
			{
				bool IsHex = GetCtrlValue(IDC_IS_HEX);

				// Change format of the edit box
				SetOffset(GetOffset(!IsHex));

				// Tell the hex view
				View->SetIsHex(IsHex);

				// Return focus to the view
				View->Focus(true);
			}
			break;
		}
		case IDC_BIT7:
		{
			if (View) View->SetBit(0x80, c->Value());
			break;
		}
		case IDC_BIT6:
		{
			if (View) View->SetBit(0x40, c->Value());
			break;
		}
		case IDC_BIT5:
		{
			if (View) View->SetBit(0x20, c->Value());
			break;
		}
		case IDC_BIT4:
		{
			if (View) View->SetBit(0x10, c->Value());
			break;
		}
		case IDC_BIT3:
		{
			if (View) View->SetBit(0x08, c->Value());
			break;
		}
		case IDC_BIT2:
		{
			if (View) View->SetBit(0x04, c->Value());
			break;
		}
		case IDC_BIT1:
		{
			if (View) View->SetBit(0x02, c->Value());
			break;
		}
		case IDC_BIT0:
		{
			if (View) View->SetBit(0x01, c->Value());
			break;
		}
		case IDC_DEC_1:
		{
			if (View && f == VK_RETURN) View->SetByte(atoi(c->Name()));
			break;
		}
		case IDC_HEX_1:
		{
			if (View && f == VK_RETURN) View->SetByte(htoi(c->Name()));
			break;
		}
		case IDC_DEC_2:
		{
			if (View && f == VK_RETURN) View->SetShort(atoi(c->Name()));
			break;
		}
		case IDC_HEX_2:
		{
			if (View && f == VK_RETURN) View->SetShort(htoi(c->Name()));
			break;
		}
		case IDC_DEC_4:
		{
			if (View && f == VK_RETURN) View->SetInt(atoi(c->Name()));
			break;
		}
		case IDC_HEX_4:
		{
			if (View && f == VK_RETURN) View->SetInt(htoi(c->Name()));
			break;
		}
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////////////////
GHexView::GHexView(AppWnd *app, IHexBar *bar)
{
	// Init
	Flash = true;
	App = app;
	Bar = bar;
	File = 0;
	Buf = 0;
	BufLen = 0;
	BufUsed = 0;
	BufPos = 0;
	Cursor = -1;
	Nibble = 0;
	Font = 0;
	Size = 0;
	LineY = 16;
	CharX = 8;
	IsHex = true;
	Pane = HexPane;
	Select = -1;
	SelectNibble = 0;
	IsReadOnly = false;
	Compare = 0;
	CompareSize = 0;
	CompareMap = 0;

	SetId(IDC_HEX_VIEW);

	// Font
	GFontType Type;
	if (Type.GetSystemFont("Fixed"))
	{
		Font = Type.Create();
		if (Font)
		{
			GDisplayString ds(Font, "A");
			CharX = ds.X();
			LineY = ds.Y();
		}
	}
	else LgiAssert(0);

	Attach(App);
	Name("GHexView");
	SetScrollBars(false, true);
	if (VScroll)
	{
		VScroll->SetNotify(this);
	}
}

GHexView::~GHexView()
{
	DeleteObj(Compare);
	DeleteArray(CompareMap);
	DeleteObj(Font);
	DeleteObj(File);
	DeleteArray(Buf);
}

GRect GHexView::GetPositionAt(int64 Offset)
{
	GRect c = GetClient();
	GRect Status;
	int64 Start = (VScroll ? (uint64)VScroll->Value() : 0) * 16;
	int Window = (c.Y() / LineY) * 16;

	if (Offset >= Start && Offset < Start + Window)
	{
		// On screen
		Status.ZOff(c.X(), LineY);
		Status.Offset(10 * CharX, ((Offset - Start) / 16) * LineY);
	}
	else
	{
		// Off screen..
		Status.ZOff(-1, -1); // NULL region
	}

	return Status;	
}

int GHexView::OnNotify(GViewI *c, int f)
{
	switch (c->GetId())
	{
		case IDC_VSCROLL:
		{
			Invalidate();
			break;
		}
	}

	return 0;
}

bool GHexView::SetFileSize(int64 size)
{
	// Save any changes
	if (App->SetDirty(false))
	{
		Size = File->SetSize(size);

		int p = BufPos;
		BufPos++;
		GetData(p, 1);

		SetScroll();
		Invalidate();
	}

	return false;
}

void GHexView::SetIsHex(bool i)
{
	if (i != IsHex)
	{
		IsHex = i;
		Invalidate();
	}
}

void GHexView::SetScroll()
{
	if (VScroll)
	{
		int LineY = Font->GetHeight();
		int Lines = GetClient().Y() / LineY;
		int64 DocLines = (Size + 15) / 16;

		VScroll->SetLimits(0, DocLines-1);
		VScroll->SetPage(Lines);
	}
}

void GHexView::SwapBytes(void *p, int Len)
{
	if (Bar && !Bar->IsLittleEndian())
	{
		uchar *c = (uchar*)p;
		for (int i=0; i<Len>>1; i++)
		{
			uchar t = c[i];
			c[i] = c[Len-1-i];
			c[Len-1-i] = t;
		}
	}
}

bool GHexView::GetData(int64 Start, int Len)
{
	static bool IsAsking = false;
	bool Status = false;

	if (!IsAsking && File && File->IsOpen())
	{
		// is the buffer allocated
		if (!Buf)
		{
			BufLen = FILE_BUFFER_SIZE << 10;
			BufPos = 1;
			Buf = new uchar[BufLen];
			LgiAssert(Buf);
		}

		// is the cursor outside the buffer?
		if (Start < BufPos || (Start + Len) >= (BufPos + BufLen))
		{
			// clear changes
			BufUsed = 0;
			
			IsAsking = true;
			bool IsClean = App->SetDirty(false);
			IsAsking = false;

			if (IsClean)
			{
				// move buffer to cover cursor pos
				int Half = BufLen >> 1;
				BufPos = Start - (Start % Half);
				if (File->Seek(BufPos, SEEK_SET) == BufPos)
				{
					memset(Buf, 0xcc, BufLen);
					BufUsed = File->Read(Buf, BufLen);
					Status =	(Start >= BufPos) &&
								((Start + Len) < (BufPos + BufUsed));

					// Check for comparision file
					if (Compare && CompareMap)
					{
						if (Compare->Seek(BufPos, SEEK_SET) == BufPos)
						{
							Compare->Read(CompareMap, BufUsed);
							for (int i=0; i<BufUsed; i++)
							{
								CompareMap[i] = CompareMap[i] != Buf[i];
							}
						}
					}
				}
			}
		}
		else
		{
			Status = (Start >= BufPos) && (Start + Len <= BufPos + BufUsed);
		}
	}

	return Status;
}

bool GHexView::GetDataAtCursor(char *&Data, int &Len)
{
	if (Buf)
	{
		int Offset = Cursor - BufPos;
		Data = (char*)Buf + Offset;
		Len = min(BufUsed, BufLen) - Offset;
		return true;
	}
	return false;
}

void GHexView::SetCursor(int64 cursor, int nibble, bool Selecting)
{
	GRect OldPos = GetPositionAt(Cursor);

	if (Selecting)
	{
		if (Select < 0)
		{
			Select = Cursor;
			SelectNibble = Nibble;
		}
	}
	else
	{
		if (Select >= 0)
		{
			Select = -1;
			OldPos = GetClient();
		}
	}

	// Limit to doc
	if (cursor >= Size)
	{
		cursor = Size - 1;
		nibble = 1;
	}
	if (cursor < 0)
	{
		cursor = 0;
		nibble = 0;
	}

	// Is different?
	if (Cursor != cursor ||
		Nibble != nibble)
	{
		// Set the cursor
		Cursor = cursor;
		Nibble = nibble;

		// Make sure the cursor is in the viewable area?
		if (VScroll)
		{
			int64 Start = (uint64) (VScroll ? VScroll->Value() : 0) * 16;
			int Lines = GetClient().Y() / LineY;
			int64 End = min(Size, Start + (Lines * 16));
			if (Cursor < Start)
			{
				// Scroll up
				VScroll->Value((Cursor - (Cursor%16)) / 16);
				OldPos = GetClient();
			}
			else if (Cursor >= End)
			{
				// Scroll down
				int64 NewVal = (Cursor - (Cursor%16) - ((Lines-1) * 16)) / 16;
				VScroll->Value(NewVal);
				OldPos = GetClient();
			}
		}

		if (Bar)
		{
			Bar->SetOffset(Cursor);
			DoInfo();
		}
		
		GRect NewPos = GetPositionAt(Cursor);
		
		if (Select >= 0)
		{
			NewPos.Union(&OldPos);
			Invalidate(&NewPos);
		}
		else
		{
			if (OldPos != NewPos) Invalidate(&OldPos);
			Invalidate(&NewPos);
		}
	}

	SendNotify(GTVN_CURSOR_CHANGED);
}

int64 GHexView::Search(SearchDlg *For, uchar *Bytes, int Len)
{
	int64 Hit = -1;

	if (For->Bin && For->Length > 0)
	{
		for (int i=0; i<Len - For->Length; i++)
		{
			bool Match = true;
			for (int n=0; n<For->Length; n++)
			{
				if (For->MatchCase || For->ForHex)
				{
					if (For->Bin[n] != Bytes[i+n])
					{
						Match = false;
						break;
					}
				}
				else
				{
					if (tolower(For->Bin[n]) != tolower(Bytes[i+n]))
					{
						Match = false;
						break;
					}
				}
			}
			if (Match)
			{
				Hit = i;
				break;
			}
		}
	}

	return Hit;
}

void GHexView::DoSearch(SearchDlg *For)
{
	int Block = 32 << 10;
	int64 Hit = -1, c;
	int64 Time = LgiCurrentTime();
	GProgressDlg *Prog = 0;

	// Search through to the end of the file...
	for (c = Cursor + 1; c < GetFileSize(); c += Block)
	{
		int Actual = min(Block, GetFileSize() - c);
		if (GetData(c, Actual))
		{
			Hit = Search(For, Buf + (c - BufPos), Actual);
			if (Hit >= 0)
			{
				Hit += c;
				break;
			}

			int64 Now = LgiCurrentTime();
			if (Now - Time > UI_UPDATE_SPEED)
			{
				Time = Now;
				if (!Prog)
				{
					if (Prog = new GProgressDlg(this))
					{
						Prog->SetDescription("Searching...");
						Prog->SetLimits(0, GetFileSize());
						Prog->SetScale(1.0 / 1024.0);
						Prog->SetType("kb");
					}
				}
				else
				{
					Prog->Value(c - Cursor);
					LgiYield();
				}
			}
		}
		else break;
	}

	if (Hit < 0)
	{
		// Now search from the start of the file to the original cursor
		for (c = 0; c < Cursor; c += Block)
		{
			if (GetData(c, Block))
			{
				int Actual = min(Block, Cursor - c);
				Hit = Search(For, Buf + (c - BufPos), Actual);
				if (Hit >= 0)
				{
					Hit += c;
					break;
				}

				int64 Now = LgiCurrentTime();
				if (Now - Time > UI_UPDATE_SPEED)
				{
					Time = Now;
					if (!Prog)
					{
						if (Prog = new GProgressDlg(this))
						{
							Prog->SetDescription("Searching...");
							Prog->SetLimits(0, GetFileSize());
							Prog->SetScale(1.0 / 1024.0);
							Prog->SetType("kb");
						}
					}
					else
					{
						Prog->Value(GetFileSize()-Cursor+c);
						LgiYield();
					}
				}
			}
			else break;
		}
	}

	if (Hit >= 0)
	{
		SetCursor(Hit);
		SetCursor(Hit + For->Length - 1, 1, true);
	}

	DeleteObj(Prog);
}

void GHexView::SetBit(uint8 Bit, bool On)
{
	if (GetData(Cursor, 1))
	{
		if (On)
		{
			Buf[Cursor-BufPos] |= Bit;
		}
		else
		{
			Buf[Cursor-BufPos] &= ~Bit;
		}

		App->SetDirty(true);
		Invalidate();
		DoInfo();
	}	
}

void GHexView::SetByte(uint8 Byte)
{
	if (GetData(Cursor, 1))
	{
		if (Buf[Cursor-BufPos] != Byte)
		{
			Buf[Cursor-BufPos] = Byte;
			App->SetDirty(true);
			Invalidate();
			DoInfo();
		}
	}	
}

void GHexView::SetShort(uint16 Short)
{
	if (GetData(Cursor, 2))
	{
		SwapBytes(&Short, sizeof(Short));

		uint16 *p = (uint16*) (&Buf[Cursor-BufPos]);
		if (*p != Short)
		{
			*p = Short;
			App->SetDirty(true);
			Invalidate();
			DoInfo();
		}
	}	
}

void GHexView::SetInt(uint32 Int)
{
	if (GetData(Cursor, 4))
	{
		SwapBytes(&Int, sizeof(Int));

		uint32 *p = (uint32*) (&Buf[Cursor-BufPos]);
		if (*p != Int)
		{
			*p = Int;
			App->SetDirty(true);
			Invalidate();
			DoInfo();
		}
	}	
}

void GHexView::DoInfo()
{
	if (Bar)
	{
		bool IsSigned = Bar->IsSigned();
		GView *w = GetWindow();

		char s[256] = "";
		if (GetData(Cursor, 1))
		{
			int c = Buf[Cursor-BufPos], sc;
			if (IsSigned)
				sc = (char)Buf[Cursor-BufPos];
			else
				sc = Buf[Cursor-BufPos];

			Bar->NotifyOff = true;

			sprintf(s, "%i", sc);
			w->SetCtrlName(IDC_DEC_1, s);
			sprintf(s, "%02.2X", c);
			w->SetCtrlName(IDC_HEX_1, s);
			sprintf(s, "%c", c >= ' ' && c <= 0x7f ? c : '.');
			w->SetCtrlName(IDC_ASC_1, s);

			uint8 Bits = Buf[Cursor-BufPos];
			Bar->SetCtrlValue(IDC_BIT7, (Bits & 0x80) != 0);
			Bar->SetCtrlValue(IDC_BIT6, (Bits & 0x40) != 0);
			Bar->SetCtrlValue(IDC_BIT5, (Bits & 0x20) != 0);
			Bar->SetCtrlValue(IDC_BIT4, (Bits & 0x10) != 0);
			Bar->SetCtrlValue(IDC_BIT3, (Bits & 0x08) != 0);
			Bar->SetCtrlValue(IDC_BIT2, (Bits & 0x04) != 0);
			Bar->SetCtrlValue(IDC_BIT1, (Bits & 0x02) != 0);
			Bar->SetCtrlValue(IDC_BIT0, (Bits & 0x01) != 0);

			Bar->NotifyOff = false;
		}
		if (GetData(Cursor, 2))
		{
			uint16 *sp = (uint16*)(Buf+(Cursor-BufPos));
			uint16 c = *sp;
			SwapBytes(&c, sizeof(c));
			int c2 = (int16)c;

			sprintf(s, "%i", IsSigned ? c2 : c);
			w->SetCtrlName(IDC_DEC_2, s);
			sprintf(s, "%04.4X", c);
			w->SetCtrlName(IDC_HEX_2, s);
		}
		if (GetData(Cursor, 4))
		{
			uint32 *lp = (uint32*)(Buf+(Cursor-BufPos));
			uint32 c = *lp;
			SwapBytes(&c, sizeof(c));

			sprintf(s, IsSigned ? "%i" : "%u", c);
			w->SetCtrlName(IDC_DEC_4, s);
			sprintf(s, "%08.8X", c);
			w->SetCtrlName(IDC_HEX_4, s);
		}
	}
}

void GHexView::CompareFile(char *File)
{
	DeleteObj(Compare);
	if (File)
	{
		if (Compare = new GFile)
		{
			if (Compare->Open(File, O_READ))
			{
				if (!CompareMap) CompareMap = new uchar[BufLen];
				CompareSize = Compare->GetSize();
				BufPos = -(BufLen * 2);
				Invalidate();
			}
			else DeleteObj(Compare);
		}
	}
}

bool GHexView::OpenFile(char *FileName, bool ReadOnly)
{
	bool Status = false;

	if (App->SetDirty(false))
	{
		DeleteObj(Compare);
		DeleteArray(CompareMap);
		DeleteObj(File);
		DeleteArray(Buf);
		BufPos = 0;

		if (FileName)
		{
			File = new GFile;
			if (File)
			{
				bool IsOpen = false;

				if (FileExists(FileName))
				{
					if (!File->Open(FileName, ReadOnly ? O_READ : O_READWRITE))
					{
						if (File->Open(FileName, O_READ))
						{
							IsReadOnly = true;
							IsOpen = true;
						}
					}
					else
					{
						IsOpen = true;
						IsReadOnly = false;
					}
				}

				if (IsOpen)
				{
					Focus(true);
					Size = File->GetSize();
					SetCursor(0);
					SetScroll();
					Status = true;
				}
				else
				{
					LgiMsg(this, "Couldn't open '%s' for reading.", AppName, MB_OK, FileName);
				}
			}
		}
		else if (VScroll)
		{
			VScroll->SetLimits(0, -1);
			Size = 0;
		}

		if (Bar)
		{
			Bar->SetCtrlValue(IDC_OFFSET, 0);
		}

		Invalidate();
		DoInfo();
	}

	return Status;
}

bool GHexView::SaveFile(char *FileName)
{
	bool Status = false;

	if (File &&
		FileName)
	{
		if (stricmp(FileName, File->GetName()) == 0)
		{
			if (File->Seek(BufPos, SEEK_SET) == BufPos)
			{
				int Len = min(BufLen, Size - BufPos);
				Status = File->Write(Buf, Len) == Len;
			}
		}
	}

	return Status;
}

void GHexView::Copy(bool AsHex)
{
	if (HasSelection())
	{
		GClipBoard Cb(this);
		int64 Min = min(Select, Cursor);
		int64 Max = max(Select, Cursor);
		int64 Len = Max - Min + 1;

		// Is the selection text only?
		bool NonText = !AsHex;
		int64 Block = 4 << 10;
		GStringPipe All;
		for (int64 i=0; i<Len; i+=Block)
		{
			int64 AbsPos = Min + i;
			int64 Bytes = min(Block, Len - i);
			if (GetData(AbsPos, Bytes))
			{
				uchar *p = Buf + (AbsPos - BufPos);
				if (AsHex)
				{
					for (int n=0; n<Bytes; n++)
						All.Print("%02.2x ", (uint8)p[n]);
				}
				else
				{
					All.Write(p, Bytes);
					for (int n=0; n<Bytes; n++)
					{
						if (p[n] < ' ' && !strchr("\b\t\r\n", p[n]))
						{
							NonText = true;
							break;
						}
					}
				}
			}
		}

		if (NonText)
		{
			// Copy as binary
			int64 Len = All.GetSize();
			GAutoPtr<void> Buf(All.New());
			Cb.Binary(CF_PRIVATEFIRST, (uchar*)Buf.Get(), Len, true);			
		}
		else
		{
			// Copy as text...

			// What charset is it in? (default no conversion)
			// FIXME
			GAutoString a(All.NewStr());
			Cb.Text(a);
		}
	}
}

void GHexView::Paste()
{
	GClipBoard Cb(this);
	GArray<GClipBoard::FormatType> Formats;
	if (!Cb.EnumFormats(Formats))
		return;

	for (int i=0; i<Formats.Length(); i++)
	{
		switch (Formats[i])
		{
			case CF_PRIVATEFIRST:
			{
				uint8 *Ptr = 0;
				int Len = 0;
				if (Cb.Binary(CF_PRIVATEFIRST, &Ptr, &Len))
				{
					Paste(Ptr, Len);
				}
				break;
			}
			case CF_TEXT:
			{
				GAutoString a(Cb.Text());
				if (a)
				{
					Paste(a, strlen(a));
				}
				break;
			}
		}
	}
}

void GHexView::Paste(void *Ptr, int Len)
{
	int Block = 4 << 10;
	for (int i=0; i<Len; i+= Block)
	{
		int64 m = min(Len - i, Block);
		if (GetData(Cursor + i, m))
		{
			uchar *p = Buf + Cursor + i - BufPos;
			LgiAssert(p >= Buf && p + m < Buf + BufLen);
			memcpy(p, (uchar*)Ptr + i, m);
		}
	}

	App->SetDirty(true);
	Invalidate();
	DoInfo();
}

void GHexView::SelectAll()
{
	Select = 0;
	SelectNibble = 0;
	SetCursor(File->GetSize()-1, 1, true);
}

void GHexView::SaveSelection(char *FileName)
{
	if (File && FileName)
	{
		GFile f;
		if (HasSelection() &&
			f.Open(FileName, O_WRITE))
		{
			int64 Min = min(Select, Cursor);
			int64 Max = max(Select, Cursor);
			int64 Len = Max - Min + 1;

			f.SetSize(Len);
			f.SetPos(0);

			int64 Block = 4 << 10;
			for (int64 i=0; i<Len; i+=Block)
			{
				int64 AbsPos = Min + i;
				int64 Bytes = min(Block, Len - i);
				if (GetData(AbsPos, Bytes))
				{
					uchar *p = Buf + (AbsPos - BufPos);
					f.Write(p, Bytes);
				}
			}									
		}
	}
}

void GHexView::SelectionFillRandom(GStream *Rnd)
{
	if (!Rnd || !File)
		return;

	int64 Min = min(Select, Cursor);
	int64 Max = max(Select, Cursor);
	int64 Len = Max - Min + 1;

	File->SetPos(Min);

	{
		int64 Last = LgiCurrentTime();
		int64 Start = Last;
		GProgressDlg Dlg(0, true);
		GArray<char> Buf;
		Buf.Length(2 << 20);
		Dlg.SetLimits(0, Len);
		Dlg.SetScale(1.0 / 1024.0 / 1024.0);
		Dlg.SetType("MB");

		#if 1
		if (Rnd->Read(&Buf[0], Buf.Length()) != Buf.Length())
		{
			LgiMsg(this, "Random stream failed.", AppName);
			return;
		}
		#endif

		for (int64 i=0; !Dlg.Cancel() && i<Len; i+=Buf.Length())
		{
			int64 Remain = min(Buf.Length(), Len-i);

			#if 0
			if (Rnd->Read(&Buf[0], Remain) != Remain)
			{
				LgiMsg(this, "Random stream failed.", AppName);
				return;
			}
			#endif

			int w = File->Write(&Buf[0], Remain);
			if (w != Remain)
			{
				LgiMsg(this, "Write file failed.", AppName);
				break;
			}

			int64 Now = LgiCurrentTime();
			if (Now - Last > 500)
			{
				Dlg.Value(i);
				LgiYield();
				Last = Now;

				double Sec = (double)(int64)(Now - Start) / 1000.0;
				double Rate = (double)(int64)(i + Remain) / Sec;
				int TotalSeconds = (Len - i - Remain) / Rate;
				char s[64];
				sprintf(s, "%i:%02.2i:%02.2i remaining", TotalSeconds/3600, (TotalSeconds%3600)/60, TotalSeconds%60);
				Dlg.SetDescription(s);
			}
		}
	}

	if (File->SetPos(BufPos) == BufPos)
	{
		BufUsed = File->Read(Buf, BufLen);
	}
	Invalidate();
}

bool GHexView::Pour(GRegion &r)
{
	GRect *Best = FindLargest(r);
	if (Best)
	{
		SetPos(*Best, true);
		return true;
	}
	return false;
}

void GHexView::OnPosChange()
{
	SetScroll();
	GLayout::OnPosChange();
}

void GHexView::OnPulse()
{
	Flash = !Flash;

	if (CursorText.Valid())
	{
		GRect a = CursorText;
		GRect b = CursorHex;
		a.Union(&b);
		a.y1 = a.y2 - Font->GetHeight() + 1;

		Invalidate(&a);
	}
}

void GHexView::OnPaint(GSurface *pDC)
{
	GRect r = GetClient();

	int i;
	char s[256];
	COLOUR Fore[256];
	COLOUR Back[256];
	int CurrentY = r.y1;
	uint64 YPos = VScroll ? VScroll->Value() : 0;
	int64 Start = YPos << 4;
	int Lines = GetClient().Y() / LineY;
	int64 End = min(Size-1, Start + (Lines * 16));
	Font->Transparent(false);
	CursorHex.ZOff(-1, -1);
	CursorText.ZOff(-1, -1);

	if (GetData(Start, End-Start))
	{
		for (int l=0; l<Lines; l++, CurrentY += LineY)
		{
			char *p = s;
			int64 LineStart = Start + (l * 16);
			if (LineStart >= Size) break;
			int Cx1; // Screen x for cursor in the HEX view
			int Cx2; // Screen x for cursor in the ASCII view
			bool IsCursor = ((Cursor >= LineStart) && (Cursor < LineStart + 16));

			// Clear the colours for this line
			for (i=0; i<CountOf(Back); i++)
			{
				Fore[i] = LC_TEXT;
				Back[i] = LC_WORKSPACE;
			}

			// Create line of text
			if (IsHex)
			{
				p += sprintf(p, "%02.2x:%08.8X  ", (uint)(LineStart >> 32), (uint)LineStart);
			}
			else
			{
				#ifdef WIN32
				p += sprintf(p, "%11.11I64i  ", LineStart);
				#else
				p += sprintf(p, "%11.11li  ", LineStart);
				#endif
			}			
			if (IsCursor)
			{
				// Work out the x position on screen for the cursor in the hex section
				int Off = ((Cursor-LineStart)*3) + Nibble;
				GDisplayString ds(Font, s);
				Cx1 = ds.X() + (CharX * Off);
			}

			// Print the hex bytes to the line
			int64 n;
			for (n=LineStart; n<LineStart+16; n++, p+=3)
			{
				if (n < Size)
				{
					sprintf(p, "%02.2X ", Buf[n-BufPos]);

					if (CompareMap && CompareMap[n-BufPos])
					{
						Fore[p - s] = Rgb24(255, 0, 0);
						Fore[p - s + 1] = Rgb24(255, 0, 0);
						Fore[p - s + 2] = Rgb24(255, 0, 0);
					}
				}
				else
				{
					strcat(p, "   ");
				}
			}

			// Separator between hex/ascii
			strcat(s, "  ");
			p += 2;

			if (IsCursor)
			{
				// Work out cursor location in the ASCII view
				int Off = Cursor-LineStart;
				GDisplayString ds(Font, s);
				Cx2 = ds.X() + (CharX * Off);
			}

			// Print the ascii characters to the line
			for (n=LineStart; n<Size && n<LineStart+16; n++)
			{
				uchar c = Buf[n-BufPos];
				if (CompareMap && CompareMap[n-BufPos])
				{
					Fore[p - s] = Rgb24(255, 0, 0);
				}
				*p++ = (c >= ' ' && c < 0x7f) ? c : '.';
			}
			*p++ = 0;

			// Draw text
			GRect Tr(0, CurrentY, r.X()-1, CurrentY+LineY-1);

			Font->Colour(LC_TEXT, LC_WORKSPACE);
			char16 *Wide = (char16*)LgiNewConvertCp(LGI_WideCharset, s, "iso-8859-1");
			if (Wide)
			{
				// Paint the selection into the colour buffers
				int64 Min = Select >= 0 ? min(Select, Cursor) : -1;
				int64 Max = Select >= 0 ? max(Select, Cursor) : -1;
				if (Min < LineStart + 16 &&
					Max >= LineStart)
				{
					// Part or all of this line is selected
					int64 s = ((Select - LineStart) * 3) + SelectNibble;
					int64 e = ((Cursor - LineStart) * 3) + Nibble;
					if (s > e)
					{
						int64 i = s;
						s = e;
						e = i;
					}
					if (s < 0) s = 0;
					if (e > 16 * 3 - 2) e = 16 * 3 - 2;

					for (i=s+HEX_COLUMN; i<=e+HEX_COLUMN; i++)
					{
						Fore[i] = ColourSelectionFore;
						Back[i] = ColourSelectionBack;
					}
					for (i=(s/3)+TEXT_COLUMN; i<=(e/3)+TEXT_COLUMN; i++)
					{
						Fore[i] = ColourSelectionFore;
						Back[i] = ColourSelectionBack;
					}
				}

				// Colour the back of the cursor grey...
				if (Cursor >= LineStart && Cursor < LineStart+16)
				{
					if (Select < 0 && Flash)
					{
						Back[HEX_COLUMN+((Cursor-LineStart)*3)+Nibble] = CursorColourBack;
						Back[TEXT_COLUMN+Cursor-LineStart] = CursorColourBack;
					}
				}

				// Go through the colour buffers, painting in runs of similar colour
				GRect r;
				int Cx = 0;
				int Len = p - s;
				for (i=0; i<Len; )
				{
					int e = i;
					while (e < Len)
					{
						if (Fore[e] != Fore[i] ||
							Back[e] != Back[i])
							break;
						e++;
					}

					int Run = e - i;
					GDisplayString Str(Font, s + i, Run);
					if (e >= Len)
						r.Set(Cx, CurrentY, X()-1, CurrentY+Str.Y()-1);
					else
						r.Set(Cx, CurrentY, Cx+Str.X()-1, CurrentY+Str.Y()-1);
					Font->Colour(Fore[i], Back[i]);
					Str.Draw(pDC, Cx, CurrentY, &r);
					Cx += Str.X();
					i = e;
				}
				
				DeleteArray(Wide);
			}

			// Draw cursor
			if (IsCursor)
			{
				pDC->Colour(Focus() ? LC_TEXT : LC_LOW, 24);
				int Cy = CurrentY+LineY-1;

				// hex cursor
				CursorHex.Set(Cx1, Cy - (Pane == HexPane ? 1 : 0), Cx1+CharX, Cy);
				pDC->Rectangle(&CursorHex);

				// ascii cursor
				CursorText.Set(Cx2, Cy - (Pane == AsciiPane ? 1 : 0), Cx2+CharX, Cy);
				pDC->Rectangle(&CursorText);
			}
		}
		
		r.y1 = CurrentY;
		if (r.Valid())
		{
			pDC->Colour(LC_WORKSPACE, 24);
			pDC->Rectangle(&r);
		}
	}
	else
	{
		pDC->Colour(LC_WORKSPACE, 24);
		pDC->Rectangle();
		
		if (File)
		{
			Font->Colour(LC_TEXT, LC_WORKSPACE);
			GDisplayString ds(Font, "Couldn't read from file.");
			ds.Draw(pDC, 5, 5);
		}
	}

}

/* Old paint code:

				if (Min < LineStart + 16 &&
					Max >= LineStart)
				{
					// Part or all of this line is selected

					int64 s = ((Select - LineStart) * 3) + SelectNibble;
					int64 e = ((Cursor - LineStart) * 3) + Nibble;
					if (s > e)
					{
						int64 i = s;
						s = e;
						e = i;
					}
					if (s < 0) s = 0;
					if (e > 16 * 3 - 2) e = 16 * 3 - 2;

					GArray<int> Border;
					Border[Border.Length()] = s + HEX_COLUMN;
					Border[Border.Length()] = e + HEX_COLUMN + 1;
					Border[Border.Length()] = (s / 3) + TEXT_COLUMN;
					Border[Border.Length()] = (e / 3) + TEXT_COLUMN + 1;
					
					bool Sel = false;
					int Next = 0;
					int Cx = 0;

					for (char *S = Utf; S && *S; )
					{
						char *e = Next < Border.Length() ? LgiSeekUtf8(Utf, Border[Next++]) : S + strlen(S);
						GDisplayString Str(Font, S, e - S);

						if (Sel)
						{
							Font->Colour(Rgb24(255, 255, 0), Rgb24(0, 0, 255));
						}
						else
						{
							Font->Colour(LC_TEXT, LC_WORKSPACE);
						}
						GRect r;
						r.ZOff(Str.X()-1, Str.Y()-1);
						r.Offset(Cx, CurrentY);
						
						Str.Draw(pDC, Cx, CurrentY, &r);
						
						Cx += Str.X();
						S = e;
						Sel = !Sel;
					}
					
					pDC->Colour(LC_WORKSPACE, 24);
					pDC->Rectangle(Cx, CurrentY, X()-1, CurrentY+LineY-1);
				}
				else
				{
					// No selection on this line
					Font->Text(pDC, 0, CurrentY, Utf, -1, &Tr);
				}
*/

void GHexView::OnMouseWheel(double Lines)
{
	if (VScroll)
	{
		VScroll->Value(VScroll->Value() + Lines);
		Invalidate();
	}
}

bool GHexView::GetCursorFromLoc(int x, int y, int64 &Cur, int &Nib)
{
	uint64 Start = ((uint64)(VScroll ? VScroll->Value() : 0)) * 16;
	GRect c = GetClient();

	int _x1 = (((x - c.x1) / CharX) - HEX_COLUMN);
	int _x2 = (((x - c.x1) / CharX) - TEXT_COLUMN);
	int cx = _x1 / 3;
	int n = _x1 % 3 > 0;
	int cy = (y - c.y1) / LineY;

	if (cx >= 0 && cx < 16)
	{
		Cur = Start + (cy * 16) + cx;
		Nib = n;
		Pane = HexPane;
		return true;
	}
	else if (_x2 >= 0 && _x2 < 16)
	{
		Cur = Start + (cy * 16) + _x2;
		Nib = 0;
		Pane = AsciiPane;
		return true;
	}

	return false;
}

void GHexView::OnMouseClick(GMouse &m)
{
	Capture(m.Down());
	if (m.Down())
	{
		Focus(true);

		if (m.Left())
		{
			int64 Cur;
			int Nib;
			if (GetCursorFromLoc(m.x, m.y, Cur, Nib))
			{
				SetCursor(Cur, Nib, m.Shift());
			}
		}
	}
}

void GHexView::OnMouseMove(GMouse &m)
{
	if (IsCapturing())
	{
		int64 Cur;
		int Nib;
		if (GetCursorFromLoc(m.x, m.y, Cur, Nib))
		{
			SetCursor(Cur, Nib, true);
		}
	}
}

void GHexView::OnFocus(bool f)
{
	Invalidate();
}

bool GHexView::OnKey(GKey &k)
{
	int Lines = GetClient().Y() / LineY;

	switch (k.vkey)
	{
		default:
		{
			if (k.IsChar && !IsReadOnly)
			{
				if (k.Down())
				{
					if (Pane == HexPane)
					{
						int c = -1;
						if (k.c16 >= '0' && k.c16 <= '9')		c = k.c16 - '0';
						else if (k.c16 >= 'a' && k.c16 <= 'f')	c = k.c16 - 'a' + 10;
						else if (k.c16 >= 'A' && k.c16 <= 'F')	c = k.c16 - 'A' + 10;

						if (c >= 0 && c < 16)
						{
							uchar *Byte = Buf + (Cursor - BufPos);
							if (Nibble)
							{
								*Byte = (*Byte & 0xf0) | c;
							}
							else
							{
								*Byte = (c << 4) | (*Byte & 0xf);
							}

							App->SetDirty(true);

							if (Nibble == 0)
							{
								SetCursor(Cursor, 1);
							}
							else if (Cursor < Size - 1)
							{
								SetCursor(Cursor+1, 0);
							}
						}
					}
					else if (Pane == AsciiPane)
					{
						uchar *Byte = Buf + (Cursor - BufPos);

						*Byte =  k.c16;

						App->SetDirty(true);
						
						SetCursor(Cursor + 1);
					}
				}

				return true;
			}			
			break;
		}
		case VK_RIGHT:
		{
			if (k.Down())
			{
				if (Pane == HexPane)
				{
					if (Nibble == 0)
					{
						SetCursor(Cursor, 1, k.Shift());
					}
					else if (Cursor < Size - 1)
					{
						SetCursor(Cursor+1, 0, k.Shift());
					}
				}
				else
				{
					SetCursor(Cursor+1, 0);
				}
			}
			return true;
			break;
		}
		case VK_LEFT:
		{
			if (k.Down())
			{
				if (Pane == HexPane)
				{
					if (Nibble == 1)
					{
						SetCursor(Cursor, 0, k.Shift());
					}
					else if (Cursor > 0)
					{
						SetCursor(Cursor-1, 1, k.Shift());
					}
				}
				else
				{
					SetCursor(Cursor-1, 0);
				}
			}
			return true;
			break;
		}
		case VK_UP:
		{
			if (k.Down())
			{
				SetCursor(Cursor-16, Nibble, k.Shift());
			}
			return true;
			break;
		}
		case VK_DOWN:
		{
			if (k.Down())
			{
				if (k.Ctrl() && CompareMap)
				{
					// Find next difference
					// int64 p = Cursor - BufPos + 1;
					int Block = 16 << 10;

					bool Done = false;
					for (int64 n = Cursor - BufPos + 1; !Done && n < Size; n += Block)
					{
						if (GetData(n, Block))
						{
							int Off = n - BufPos;
							int Len = BufUsed - Off;
							if (Len > Block) Len = Block;

							for (int i=0; i<Len; i++)
							{
								if (CompareMap[Off + i])
								{
									SetCursor(BufPos + Off + i, 0);
									Done = true;
									break;
								}
							}
						}
						else break;
					}

					if (!Done)
						LgiMsg(this, "No differences.", AppName);
				}
				else
				{
					// Down
					SetCursor(Cursor+16, Nibble, k.Shift());
				}
			}
			return true;
			break;
		}
		case VK_PAGEUP:
		{
			if (k.Down())
			{
				if (k.Ctrl())
				{
					SetCursor(Cursor - (Lines * 16 * 16), Nibble, k.Shift());
				}
				else
				{
					SetCursor(Cursor - (Lines * 16), Nibble, k.Shift());
				}
			}
			return true;
			break;
		}
		case VK_PAGEDOWN:
		{
			if (k.Down())
			{
				if (k.Ctrl())
				{
					SetCursor(Cursor + (Lines * 16 * 16), Nibble, k.Shift());
				}
				else
				{
					SetCursor(Cursor + (Lines * 16), Nibble, k.Shift());
				}
			}
			return true;
			break;
		}
		case VK_HOME:
		{
			if (k.Down())
			{
				if (k.Ctrl())
				{
					SetCursor(0, 0, k.Shift());
				}
				else
				{
					SetCursor(Cursor - (Cursor%16), 0, k.Shift());
				}
			}
			return true;
			break;
		}
		case VK_END:
		{
			if (k.Down())
			{
				if (k.Ctrl())
				{
					SetCursor(Size-1, 1, k.Shift());
				}
				else
				{
					SetCursor(Cursor - (Cursor%16) + 15, 1, k.Shift());
				}
			}
			return true;
			break;
		}
		case VK_BACKSPACE:
		{
			if (k.Down())
			{
				if (Pane == HexPane)
				{
					if (Nibble == 0)
					{
						SetCursor(Cursor-1, 1);
					}
					else
					{
						SetCursor(Cursor, 0);
					}
				}
				else
				{
					SetCursor(Cursor - 1);
				}
			}
			return true;
			break;
		}
		case '\t':
		{
			if (k.Down())
			{
				if (k.IsChar)
				{
					if (Pane == HexPane)
					{
						Pane = AsciiPane;
					}
					else
					{
						Pane = HexPane;
					}

					Invalidate();
				}
			}
			return true;
			break;
		}
	}

	return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////
AppWnd::AppWnd() : GDocApp<GOptionsFile>(AppName, "MAIN")
{
	/*
	if (Options = new GOptionsFile("iHexOptions"))
	{
		Options->Serialize(false);
	}
	*/

	Tools = 0;
	Status = 0;
	Active = false;
	Doc = 0;
	Search = 0;
	Bar = 0;
	Split = 0;
	Visual = 0;
	TextView = 0;

	if (_Create())
	{
		DropTarget(true);
		if (_LoadMenu("IDM_MENU", 
			#ifdef _DEBUG
			"Debug"
			#else
			"Release"
			#endif
			))
		{
			if (_FileMenu)
			{
				CmdSave.MenuItem = Menu->FindItem(IDM_SAVE);
				CmdSaveAs.MenuItem = Menu->FindItem(IDM_SAVEAS);
				CmdClose.MenuItem = Menu->FindItem(IDM_CLOSE);
				_FileMenu->AppendSeparator(4);
				CmdChangeSize.MenuItem = _FileMenu->AppendItem("Change Size", IDM_CHANGE_FILE_SIZE, true, 5);
				CmdCompare.MenuItem = _FileMenu->AppendItem("Compare With File", IDM_COMPARE, true, 6);
			}

			CmdFind.MenuItem = Menu->FindItem(IDM_FIND);
			CmdNext.MenuItem = Menu->FindItem(IDM_NEXT);
			CmdPaste.MenuItem = Menu->FindItem(IDM_PASTE);
			CmdCopy.MenuItem = Menu->FindItem(IDM_COPY);
			CmdCopyHex.MenuItem = Menu->FindItem(IDM_COPY_HEX);
			CmdSelectAll.MenuItem = Menu->FindItem(IDM_SELECT_ALL);

			/*
			GSubMenu *Edit = Menu->AppendSub("&Edit");
			if (Edit)
			{
				CmdCopy.MenuItem = Edit->AppendItem("Copy As Binary\tCtrl+C", IDM_COPY);
				Edit->AppendItem("Copy As Hex\tShift+Ctrl+C", IDM_COPY_HEX);
				CmdPaste.MenuItem = Edit->AppendItem("Paste\tCtrl+V", IDM_PASTE);
				Edit->AppendSeparator();
				CmdSelectAll.MenuItem = Edit->AppendItem("Select All\tCtrl+A", IDM_SELECT_ALL);
				Edit->AppendSeparator();
				CmdFind.MenuItem = Edit->AppendItem("Find\tCtrl+F", IDM_FIND, false, 7);
				Edit->AppendItem("Find In Files\tShift+Ctrl+F", IDM_FIND_IN_FILES, true);
				CmdNext.MenuItem = Edit->AppendItem("Next\tF3", IDM_NEXT, false, 8);
			}

			GSubMenu *Tools = Menu->AppendSub("&Tools");
			if (Tools)
			{
				Tools->AppendItem("Save To File", IDM_SAVE_SELECTION, true);
				Tools->AppendItem("Fill With Random Bytes", IDM_RND_SELECTION, true);
				Tools->AppendItem("Combine Files", IDM_COMBINE_FILES, true);
			}

			GSubMenu *Help = Menu->AppendSub("&Help");
			if (Help)
			{
				Help->AppendItem("&Help", IDM_HELP, true);
				Help->AppendSeparator();
				Help->AppendItem("&About", IDM_ABOUT, true);
			}
			*/
		}
		OnDocument(false);

		Tools = LgiLoadToolbar(this, "Tools.gif", 24, 24);
		if (Tools)
		{
			Tools->TextLabels(true);
			Tools->Attach(this);
			Tools->AppendButton("Open", IDM_OPEN);
			CmdSave.ToolButton = Tools->AppendButton("Save", IDM_SAVE, TBT_PUSH, false);
			// CmdSaveAs.ToolButton = Tools->AppendButton("Save As", IDM_SAVEAS, TBT_PUSH, false);
			CmdFind.ToolButton = Tools->AppendButton("Search", IDM_FIND, TBT_PUSH, false, 3);
			Tools->AppendSeparator();
			CmdVisualise.ToolButton = Tools->AppendButton("Visualise", IDM_VISUALISE, TBT_TOGGLE, false, 4);
			CmdText.ToolButton = Tools->AppendButton("Text", IDM_TEXTVIEW, TBT_TOGGLE, false, 5);
		}

		Pour();
		Bar = new IHexBar(this, Tools ? Tools->Y() : 20);

		Status = new GStatusBar;
		if (Status)
		{
			StatusInfo[0] = Status->AppendPane("", -1);
			StatusInfo[1] = Status->AppendPane("", 200);
			if (StatusInfo[1]) StatusInfo[1]->Sunken(true);
			Status->Attach(this);
		}

		Doc = new GHexView(this, Bar);
		if (Bar)
		{
			Bar->View = Doc;
		}

		OnDirty(false);
		
		#ifdef LINUX
		char *f = LgiFindFile("icon-32x32.png");
		if (f) Handle()->setIcon(f);
		DeleteArray(f);
		#endif
		
		Visible(true);
		Pour();
	}
}

AppWnd::~AppWnd()
{
	DeleteObj(Search);
	LgiApp->AppWnd = 0;
	DeleteObj(Bar);
	_Destroy();
}

bool AppWnd::OnRequestClose(bool OsShuttingDown)
{
	if (!Active)
	{
		return GWindow::OnRequestClose(OsShuttingDown);
	}

	return false;
}

void AppWnd::OnDirty(bool NewValue)
{
	CmdSave.Enabled(NewValue);
	CmdSaveAs.Enabled(NewValue);
	
	//CmdClose.Enabled(Doc && Doc->HasFile());
	CmdChangeSize.Enabled(Doc && Doc->HasFile());
	CmdCompare.Enabled(Doc && Doc->HasFile());
}

bool AppWnd::OnKey(GKey &k)
{
	return false;
}

void AppWnd::OnPosChange()
{
	GDocApp<GOptionsFile>::OnPosChange();
}

int AppWnd::OnNotify(GViewI *Ctrl, int Flags)
{
	switch (Ctrl->GetId())
	{
		case IDC_HEX_VIEW:
		{
			if (Flags == GTVN_CURSOR_CHANGED)
			{
				char *Data;
				int Len;
				if (Visual && Doc->GetDataAtCursor(Data, Len))
				{
					LgiTrace("Len=%i\n", Len);
					Visual->Visualise(Data, Len, GetCtrlValue(IDC_LITTLE) );
				}
				if (TextView && Doc->GetDataAtCursor(Data, Len))
				{
					GStringPipe p(1024);
					for (char *s = Data; s < Data + Len && s < Data + (4 << 10) && *s; s++)
					{
						if (*s >= ' ' || *s == '\n' || *s == '\t')
						{
							p.Push(s, 1);
						}
					}
					char *t = p.NewStr();
					if (t)
					{
						TextView->Name(t);
						DeleteArray(t);
					}
				}
			}
			break;
		}
	}
	return GDocApp<GOptionsFile>::OnNotify(Ctrl, Flags);
}

void AppWnd::OnPulse()
{
}

int AppWnd::OnEvent(GMessage *Msg)
{
	return GDocApp<GOptionsFile>::OnEvent(Msg);
}

void AppWnd::OnPaint(GSurface *pDC)
{
	pDC->Colour(LC_MED, 24);
	pDC->Rectangle();
}

#define SPLIT_X		590

void AppWnd::ToggleVisualise()
{
	if (GetCtrlValue(IDM_VISUALISE))
	{
		if (!Split)
			Split = new GSplitter;

		if (Split)
		{
			Doc->Detach();
			Split->Value(SPLIT_X);
			Split->Border(false);
			Split->Raised(false);
			Split->Attach(this);
			Split->SetViewA(Doc, false);
			Split->SetViewB(Visual = new GVisualiseView(this), false);
		}
	}
	else
	{
		Doc->Detach();
		DeleteObj(Split);
		Doc->Attach(this);
		Visual = 0;
	}

	Pour();
}

void AppWnd::ToggleTextView()
{
	if (GetCtrlValue(IDM_TEXTVIEW))
	{
		SetCtrlValue(IDM_VISUALISE, false);

		if (!Split)
			Split = new GSplitter;

		if (Split)
		{
			Doc->Detach();
			Split->Value(SPLIT_X);
			Split->Border(false);
			Split->Raised(false);
			Split->Attach(this);
			Split->SetViewA(Doc, false);
			Split->SetViewB(TextView = new GTextView3(100, 0, 0, 100, 100, 0), false);
		}
	}
	else
	{
		Doc->Detach();
		DeleteObj(Split);
		Doc->Attach(this);
		TextView = 0;
	}

	Pour();
}

int Cmp(char **a, char **b)
{
	return stricmp(*a, *b);
}

int AppWnd::OnCommand(int Cmd, int Event, OsView Wnd)
{
	switch (Cmd)
	{
		case IDM_COPY:
		{
			if (Doc)
				Doc->Copy();
			break;
		}
		case IDM_COPY_HEX:
		{
			if (Doc)
				Doc->Copy(true);
			break;
		}
		case IDM_PASTE:
		{
			if (Doc)
				Doc->Paste();
			break;
		}
		case IDM_SELECT_ALL:
		{
			if (Doc)
				Doc->SelectAll();
			break;
		}

		case IDM_COMBINE_FILES:
		{
			GFileSelect s;
			s.Parent(this);
			s.MultiSelect(true);
			if (s.Open())
			{
				int64 Size = 0;
				int i;
				for (i=0; i<s.Length(); i++)
				{
					char *f = s[i];
					Size += LgiFileSize(f);
				}
				
				GFileSelect o;
				if (o.Save())
				{
					GFile Out;
					if (Out.Open(o.Name(), O_WRITE))
					{
						GProgressDlg Dlg(this);
						Dlg.SetLimits(0, Size);
						Dlg.SetType("MB");
						Dlg.SetScale(1.0/1024.0/1024.0);
						GArray<char> Buf;
						Buf.Length(1 << 20);
						Out.SetSize(0);
						GArray<char*> Files;
						for (i=0; i<s.Length(); i++)
							Files[i] = s[i];
						
						Files.Sort(Cmp);

						for (i=0; i<Files.Length(); i++)
						{
							GFile In;
							if (In.Open(Files[i], O_READ))
							{
								printf("Appending %s\n", Files[i]);
								char *d = strrchr(Files[i], DIR_CHAR);
								if (d) Dlg.SetDescription(d+1);
								
								int64 Fs = In.GetSize();
								for (int64 p = 0; p<Fs; )
								{
									int r = In.Read(&Buf[0], Buf.Length());
									if (r > 0)
									{
										int w = Out.Write(&Buf[0], r);
										if (w != r)
										{
											printf("%s:%i - Write error...!\n", _FL);
											break;
										}
										
										p += w;
										Dlg.Value(Dlg.Value() + w);
										LgiYield();
									}
									else break;
								}
							}
							else printf("%s:%i - Can't open %s\n", _FL, Files[i]);
						}
					}
					else printf("%s:%i - Can't open %s\n", _FL, o.Name());
				}
			}
			break;
		}
		case IDM_VISUALISE:
		{
			if (GetCtrlValue(IDM_TEXTVIEW))
			{
				SetCtrlValue(IDM_TEXTVIEW, false);
				ToggleTextView();
			}
			ToggleVisualise();
			OnNotify(Doc, GTVN_CURSOR_CHANGED);
			break;
		}
		case IDM_TEXTVIEW:
		{
			if (GetCtrlValue(IDM_VISUALISE))
			{
				SetCtrlValue(IDM_VISUALISE, false);
				ToggleVisualise();
			}
			ToggleTextView();
			OnNotify(Doc, GTVN_CURSOR_CHANGED);
			break;
		}
		case IDM_SAVE:
		{
			_SaveFile(GetCurFile());
			OnDirty(GetDirty());
			break;
		}
		case IDM_EXIT:
		{
			if (Doc)
			{
				Doc->OpenFile(0, false);
			}

			LgiCloseApp();
			break;
		}
		case IDM_CLOSE:
		{
			if (Doc)
			{
				Doc->OpenFile(0, false);
				OnDocument(false);
				OnDirty(GetDirty());
			}
			break;
		}
		case IDM_SAVE_SELECTION:
		{
			if (Doc)
			{
				GFileSelect s;
				s.Parent(this);
				if (s.Save())
				{
					Doc->SaveSelection(s.Name());
				}
			}
			break;
		}
		case IDM_RND_SELECTION:
		{
			if (!Doc)
				break;

			int Status = 0;
			RandomData Rnd;
			Doc->SelectionFillRandom(&Rnd);
			break;
		}
		case IDM_FIND:
		{
			if (Doc)
			{
				DeleteObj(Search);
				Search = new SearchDlg(this);
				if (Search && Search->DoModal() == IDOK)
				{
					Doc->DoSearch(Search);
				}
			}
			break;
		}
		case IDM_FIND_IN_FILES:
		{
			FindInFiles ff(this, GetOptions());
			if (ff.DoModal())
			{
				new FindInFilesResults(this, GetOptions());
			}
			break;
		}
		case IDM_NEXT:
		{
			if (Doc && Search)
			{
				Doc->DoSearch(Search);
			}
			break;
		}
		case IDM_COMPARE:
		{
			if (Doc && Doc->HasFile())
			{
				GFileSelect s;
				s.Parent(this);
				if (s.Open())
				{
					Doc->CompareFile(s.Name());
				}
			}
			break;
		}
		case IDM_CHANGE_FILE_SIZE:
		{
			if (Doc && Doc->HasFile())
			{
				ChangeSizeDlg Dlg(this, Doc->GetFileSize());
				if (Dlg.DoModal())
				{
					Doc->SetFileSize(Dlg.Size);
				}
			}
			break;
		}
		case IDM_TEST_SEARCH:
		{
			GList Tmp(100, 0, 0, 100, 100);
			FindUtils Utils(&Tmp);
			int Len = 3 << 20;
			uint8 *Mem = new uint8[Len];
			if (Mem)
			{
				GMemStream MemStr(Mem, Len, false);
				GArray<uint8> Needle;
				Needle.Add((uint8*)"STRING", 6);

				int Start = (2 << 20) - 20;
				int End = (2 << 20) + 20;
				for (int i=Start; i<End; i++)
				{
					MemStr.SetPos(0);
					memset(Mem, 0, Len);
					memcpy(Mem + i, &Needle[0], Needle.Length());

					if (Utils.Search("c:\\path.txt", MemStr, Needle, false))
					{
						// Check the string was found correctly
						List<ResultItem> r;
						if (Tmp.GetAll(r))
						{
							LgiAssert(r.Length() == 1); // There is only 1 result.
							ResultItem *ri = r.First();
							LgiAssert(ri);
							LgiAssert(ri->Pos == i);
						}
						else LgiAssert(!"ResultItem not found.");
					}
					else LgiAssert(!"Search str not found.");

					Tmp.Empty();
				}

				DeleteArray(Mem);
			}
			break;
		}
		case IDM_HELP:
		{
			Help("index.html");
			break;
		}
		case IDM_ABOUT:
		{
			GAbout Dlg(	this,
						AppName,
						APP_VER,
						"\nSimple Hex Viewer",
						"_about.gif",
						"http://www.memecode.com/ihex.php",
						"fret@memecode.com");
			break;
		}
	}
	
	return GDocApp<GOptionsFile>::OnCommand(Cmd, Event, Wnd);
}

void AppWnd::OnResult(char *Path, uint64 Offset)
{
	if (OpenFile(Path, false))
	{
		Doc->SetCursor(Offset);
	}
}

void AppWnd::Help(char *File)
{
	char e[300];
	if (File && LgiGetExePath(e, sizeof(e)))
	{
		#ifdef WIN32
		if (stristr(e, "\\Release") || stristr(e, "\\Debug"))
			LgiTrimDir(e);
		#endif
		LgiMakePath(e, sizeof(e), e, "Help");
		LgiMakePath(e, sizeof(e), e, File);
		if (FileExists(e))
		{
			LgiExecute(e);
		}
		else
		{
			LgiMsg(this, "The help file '%s' doesn't exist.", AppName, MB_OK, e);
		}
	}
}

void AppWnd::SetStatus(int Pos, char *Text)
{
	if (Pos >= 0 && Pos < 3 && StatusInfo[Pos] && Text)
	{
		StatusInfo[Pos]->Name(Text);
	}
}

GRect GetClient(GView *w)
{
	#ifdef WIN32
	RECT r = {0, 0, 0, 0};
	if (w)
	{
		GetClientRect(w->Handle(), &r);
	}
	return GRect(r);
	#else
	return GRect(0, 0, (w)?w->X()-1:0, (w)?w->Y()-1:0);
	#endif
}

void AppWnd::Pour()
{
	GRect Client = GetClient();
	GDocApp<GOptionsFile>::Pour();
}

void AppWnd::OnDocument(bool Open)
{
	CmdClose.Enabled(Open);
	CmdCopy.Enabled(Open);
	CmdCopyHex.Enabled(Open);
	CmdPaste.Enabled(Open);
	CmdSelectAll.Enabled(Open);
	CmdFind.Enabled(Open);
	CmdNext.Enabled(Open);
	CmdVisualise.Enabled(Open);
	CmdText.Enabled(Open);
}

bool AppWnd::OpenFile(char *FileName, bool ReadOnly)
{
	bool Status = false;
	if (Doc)
	{
		Status = Doc->OpenFile(FileName, ReadOnly);
		OnDocument(true);
		OnDirty(GetDirty());
	}
	return Status;
}

bool AppWnd::SaveFile(char *FileName)
{
	bool Status = false;
	if (Doc)
	{
		Status = Doc->SaveFile(FileName);
	}
	return Status;
}

//////////////////////////////////////////////////////////////////
int LgiMain(OsAppArguments &AppArgs)
{
	GApp a("application/x-i.Hex", AppArgs);
	if (a.IsOk())
	{
		a.AppWnd = new AppWnd;
		a.Run();
	}

	return 0;
}
