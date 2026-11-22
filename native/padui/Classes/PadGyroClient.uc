//=============================================================================
// PadGyroClient - the Handheld Settings window (Mod > Handheld Settings).
//
// Gyro: the app feeds the device's real rotation (angle per frame) on the virtual pad's JoyU (turn) / JoyV (look up)
// axes, so the gyro settings only rewrite those bindings: Speed = BaseSpeed * Sensitivity (BaseSpeed = the tuned 100%),
// 0 when off, negative look up when inverted. Saved here ([PadUI.PadGyroClient]) and, through SET Input, in User.ini.
//
// Look stick curve, turn boost and the performance overlay live in the app's native layer (files/handheld.cfg).
// We reach it through the clipboard: copying "utandroid:set ..." never hits the real clipboard (the loader takes
// it), and after "utandroid:get" the next paste returns "perf=1 curve=1.50 boost=1.00".
// The frame rate limit (display refresh) and gyro on/off also go to the app that way.
//=============================================================================
class PadGyroClient extends UMenuDialogClientWindow;

var config float Sensitivity;   // 1.0 = 100%
var config bool bEnabled;
var config bool bInvert;
var float BaseSpeed;            // must match GameConfig.kt's JoyU/JoyV defaults

var UWindowCheckbox EnableCheck, InvertCheck, BoostCheck, PerfCheck;
var UWindowHSliderControl SensSlider, CurveSlider;
var UWindowComboControl FpsCombo;
var UWindowSmallButton CloseButton;
var bool bLoaded;

function Created()
{
	Super.Created();

	EnableCheck = AddCheck("Gyro aiming", "Turn the handheld to aim. The right stick still works on top.", bEnabled);
	SensSlider = AddSlider("How far the view turns for a turn of the handheld.", 25, 300, 5, Sensitivity * 100);
	InvertCheck = AddCheck("Invert gyro up/down", "Tilt the top edge towards you to look down instead of up.", bInvert);
	CurveSlider = AddSlider("Look stick response: higher = finer aim near the centre, same top speed.", 100, 300, 10, 150);
	BoostCheck = AddCheck("Turn boost", "Holding the look stick fully sideways speeds the turn up after a moment.", True);
	PerfCheck = AddCheck("Performance overlay", "FPS, power, CPU and GPU in the corner. L3 + R3 toggles it too.", False);
	FpsCombo = UWindowComboControl(CreateControl(class'UWindowComboControl', 10, 0, 100, 1));
	FpsCombo.SetText("Frame rate limit");
	FpsCombo.SetHelpText("Lower limits run the display slower too: longer battery life, less heat.");
	FpsCombo.SetFont(F_Normal);
	FpsCombo.SetEditable(False);
	FpsCombo.AddItem("Display maximum", "0");
	FpsCombo.AddItem("60 fps", "60");
	FpsCombo.SetSelectedIndex(0);

	CloseButton = UWindowSmallButton(CreateControl(class'UWindowSmallButton', 0, 0, 48, 16));
	CloseButton.SetText("Close");

	ReadNative();
	UpdateLabels();
	bLoaded = True;
}

function UWindowCheckbox AddCheck(string Text, string Help, bool bOn)
{
	local UWindowCheckbox C;

	C = UWindowCheckbox(CreateControl(class'UWindowCheckbox', 10, 0, 100, 1));
	C.SetText(Text);
	C.SetHelpText(Help);
	C.SetFont(F_Normal);
	C.Align = TA_Left;
	C.bChecked = bOn;
	return C;
}

function UWindowHSliderControl AddSlider(string Help, float Min, float Max, int Step, float Value)
{
	local UWindowHSliderControl S;

	S = UWindowHSliderControl(CreateControl(class'UWindowHSliderControl', 10, 0, 100, 1));
	S.SetRange(Min, Max, Step);
	S.SetFont(F_Normal);
	S.SetHelpText(Help);
	S.SetValue(Value, True);
	return S;
}

// The frame creates this client at its initial size and resizes it afterwards: lay out on every paint.
function BeforePaint(Canvas C, float X, float Y)
{
	local float W, Top;

	Super.BeforePaint(C, X, Y);
	W = WinWidth - 20;
	Top = 6;
	Place(EnableCheck, W, Top);
	Place(SensSlider, W, Top);
	Place(InvertCheck, W, Top);
	Top += 4;
	Place(CurveSlider, W, Top);
	Place(BoostCheck, W, Top);
	Top += 4;
	Place(PerfCheck, W, Top);
	Place(FpsCombo, W, Top);
	FpsCombo.EditBoxWidth = W * 0.45;
	SensSlider.SliderWidth = W * 0.45;
	CurveSlider.SliderWidth = W * 0.45;
	CloseButton.WinLeft = WinWidth - 58;
	CloseButton.WinTop = WinHeight - 22;
}

