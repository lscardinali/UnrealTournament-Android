//=============================================================================
// PadRootWindow - controller navigation for UWindow dialogs (UTAndroid spike).
//
// Install: [UTMenu.UTConsole] RootWindow=PadUI.PadRootWindow
//
// The Android SDL bridge turns the pad into keys while a menu is up:
//   D-pad = arrows, A = Enter, R1 = Tab, L1 = Space, B = Escape.
// In a dialog this window takes those keys first (HotKeyDown):
//   arrows      move a visible focus box to the nearest control in that direction
//               (Left/Right adjust a focused slider or cycle a focused combo instead)
//   Enter       activate: click a button / toggle a checkbox / open-pick a combo list
//   Tab / Space next / previous tab of the dialog's page control
//   Up past the top control of a tabbed dialog focuses its tab strip: Left/Right switch tabs, Down/Enter go back in
//   list boxes  Up/Down walk the items (leaving the list at either end), Enter = double-click the item
//   key binding (Controls tab): A on a binding waits for the next PAD button (Joy1-16 from the SDL bridge), which is
//               bound (shown as A, B, R2, D-Pad Up, ...); Start (Escape) cancels. Pad-made keys are ignored meanwhile; Joy buttons are swallowed otherwise.
//   text fields A opens an on-screen keyboard: D-pad picks a key, A types it, B backspace, X space, Y shift,
//               L1/R1 move the caret, Start (or Done) closes it.
//   server list Up/Down pick a server, A joins it, X refreshes the list (type filter grid: A toggles a type)
//   Escape      closes an open combo list before it closes the dialog
// The menu bar and its pulldowns keep their stock key handling.
//=============================================================================
class PadRootWindow extends UMenuRootWindow;



const MAXCTL = 192;

var UWindowWindow PadFocus;
var int PadFrame;        // frames drawn (a clock that keeps running while the game is paused)
var int PollStartFrame;  // when a key-binding button started waiting for its key
var int PollEndFrame;    // when that wait ended (bound or cancelled)
var bool bWasPolling;
var string PadButtonName[16];

// On-screen keyboard (text fields)
var bool bKb;
var bool bPadDebug;
var string EvLog;
var int JoyLastFrame[16];      // last frame each Joy button reported a press: the engine repeats it every frame held
var UWindowEditBox KbEdit;
var int KbRow, KbCol;
var bool bKbShift;
var int KbBFrame;              // when B (backspace) was last pressed
var int KbEscFrame;            // pending close from an Escape (Start, or the Escape half of B: the order varies)
var string KbRows[4], KbShiftRows[4];
var UWindowPageControl PadTabs; // set while the focus is on a dialog's tab strip (PadFocus is None then)
var UWindowWindow Ctl[192];
var int NumCtl;

// ---------------------------------------------------------------- helpers

function bool IsUnder(UWindowWindow W, UWindowWindow Top)
{
	while (W != None)
	{
		if (W == Top)
			return True;
		W = W.ParentWindow;
	}
	return False;
}

function bool IsShown(UWindowWindow W)
{
	while (W != None && W != Self)
	{
		if (!W.bWindowVisible)
			return False;
		W = W.ParentWindow;
	}
	return W == Self;
}

// Controls a dialog created with CreateControl (their parent is a dialog client window), plus any list box.
function bool Focusable(UWindowWindow W)
{
	if (UWindowListBox(W) != None)
		return True;
	if (UBrowserServerGrid(W) != None)
		return True; // server browser list
	if (UBrowserTypesGrid(W) != None) return True; // game-type filter grid //#UT
	if (UWindowDialogControl(W) == None || UWindowDialogClientWindow(W.ParentWindow) == None)
		return False;
	if (UWindowButton(W) != None)
		return !UWindowButton(W).bDisabled;
	return UWindowHSliderControl(W) != None || UWindowComboControl(W) != None || UWindowEditControl(W) != None;
}

// The dialog the pad works on: the active top-level window, unless that is the menu bar.
function UWindowWindow PadDialog()
{
	local UWindowWindow K;

	if (ActiveWindow == None || !ActiveWindow.bWindowVisible || ActiveWindow == MenuBar)
		return None;
	K = KeyFocusWindow;
	if (K != None && (UWindowPulldownMenu(K) != None || IsUnder(K, MenuBar)))
		return None;
	return ActiveWindow;
}

function Collect(UWindowWindow W)
{
	local UWindowWindow C;

	for (C = W.FirstChildWindow; C != None; C = C.NextSiblingWindow)
	{
		if (C.bWindowVisible)
		{
			if (Focusable(C))
			{
				if (NumCtl < MAXCTL)
					Ctl[NumCtl++] = C;
			}
			else
				Collect(C);
		}
	}
}

