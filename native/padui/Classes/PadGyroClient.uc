//=============================================================================
// PadGyroClient - Gyro Aiming settings.
//
// The app feeds the device's real rotation (angle per frame) on the virtual pad's JoyU (turn) / JoyV (look up) axes
// (GyroAim.kt -> SDL_utandroid.c). How far that turns the view is just the binding's Speed, so these settings only
// rewrite the two bindings: Speed = BaseSpeed * Sensitivity (BaseSpeed = the tuned 100%), 0 when off, negative look
// up when inverted. Saved here ([PadUI.PadGyroClient] in UnrealTournament.ini) and, through SET Input, in User.ini.
//=============================================================================
class PadGyroClient extends UMenuDialogClientWindow;

var config float Sensitivity;   // 1.0 = 100%
var config bool bEnabled;
var config bool bInvert;
var float BaseSpeed;            // must match GameConfig.kt's JoyU/JoyV defaults

var UWindowHSliderControl SensSlider;
var UWindowCheckbox EnableCheck, InvertCheck;
var UWindowSmallButton CloseButton;
var bool bLoaded;

function Created()
{
	local float W;

	Super.Created();
	W = WinWidth - 20;

	EnableCheck = UWindowCheckbox(CreateControl(class'UWindowCheckbox', 10, 6, W, 1));
	EnableCheck.SetText("Gyro aiming");
	EnableCheck.SetHelpText("Turn the handheld to aim. The right stick still works on top.");
	EnableCheck.SetFont(F_Normal);
	EnableCheck.Align = TA_Left;
	EnableCheck.bChecked = bEnabled;

	SensSlider = UWindowHSliderControl(CreateControl(class'UWindowHSliderControl', 10, 26, W, 1));
	SensSlider.SetRange(25, 300, 5);
	SensSlider.SetFont(F_Normal);
	SensSlider.SetHelpText("How far the view turns for a turn of the handheld.");
	SensSlider.SetValue(Sensitivity * 100, True);

	InvertCheck = UWindowCheckbox(CreateControl(class'UWindowCheckbox', 10, 46, W, 1));
	InvertCheck.SetText("Invert up/down");
	InvertCheck.SetHelpText("Tilt the top edge towards you to look down instead of up.");
	InvertCheck.SetFont(F_Normal);
	InvertCheck.Align = TA_Left;
	InvertCheck.bChecked = bInvert;

	CloseButton = UWindowSmallButton(CreateControl(class'UWindowSmallButton', WinWidth - 58, 66, 48, 16));
	CloseButton.SetText("Close");

	UpdateLabel();
	bLoaded = True;
}

// The frame creates this client at its initial size and resizes it afterwards: lay out on every paint.
function BeforePaint(Canvas C, float X, float Y)
{
	local float W;

	Super.BeforePaint(C, X, Y);
	W = WinWidth - 20;
	EnableCheck.SetSize(W, EnableCheck.WinHeight);
	InvertCheck.SetSize(W, InvertCheck.WinHeight);
	SensSlider.SetSize(W, SensSlider.WinHeight);
	SensSlider.SliderWidth = W * 0.45;
	CloseButton.WinLeft = WinWidth - 58;
	CloseButton.WinTop = WinHeight - 22;
}

function UpdateLabel()
{
	SensSlider.SetText("Sensitivity" @ int(SensSlider.GetValue()) $ "%");
}

function Apply()
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
		UpdateLabel();
		Apply();
		break;
	case EnableCheck:
		bEnabled = EnableCheck.bChecked;
		Apply();
		break;
	case InvertCheck:
		bInvert = InvertCheck.bChecked;
		Apply();
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