function Place(UWindowDialogControl C, float W, out float Top)
{
	C.WinLeft = 10;
	C.WinTop = Top;
	C.SetSize(W, C.WinHeight);
	Top += 19;
}

function UpdateLabels()
{
	SensSlider.SetText("Gyro sensitivity" @ int(SensSlider.GetValue()) $ "%");
	CurveSlider.SetText("Look stick curve" @ FormatCurve(CurveSlider.GetValue()));
}

function string FormatCurve(float V)
{
	local int I;

	I = int(V + 0.5);
	// (UnrealScript's % works on floats: keep to ints here, "1.50" not "1.00")
	return string(I / 100) $ "." $ Right("0" $ string(I - (I / 100) * 100), 2);
}

// ---------------------------------------------------------------- native settings (clipboard channel)

function ReadNative()
{
	local string R;

	GetPlayerOwner().CopyToClipboard("utandroid:get");
	R = GetPlayerOwner().PasteFromClipboard();
	if (InStr(R, "perf=") < 0)
		return; // not running on the Android build
	PerfCheck.bChecked = Value(R, "perf") > 0.5;
	CurveSlider.SetValue(Value(R, "curve") * 100, True);
	BoostCheck.bChecked = Value(R, "boost") > 0.01;
	if (FpsCombo.FindItemIndex2(string(int(Value(R, "fps")))) >= 0)
		FpsCombo.SetSelectedIndex(FpsCombo.FindItemIndex2(string(int(Value(R, "fps")))));
}

function float Value(string S, string Key)
{
	local int I;

	I = InStr(S, Key $ "=");
	if (I < 0)
		return 0;
	S = Mid(S, I + Len(Key) + 1);
	I = InStr(S, " ");
	if (I >= 0)
		S = Left(S, I);
	return float(S);
}

function WriteNative()
{
	local string Cmd;

	Cmd = "utandroid:set perf=";
	if (PerfCheck.bChecked) Cmd = Cmd $ "1"; else Cmd = Cmd $ "0";
	Cmd = Cmd @ "curve=" $ FormatCurve(CurveSlider.GetValue());
	if (BoostCheck.bChecked) Cmd = Cmd @ "boost=1.0"; else Cmd = Cmd @ "boost=0";
	if (bEnabled) Cmd = Cmd @ "gyro=1"; else Cmd = Cmd @ "gyro=0";
	Cmd = Cmd @ "fps=" $ FpsCombo.GetValue2();
	GetPlayerOwner().CopyToClipboard(Cmd);
}

// ---------------------------------------------------------------- gyro bindings

function ApplyGyro()
{
	local float Speed;

	Speed = 0;
	if (bEnabled)
		Speed = BaseSpeed * Sensitivity;
	GetPlayerOwner().ConsoleCommand("SET Input JoyU Axis aTurn Speed=" $ Speed);
	if (bInvert)
		Speed = -Speed;
	GetPlayerOwner().ConsoleCommand("SET Input JoyV Axis aLookUp Speed=" $ Speed);
	SaveConfig();
	WriteNative(); // the app stops the gyro sensor when it's off
}

function Notify(UWindowDialogControl C, byte E)
{
	Super.Notify(C, E);
	if (C == CloseButton && E == DE_Click)
	{
		ParentWindow.Close();
		return;
	}
	if (!bLoaded || E != DE_Change)
		return;
	switch (C)
	{
	case SensSlider:
		Sensitivity = SensSlider.GetValue() / 100.0;
		UpdateLabels();
		ApplyGyro();
		break;
	case EnableCheck:
		bEnabled = EnableCheck.bChecked;
		ApplyGyro();
		break;
	case InvertCheck:
		bInvert = InvertCheck.bChecked;
		ApplyGyro();
		break;
	case CurveSlider:
		UpdateLabels();
		WriteNative();
		break;
	case BoostCheck:
	case PerfCheck:
	case FpsCombo:
		WriteNative();
		break;
	}
}

defaultproperties
{
      Sensitivity=1.000000
      bEnabled=True
      bInvert=False
      BaseSpeed=16000.000000
}