function GetRect(UWindowWindow W, out float X, out float Y)
{
	W.WindowToGlobal(0, 0, X, Y);
}

function float Gap(float A0, float A1, float B0, float B1)
{
	if (B0 > A1)
		return B0 - A1;
	if (A0 > B1)
		return A0 - B1;
	return 0;
}

// Nearest control in direction (DX, DY), scored on the distance along the direction plus the sideways gap.
function UWindowWindow FindDir(int DX, int DY)
{
	local int i;
	local float FX, FY, FW, FH, X, Y, Along, Side, Score, BestScore;
	local UWindowWindow C, Best;
	local UWindowScrollingDialogClient Pane;
	local int Pass;

	GetRect(PadFocus, FX, FY);
	FW = PadFocus.WinWidth;
	FH = PadFocus.WinHeight;
	// In a scrolling page, the next control down may be scrolled out of view, further away on screen than the
	// dialog's own buttons: prefer controls of the same page, then anything.
	Pane = ScrollPaneOf(PadFocus);
	for (Pass = 0; Pass < 2 && Best == None; Pass++)
	{
	if (Pass == 0 && Pane == None)
		continue;
	BestScore = 1000000;
	for (i = 0; i < NumCtl; i++)
	{
		C = Ctl[i];
		if (C == PadFocus)
			continue;
		if (Pass == 0 && !IsUnder(C, Pane))
			continue;
		GetRect(C, X, Y);
		if (DY != 0)
		{
			if (DY > 0) Along = Y - (FY + FH * 0.5);
			else        Along = (FY + FH * 0.5) - (Y + C.WinHeight);
			if (Along < -FH * 0.25 || (Y + C.WinHeight * 0.5 - FY - FH * 0.5) * DY <= 1)
				continue;
			Side = Gap(FX, FX + FW, X, X + C.WinWidth);
			Score = FMax(Along, 0) + Side * 3 + Abs(X - FX) * 0.05;
		}
		else
		{
			if (DX > 0) Along = X - (FX + FW * 0.5);
			else        Along = (FX + FW * 0.5) - (X + C.WinWidth);
			if (Along < -4) // must start past the focused control's centre
				continue;
			Side = Gap(FY, FY + FH, Y, Y + C.WinHeight);
			if (Side > FH * 2)
				continue; // left/right stays on (roughly) the same row
			Score = FMax(Along, 0) + Side * 3;
		}
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = C;
		}
	}
	}
	return Best;
}

function UWindowScrollingDialogClient ScrollPaneOf(UWindowWindow W)
{
	for (W = W.ParentWindow; W != None; W = W.ParentWindow)
		if (UWindowScrollingDialogClient(W) != None)
			return UWindowScrollingDialogClient(W);
	return None;
}

function UWindowWindow FirstControl()
{
	local int i;
	local float X, Y, BX, BY;
	local UWindowWindow Best;

	for (i = 0; i < NumCtl; i++)
	{
		GetRect(Ctl[i], X, Y);
		if (Best == None || Y < BY - 2 || (Abs(Y - BY) <= 2 && X < BX))
		{
			Best = Ctl[i];
			BX = X;
			BY = Y;
		}
	}
	return Best;
}

// Scroll an enclosing scrolling dialog so the control is on screen.
function ScrollTo(UWindowWindow C)
{
	local UWindowWindow P;
	local UWindowScrollingDialogClient S;
	local float GX, GY, LX, LY;

	for (P = C.ParentWindow; P != None; P = P.ParentWindow)
	{
		S = UWindowScrollingDialogClient(P);
		if (S != None)
		{
			if (S.bShowVertSB && S.VertSB != None)
			{
				C.WindowToGlobal(0, 0, GX, GY);
				S.ClientArea.GlobalToWindow(GX, GY, LX, LY);
				S.VertSB.Show(LY + C.WinHeight);
				S.VertSB.Show(LY);
			}
			return;
		}
	}
}

function SetPadFocus(UWindowWindow C)
{
	local UWindowListBox L;

	if (C == None)
		return;
	PadFocus = C;
	C.ActivateWindow(0, False);
	ScrollTo(C);
	// Entering a list: select its first item so Up/Down have a starting point.
	L = UWindowListBox(C);
	if (L != None && L.SelectedItem == None)
		ListSelect(L, ListStep(L, 1));
}

// Next/previous shown item of a list box (the first one when nothing is selected); None at either end.
function UWindowListBoxItem ListStep(UWindowListBox L, int Dir)
{
	local UWindowList I;

	if (L.SelectedItem == None)
		I = L.Items.Next;
	else if (Dir < 0)
		I = L.SelectedItem.Prev;
	else
		I = L.SelectedItem.Next;
	while (I != None && I != L.Items)
	{
		if (I.ShowThisItem())
			return UWindowListBoxItem(I);
		if (Dir < 0)
			I = I.Prev;
		else
			I = I.Next;
	}
	return None;
}

