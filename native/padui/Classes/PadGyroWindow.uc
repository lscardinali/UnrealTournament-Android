//=============================================================================
// PadGyroWindow - the Gyro Aiming settings dialog.
//=============================================================================
class PadGyroWindow extends UMenuFramedWindow;

function Created()
{
	bStatusBar = False;
	bSizable = False;
	Super.Created();
	SetSize(250, 210);
	WinLeft = Root.WinWidth / 2 - WinWidth / 2;
	WinTop = Root.WinHeight / 2 - WinHeight / 2;
}

defaultproperties
{
      ClientClass=Class'PadUI.PadGyroClient'
      WindowTitle="Handheld Settings"
}
