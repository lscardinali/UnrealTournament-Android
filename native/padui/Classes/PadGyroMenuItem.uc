//=============================================================================
// PadGyroMenuItem - "Gyro Aiming" in the Mod menu (listed by System/../SystemLocalized/int/PadUI.int).
//=============================================================================
class PadGyroMenuItem expands UMenuModMenuItem;

function Execute()
{
	MenuItem.Owner.Root.CreateWindow(class'PadGyroWindow', 0, 0, 100, 100, None, True);
}

defaultproperties
{
      MenuCaption="&Handheld Settings"
      MenuHelp="Gyro aiming, look stick response, performance overlay."
}