function ListSelect(UWindowListBox L, UWindowListBoxItem I)
{
	if (I == None)
		return;
	L.SetSelectedItem(I);
	L.MakeSelectedVisible();
}

// Keep PadFocus on a visible control of the current dialog.
function PadUpdate()
{
	local UWindowWindow D;

	D = PadDialog();
	if (D == None)
	{
		PadFocus = None;
		PadTabs = None;
		return;
	}
	if (PadTabs != None)
	{
		if (IsShown(PadTabs) && IsUnder(PadTabs, D))
			return;
		PadTabs = None;
	}
	if (PadFocus == None || !IsShown(PadFocus) || !IsUnder(PadFocus, D))
	{
		NumCtl = 0;
		Collect(D);
		PadFocus = FirstControl();
		if (PadFocus != None)
			ScrollTo(PadFocus);
		else
			PadTabs = FindPages(D); // nothing to focus on this page (e.g. the browser's News tab): work the tabs
	}
}

function UWindowPageControl FindPages(UWindowWindow W)
{
	local UWindowWindow C;
	local UWindowPageControl P;

	if (UWindowPageControl(W) != None)
		return UWindowPageControl(W);
	for (C = W.FirstChildWindow; C != None; C = C.NextSiblingWindow)
	{
		if (C.bWindowVisible)
		{
			P = FindPages(C);
			if (P != None)
				return P;
		}
	}
	return None;
}

// The page control around the focus, else the first one in the dialog.
function UWindowPageControl PageControlOf(UWindowWindow D)
{
	local UWindowWindow W;

	if (PadTabs != None)
		return PadTabs;
	for (W = PadFocus; W != None && W != D; W = W.ParentWindow)
		if (UWindowPageControl(W) != None)
			return UWindowPageControl(W);
	return FindPages(D);
}

// Leave the tab strip for the first control of the shown page.
function LeaveTabs(UWindowWindow D)
{
	PadTabs = None;
	PadFocus = None;
	PadUpdate();
	if (PadFocus != None)
		SetPadFocus(PadFocus);
}

// Tab strip: the tab next to the selected one on screen. Left/Right stay in its row (wrapping to the list order at
// the row ends); Up/Down pick the nearest tab in the row above/below. False when there is none (Down: leave the strip).
function bool TabDir(UWindowWindow D, int DX, int DY)
{
	local UWindowPageControl PC;
	local UWindowTabControlItem T, I, Best;
	local float CX, IX, Dist, BestDist;

	PC = PageControlOf(D);
	if (PC == None || PC.SelectedTab == None)
		return False;
	T = PC.SelectedTab;
	CX = T.TabLeft + T.TabWidth / 2;
	BestDist = 1000000;
	for (I = UWindowTabControlItem(PC.Items.Next); I != None; I = UWindowTabControlItem(I.Next))
	{
		if (I == T || I.TabLeft < 0)
			continue;
		IX = I.TabLeft + I.TabWidth / 2;
		if (DY == 0)
		{
			if (Abs(I.TabTop - T.TabTop) > 2 || (IX - CX) * DX <= 0)
				continue;
			Dist = Abs(IX - CX);
		}
		else
		{
			if ((I.TabTop - T.TabTop) * DY <= 2)
				continue;
			Dist = Abs(I.TabTop - T.TabTop) * 1000 + Abs(IX - CX);
		}
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = I;
		}
	}
	if (Best == None)
	{
		if (DY != 0)
			return DY < 0; // Up at the top row: stay; Down at the bottom row: leave the strip
		return SwitchPage(D, DX > 0);
	}
	PC.GotoTab(Best, True);
	return True;
}

function bool SwitchPage(UWindowWindow D, bool bNext)
{
	local UWindowPageControl PC;
	local UWindowTabControlItem T, N;

	PC = PageControlOf(D);
	if (PC == None || PC.SelectedTab == None)
		return False;
	T = PC.SelectedTab;
	if (bNext)
	{
		N = UWindowTabControlItem(T.Next);
		if (N == None)
			N = UWindowTabControlItem(PC.Items.Next);
	}
	else
	{
		if (T.Prev == PC.Items)
			N = None;
		else
			N = UWindowTabControlItem(T.Prev);
		if (N == None)
			N = UWindowTabControlItem(PC.Items.Last);
	}
	if (N != None && N != T)
	{
		PC.GotoTab(N, True);
		if (PadTabs == None)
			PadFocus = None;
	}
	return True;
}

function CycleCombo(UWindowComboControl C, int Dir)
{
	local int i, n;

	n = C.List.Items.Count();
	if (n <= 0)
		return;
	i = C.GetSelectedIndex() + Dir;
	if (i < 0) i = n - 1;
	if (i >= n) i = 0;
	C.SetSelectedIndex(i);
}

// ---------------------------------------------------------------- input

function bool PadKey(int Key)
{
	local PlayerPawn P;
	local UWindowWindow D;
	local UWindowComboControl Combo;
	local UWindowWindow N;
	local UWindowListBox List;
	local UWindowListBoxItem Item;
	local UBrowserServerGrid SGrid;
	local UBrowserTypesGrid TGrid; //#UT
	//#227 local UBrowserServerGrid TGrid; // 227's browser has no game-type grid: TGrid stays None
	local int Row, Rows;

	D = PadDialog();
	if (D == None)
		return False;
	PadUpdate();
	P = GetPlayerOwner();
	if (PadTabs != None)
	{
		switch (Key)
		{
		case P.EInputKey.IK_Space:
			return SwitchPage(D, False);
		case P.EInputKey.IK_Tab:
			return SwitchPage(D, True);
		case P.EInputKey.IK_Left:
			return TabDir(D, -1, 0);
		case P.EInputKey.IK_Right:
			return TabDir(D, 1, 0);
		case P.EInputKey.IK_Up:
			return TabDir(D, 0, -1);
		case P.EInputKey.IK_Down:
			if (!TabDir(D, 0, 1))
				LeaveTabs(D);
			return True;
		case P.EInputKey.IK_Enter:
			LeaveTabs(D);
			return True;
		}
		return False;
	}
	if (PadFocus == None)
		return False;
	Combo = UWindowComboControl(PadFocus);

	// An open combo list takes Up/Down/Enter.
	if (Combo != None && Combo.bListVisible)
	{
		if (Key == P.EInputKey.IK_Up || Key == P.EInputKey.IK_Down || Key == P.EInputKey.IK_Enter)
		{
			Combo.List.KeyDown(Key, 0, 0);
			return True;
		}
		if (Key == P.EInputKey.IK_Left || Key == P.EInputKey.IK_Right)
			return True;
	}

	// Server browser grids: Up/Down pick a row and leave the grid at either end; A joins the server / toggles the type.
	SGrid = UBrowserServerGrid(PadFocus);
	TGrid = UBrowserTypesGrid(PadFocus); //#UT
	if (SGrid != None || TGrid != None)
	{
		if (SGrid != None) { Row = SGrid.GetSelectedRow(); Rows = SGrid.Count; }
		else               { Row = TGrid.GetSelectedRow(); Rows = TGrid.Count; }
		if ((Key == P.EInputKey.IK_Up && Rows > 0 && (Row > 0 || Row < 0))
			|| (Key == P.EInputKey.IK_Down && Rows > 0 && Row < Rows - 1))
		{
			if (SGrid != None) SGrid.KeyDown(Key, 0, 0);
			else               TGrid.KeyDown(Key, 0, 0);
			return True;
		}
		if (Key == P.EInputKey.IK_Enter)
		{
			if (SGrid != None) SGrid.DoubleClickRow(Row);
			else               TGrid.DoubleClickRow(Row);
			return True;
		}
	}

	// List box: Up/Down walk the items and leave the list at either end; A = double-click the selected item
	// (e.g. moves a map/mutator to the other list).
	List = UWindowListBox(PadFocus);
	if (List != None)
	{
		if (Key == P.EInputKey.IK_Up || Key == P.EInputKey.IK_Down)
		{
			if (Key == P.EInputKey.IK_Up) Item = ListStep(List, -1);
			else                          Item = ListStep(List, 1);
			if (Item != None)
			{
				ListSelect(List, Item);
				return True;
			}
		}
		else if (Key == P.EInputKey.IK_Enter)
		{
			if (List.SelectedItem != None)
			{
				List.DoubleClickItem(List.SelectedItem);
				List.MakeSelectedVisible();
			}
			return True;
		}
	}

	switch (Key)
	{
	case P.EInputKey.IK_Left:
	case P.EInputKey.IK_Right:
		if (UWindowHSliderControl(PadFocus) != None)
		{
			PadFocus.KeyDown(Key, 0, 0);
			return True;
		}
		if (Combo != None)
		{
			if (Key == P.EInputKey.IK_Left) CycleCombo(Combo, -1);
			else                            CycleCombo(Combo, 1);
			return True;
		}
		NumCtl = 0;
		Collect(D);
		if (Key == P.EInputKey.IK_Left) N = FindDir(-1, 0);
		else                            N = FindDir(1, 0);
		SetPadFocus(N);
		return True;
	case P.EInputKey.IK_Up:
	case P.EInputKey.IK_Down:
		NumCtl = 0;
		Collect(D);
		if (Key == P.EInputKey.IK_Up) N = FindDir(0, -1);
		else                          N = FindDir(0, 1);
		if (N == None && Key == P.EInputKey.IK_Up)
		{
			PadTabs = PageControlOf(D);
			if (PadTabs != None)
				PadFocus = None;
			return True;
		}
		SetPadFocus(N);
		return True;
	case P.EInputKey.IK_Enter:
		if (Combo != None)
		{
			if (Combo.bCanEdit)
				return OpenKeyboard(Combo.EditBox);
			Combo.DropDown();
			return True;
		}
		if (UWindowEditControl(PadFocus) != None)
			return OpenKeyboard(UWindowEditControl(PadFocus).EditBox);
		if (UWindowButton(PadFocus) != None)
		{
			PadFocus.KeyDown(P.EInputKey.IK_Space, 0, 0);
			return True;
		}
		return False;
	case P.EInputKey.IK_Tab:
		return SwitchPage(D, True);
	case P.EInputKey.IK_Space:
		if (UWindowEditControl(PadFocus) != None)
			return False;
		return SwitchPage(D, False);
	}
	return False;
}

// A mouse click on a control moves the focus box there.
function WindowEvent(WinMessage Msg, Canvas C, float X, float Y, int Key)
{
	local UWindowWindow W, D;

	// The keyboard owns the pad: X/Y also arrive as mouse clicks from the bridge.
	if (bKb && (Msg == WM_LMouseDown || Msg == WM_LMouseUp || Msg == WM_RMouseDown || Msg == WM_RMouseUp
		|| Msg == WM_MMouseDown || Msg == WM_MMouseUp))
		return;
	Super.WindowEvent(Msg, C, X, Y, Key);
	if (Msg == WM_LMouseDown)
	{
		D = PadDialog();
		for (W = MouseWindow; W != None && W != D; W = W.ParentWindow)
			if (Focusable(W))
			{
				if (IsUnder(W, D))
				{
					PadFocus = W;
					PadTabs = None;
				}
				break;
			}
	}
}

// The key-binding window around the focused control, if any.
function UMenuCustomizeClientWindow CustomizeWindow()
{
	local UWindowWindow W;

	for (W = PadFocus; W != None; W = W.ParentWindow)
		if (UMenuCustomizeClientWindow(W) != None)
			return UMenuCustomizeClientWindow(W);
	return None;
}

// Pad names instead of "Joystick N" (the SDL bridge's layout, see SDL_utandroid.c).
function NamePadButtons(UMenuCustomizeClientWindow Cust)
{
	local int J;

	J = GetPlayerOwner().EInputKey.IK_Joy1;
	if (Cust.LocalizedKeyName[J] == PadButtonName[0])
		return;
	for (J = 0; J < 16; J++)
		Cust.LocalizedKeyName[GetPlayerOwner().EInputKey.IK_Joy1 + J] = PadButtonName[J];
}

// The key-binding window whose button is waiting for a key, if any.
function UMenuCustomizeClientWindow PollingWindow()
{
	local UMenuCustomizeClientWindow Cust;

	Cust = CustomizeWindow();
	if (Cust != None)
	{
		NamePadButtons(Cust);
		if (Cust.bPolling)
			return Cust;
	}
	return None;
}

function UMenuCustomizeClientWindow UpdatePolling()
{
	local UMenuCustomizeClientWindow Cust;

	Cust = PollingWindow();
	if (Cust != None && !bWasPolling)
		PollStartFrame = PadFrame;
	if (Cust == None && bWasPolling)
		PollEndFrame = PadFrame;
	bWasPolling = Cust != None;
	return Cust;
}

function bool NewJoyPress(int Key)
{
	local int J;
	local bool bNew;

	J = Key - GetPlayerOwner().EInputKey.IK_Joy1;
	bNew = PadFrame - JoyLastFrame[J] > 2;
	JoyLastFrame[J] = PadFrame;
	return bNew;
}

function bool IsJoyKey(int Key)
{
	local PlayerPawn P;

	P = GetPlayerOwner();
	return Key >= P.EInputKey.IK_Joy1 && Key <= P.EInputKey.IK_Joy16;
}

// ---------------------------------------------------------------- on-screen keyboard

function bool OpenKeyboard(UWindowEditBox E)
{
	if (E == None || !E.bCanEdit)
		return False;
	bKb = True;
	KbEscFrame = -100;
	KbEdit = E;
	E.bAllSelected = False;
	E.CaretOffset = Len(E.Value);
	E.bShowCaret = True;
	KbRow = 1;
	KbCol = 0;
	bKbShift = False;
	return True;
}

function CloseKeyboard()
{
	bKb = False;
	PollEndFrame = PadFrame; // the key half of the closing press does nothing
	if (KbEdit != None && KbEdit.bChangePending)
	{
		KbEdit.bChangePending = False;
		KbEdit.Notify(DE_Change);
	}
	KbEdit = None;
}

function KbType(string Ch)
{
	if (KbEdit.bNumericOnly)
	{
		if (Ch < "0" || Ch > "9")
		{
			if (!(KbEdit.bNumericFloat && Ch == "." && InStr(KbEdit.Value, ".") < 0))
				return;
		}
	}
	KbEdit.InsertText(Ch); // InsertUnicode takes a string in UT, a char code in 227
}

// The selected key: a character, or "#shift", "#space", "#del", "#done" on the bottom row.
function string KbCell(int R, int Col)
{
	if (R < 4)
	{
		if (bKbShift)
			return Mid(KbShiftRows[R], Col, 1);
		return Mid(KbRows[R], Col, 1);
	}
	if (Col < 2) return "#shift";
	if (Col < 6) return "#space";
	if (Col < 8) return "#del";
	return "#done";
}

function KbPress()
{
	local string K;

	K = KbCell(KbRow, KbCol);
	if (K == "#shift")      bKbShift = !bKbShift;
	else if (K == "#space") KbType(" ");
	else if (K == "#del")   KbEdit.Backspace();
	else if (K == "#done")  CloseKeyboard();
	else                    KbType(K);
}

function KeyboardKey(int Key)
{
	local PlayerPawn P;

	P = GetPlayerOwner();
	if (KbEdit == None || !IsShown(KbEdit))
	{
		CloseKeyboard();
		return;
	}
	switch (Key)
	{
	case P.EInputKey.IK_Up:    KbRow = (KbRow + 4) % 5; break;
	case P.EInputKey.IK_Down:  KbRow = (KbRow + 1) % 5; break;
	case P.EInputKey.IK_Left:  KbCol = (KbCol + 9) % 10; break;
	case P.EInputKey.IK_Right: KbCol = (KbCol + 1) % 10; break;
	case P.EInputKey.IK_Joy1:  KbPress(); break;                              // A
	case P.EInputKey.IK_Joy2:  KbEdit.Backspace(); KbBFrame = PadFrame; KbEscFrame = -100; break; // B
	case P.EInputKey.IK_Joy3:  KbType(" "); break;                             // X
	case P.EInputKey.IK_Joy4:  bKbShift = !bKbShift; break;                    // Y
	case P.EInputKey.IK_Joy5:  KbEdit.MoveLeft(); break;                       // L1
	case P.EInputKey.IK_Joy6:  KbEdit.MoveRight(); break;                      // R1
	}
}

function DrawDebug(Canvas C)
{
	local int i;
	local float X, Y;

	C.Font = Fonts[F_Normal];
	C.DrawColor.R = 255; C.DrawColor.G = 255; C.DrawColor.B = 0;
	C.SetPos(10, C.ClipY - 120);
	C.DrawText(EvLog, False);
	return;
	for (i = 0; i < NumCtl; i++)
	{
		GetRect(Ctl[i], X, Y);
		C.SetPos(C.ClipX - 520, 60 + i * 18);
		C.DrawText(string(i) @ string(Ctl[i].Class.Name) @ string(int(X)) @ string(int(Y)) @ string(int(Ctl[i].WinWidth)) @ string(int(Ctl[i].WinHeight)) @ string(Ctl[i] == PadFocus), False);
	}
}

function DrawKbCell(Canvas C, float X, float Y, float W, float H, string Label, bool bSel)
{
	local float TW, TH;

	if (bSel) { C.DrawColor.R = 255; C.DrawColor.G = 200; C.DrawColor.B = 0; }
	else      { C.DrawColor.R = 70;  C.DrawColor.G = 80;  C.DrawColor.B = 120; }
	C.SetPos(X + 2, Y + 2);
	C.DrawRect(Texture'UWindow.WhiteTexture', W - 4, H - 4);
	if (bSel) { C.DrawColor.R = 0;   C.DrawColor.G = 0;   C.DrawColor.B = 0; }
	else      { C.DrawColor.R = 255; C.DrawColor.G = 255; C.DrawColor.B = 255; }
	C.StrLen(Label, TW, TH);
	C.SetPos(X + (W - TW) / 2, Y + (H - TH) / 2);
	C.DrawText(Label, False);
}

function DrawKeyboard(Canvas C)
{
	local float Cell, X0, Y0, W, TW, TH;
	local int R, Col;
	local string S, Label;

	Cell = 20 * GUIScale;
	W = 10 * Cell;
	X0 = (C.ClipX - W) / 2;
	Y0 = C.ClipY - 6 * Cell - 6 * GUIScale;
	C.Style = 1;
	C.Font = Fonts[F_LargeBold];
	// panel + preview line
	C.DrawColor.R = 20; C.DrawColor.G = 22; C.DrawColor.B = 40;
	C.SetPos(X0 - 4 * GUIScale, Y0 - 4 * GUIScale);
	C.DrawRect(Texture'UWindow.WhiteTexture', W + 8 * GUIScale, 6 * Cell + 8 * GUIScale);
	C.DrawColor.R = 240; C.DrawColor.G = 240; C.DrawColor.B = 240;
	C.SetPos(X0 + 2, Y0 + 2);
	C.DrawRect(Texture'UWindow.WhiteTexture', W - 4, Cell - 4);
	S = Left(KbEdit.Value, KbEdit.CaretOffset) $ "|" $ Mid(KbEdit.Value, KbEdit.CaretOffset);
	C.StrLen(S, TW, TH);
	while (TW > W - 16 && Len(S) > 1)
	{
		S = Mid(S, 1);
		C.StrLen(S, TW, TH);
	}
	C.DrawColor.R = 0; C.DrawColor.G = 0; C.DrawColor.B = 0;
	C.SetPos(X0 + 8, Y0 + (Cell - TH) / 2);
	C.DrawText(S, False);
	// keys
	for (R = 0; R < 4; R++)
		for (Col = 0; Col < 10; Col++)
			DrawKbCell(C, X0 + Col * Cell, Y0 + (R + 1) * Cell, Cell, Cell, KbCell(R, Col), R == KbRow && Col == KbCol);
	Label = "Shift";
	if (bKbShift)
		Label = "SHIFT";
	DrawKbCell(C, X0,            Y0 + 5 * Cell, 2 * Cell, Cell, Label,   KbRow == 4 && KbCol < 2);
	DrawKbCell(C, X0 + 2 * Cell, Y0 + 5 * Cell, 4 * Cell, Cell, "Space", KbRow == 4 && KbCol >= 2 && KbCol < 6);
	DrawKbCell(C, X0 + 6 * Cell, Y0 + 5 * Cell, 2 * Cell, Cell, "Del",   KbRow == 4 && KbCol >= 6 && KbCol < 8);
	DrawKbCell(C, X0 + 8 * Cell, Y0 + 5 * Cell, 2 * Cell, Cell, "Done",  KbRow == 4 && KbCol >= 8);
}

function bool HotKeyDown(int Key, float X, float Y)
{
	local UMenuCustomizeClientWindow Cust;

	if (bPadDebug)
		EvLog = Right(EvLog $ " D" $ Key $ "@" $ (PadFrame % 1000), 200);
	// A held pad button arrives as a press every frame: only the first one of a run counts.
	if (IsJoyKey(Key) && !NewJoyPress(Key))
		return True;
	if (bKb)
	{
		KeyboardKey(Key);
		return True;
	}
	Cust = UpdatePolling();
	// X on the server list: refresh it.
	if (IsJoyKey(Key) && Cust == None && Key == GetPlayerOwner().EInputKey.IK_Joy3 && PadDialog() != None
		&& UBrowserServerGrid(PadFocus) != None)
	{
		UBrowserServerGrid(PadFocus).Refresh();
		return True;
	}
	if (IsJoyKey(Key))
	{
		// Bind it, unless it is the tail of the very press (A / Y) that started the wait.
		if (Cust != None && PadFrame - PollStartFrame >= 15)
		{
			Cust.KeyDown(Key, 0, 0);
			UpdatePolling();
		}
		return True;
	}
	// The arrows/Enter/Tab the bridge makes from a pad press arrive after its Joy button: ignore them while a binding
	// waits and right after one was made (else the A that was just bound would start a new wait).
	if (Cust != None || PadFrame - PollEndFrame < 15)
		return True;
	if (PadKey(Key))
		return True;
	return Super.HotKeyDown(Key, X, Y);
}

function bool HotKeyUp(int Key, float X, float Y)
{
	if (bPadDebug)
		EvLog = Right(EvLog $ " U" $ Key $ "@" $ (PadFrame % 1000), 200);
	if (IsJoyKey(Key))
		return True;
	// 227's key-binding window cancels the wait on any key release (KeyUp -> CancelKeySelection): the release of
	// the Enter the bridge sends with A would end it before the pad button to bind is pressed. Swallow releases
	// while a binding waits, and right after one was made.
	if (UpdatePolling() != None || PadFrame - PollEndFrame < 15)
		return True;
	return Super.HotKeyUp(Key, X, Y);
}

// Escape (B) closes an open combo list first.
function CloseActiveWindow()
{
	local UWindowComboControl Combo;
	local UMenuCustomizeClientWindow Cust;

	// Escape with the keyboard up: Start closes it (B's Escape half is ignored, B is backspace).
	if (bKb)
	{
		if (PadFrame - KbBFrame >= 15)
			KbEscFrame = PadFrame; // closes in 15 frames unless B's Joy button shows up
		return;
	}
	// Escape while a binding waits: cancel the wait, keep the dialog. Also ignore the Escape half of a B press that
	// just ended a wait.
	Cust = UpdatePolling();
	if (Cust != None)
	{
		Cust.bPolling = False;
		if (Cust.SelectedButton != None)
			Cust.SelectedButton.bDisabled = False;
		UpdatePolling();
		return;
	}
	if (PadFrame - PollEndFrame < 15)
		return;
	Combo = UWindowComboControl(PadFocus);
	if (Combo != None && Combo.bListVisible && PadDialog() != None)
	{
		Combo.CloseUp();
		return;
	}
	Super.CloseActiveWindow();
}

// ---------------------------------------------------------------- focus box

// Focus box around a rectangle in root (GUI) coordinates.
function DrawBox(Canvas C, float GX, float GY, float GW, float GH)
{
	local float X, Y, W, H, T;

	X = GX * GUIScale - 2;
	Y = GY * GUIScale - 2;
	W = GW * GUIScale + 4;
	H = GH * GUIScale + 4;
	T = FMax(2, GUIScale);
	if (W > 0 && H > 0)
	{
		C.Style = 1;
		C.DrawColor.R = 255;
		C.DrawColor.G = 200;
		C.DrawColor.B = 0;
		C.SetPos(X, Y);           C.DrawRect(Texture'UWindow.WhiteTexture', W, T);
		C.SetPos(X, Y + H - T);   C.DrawRect(Texture'UWindow.WhiteTexture', W, T);
		C.SetPos(X, Y);           C.DrawRect(Texture'UWindow.WhiteTexture', T, H);
		C.SetPos(X + W - T, Y);   C.DrawRect(Texture'UWindow.WhiteTexture', T, H);
	}
}

function DrawMouse(Canvas C)
{
	local float X, Y;
	local UWindowTabControlItem I;

	PadFrame++;
	UpdatePolling();
	PadUpdate();
	if (PadTabs != None)
	{
		I = PadTabs.SelectedTab;
		if (I != None && I.TabLeft >= 0)
		{
			PadTabs.TabArea.WindowToGlobal(I.TabLeft, I.TabTop, X, Y);
			DrawBox(C, X, Y, I.TabWidth, I.TabHeight);
		}
	}
	else if (PadFocus != None)
	{
		PadFocus.WindowToGlobal(0, 0, X, Y);
		DrawBox(C, X, Y, PadFocus.WinWidth, PadFocus.WinHeight);
	}
	if (bPadDebug)
		DrawDebug(C);
	if (bKb && KbEscFrame >= 0 && PadFrame - KbEscFrame >= 15)
		CloseKeyboard();
	if (bKb)
	{
		if (KbEdit == None || !IsShown(KbEdit))
			CloseKeyboard();
		else
			DrawKeyboard(C);
	}
	Super.DrawMouse(C);
}

defaultproperties
{
      PollEndFrame=-100
      KbBFrame=-100
      KbEscFrame=-100
      KbRows(0)="1234567890"
      KbRows(1)="qwertyuiop"
      KbRows(2)="asdfghjkl'"
      KbRows(3)="zxcvbnm,.-"
      KbShiftRows(0)="!@#$%^&*()"
      KbShiftRows(1)="QWERTYUIOP"
      KbShiftRows(2)="ASDFGHJKL;"
      KbShiftRows(3)="ZXCVBNM_:/"
      PadButtonName(0)="A"
      PadButtonName(1)="B"
      PadButtonName(2)="X"
      PadButtonName(3)="Y"
      PadButtonName(4)="L1"
      PadButtonName(5)="R1"
      PadButtonName(6)="L2"
      PadButtonName(7)="R2"
      PadButtonName(8)="Select"
      PadButtonName(9)="Start"
      PadButtonName(10)="L3"
      PadButtonName(11)="R3"
      PadButtonName(12)="D-Pad Up"
      PadButtonName(13)="D-Pad Down"
      PadButtonName(14)="D-Pad Left"
      PadButtonName(15)="D-Pad Right"
      ConfiguredGUIScale=3.000000 //#UT
      AutoGUIScale=False //#UT
      //#227 GUIScale=3.000000
      LookAndFeelClass="UMenu.UMenuBlueLookAndFeel"
}
